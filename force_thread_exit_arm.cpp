#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <android/log.h>
#include <elf.h>           // NT_PRSTATUS
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>       // PTRACE_GETREGSET / PTRACE_SETREGSET
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

#define TAG "ForceThreadExit"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
//#define LOGI(...) do {} while(0)
//#define LOGE(...) do {} while(0)

constexpr int kDefaultExitCode = 42;
constexpr int kForceExitSignal = SIGUSR1;
constexpr int kWaitExitTimeoutMs = 1000;
constexpr int kWaitExitStepUs = 1000;

volatile sig_atomic_t g_force_exit_target_tid = -1;
volatile sig_atomic_t g_force_exit_code = 0;
volatile sig_atomic_t g_force_exit_active_signo = kForceExitSignal;
volatile sig_atomic_t g_force_exit_handler_installed = 0;
volatile sig_atomic_t g_force_exit_handler_installed_signo = 0;
volatile sig_atomic_t g_force_exit_handler_enter_count = 0;
volatile sig_atomic_t g_force_exit_handler_matched_count = 0;
volatile sig_atomic_t g_force_exit_handler_tid_mismatch_count = 0;
volatile sig_atomic_t g_force_exit_handler_unexpected_signo_count = 0;
volatile sig_atomic_t g_force_exit_handler_syscall_returned = 0;
volatile sig_atomic_t g_force_exit_handler_last_signo = 0;
volatile sig_atomic_t g_force_exit_handler_last_tid = -1;
volatile sig_atomic_t g_force_exit_handler_last_target_tid = -1;
volatile sig_atomic_t g_force_exit_handler_last_exit_code = 0;
pthread_mutex_t g_force_exit_install_lock = PTHREAD_MUTEX_INITIALIZER;

#if defined(__aarch64__) || defined(__arm__)
#define FORCE_THREAD_EXIT_ARM_SUPPORTED 1
#else
#define FORCE_THREAD_EXIT_ARM_SUPPORTED 0
#endif

pid_t gettid_linux() {
    return static_cast<pid_t>(syscall(__NR_gettid));
}

bool is_current_process_tid(pid_t tid) {
    char path[128];
    std::snprintf(path, sizeof(path), "/proc/self/task/%d", tid);
    return access(path, F_OK) == 0;
}

bool wait_tid_disappear_from_current_process(pid_t tid, int timeout_ms) {
    if (timeout_ms <= 0) {
        timeout_ms = kWaitExitTimeoutMs;
    }

    int waited_us = 0;
    const int timeout_us = timeout_ms * 1000;

    while (waited_us < timeout_us) {
        if (!is_current_process_tid(tid)) {
            return true;
        }

        usleep(kWaitExitStepUs);
        waited_us += kWaitExitStepUs;
    }

    return !is_current_process_tid(tid);
}

void reset_force_exit_handler_diag() {
    g_force_exit_handler_enter_count = 0;
    g_force_exit_handler_matched_count = 0;
    g_force_exit_handler_tid_mismatch_count = 0;
    g_force_exit_handler_unexpected_signo_count = 0;
    g_force_exit_handler_syscall_returned = 0;
    g_force_exit_handler_last_signo = 0;
    g_force_exit_handler_last_tid = -1;
    g_force_exit_handler_last_target_tid = static_cast<sig_atomic_t>(g_force_exit_target_tid);
    g_force_exit_handler_last_exit_code = static_cast<sig_atomic_t>(g_force_exit_code);
}

void log_force_exit_handler_diag(const char* stage) {
    LOGI("[diag] handler stage=%s entered=%d matched=%d tid_mismatch=%d unexpected_signo=%d "
         "syscall_returned=%d active_signo=%d installed_signo=%d last_signo=%d last_tid=%d last_target_tid=%d last_exit_code=%d current_target_tid=%d current_exit_code=%d",
         stage ? stage : "<unknown>",
         static_cast<int>(g_force_exit_handler_enter_count),
         static_cast<int>(g_force_exit_handler_matched_count),
         static_cast<int>(g_force_exit_handler_tid_mismatch_count),
         static_cast<int>(g_force_exit_handler_unexpected_signo_count),
         static_cast<int>(g_force_exit_handler_syscall_returned),
         static_cast<int>(g_force_exit_active_signo),
         static_cast<int>(g_force_exit_handler_installed_signo),
         static_cast<int>(g_force_exit_handler_last_signo),
         static_cast<int>(g_force_exit_handler_last_tid),
         static_cast<int>(g_force_exit_handler_last_target_tid),
         static_cast<int>(g_force_exit_handler_last_exit_code),
         static_cast<int>(g_force_exit_target_tid),
         static_cast<int>(g_force_exit_code));
}

unsigned long long signal_mask_for_signo(int signo) {
    if (signo <= 0 || signo > 64) {
        return 0;
    }

    return 1ULL << (signo - 1);
}

bool status_line_matches_key(const char* line, const char* key) {
    size_t key_len = std::strlen(key);
    return std::strncmp(line, key, key_len) == 0 && line[key_len] == ':';
}

unsigned long long parse_status_signal_mask(const char* line, const char* key) {
    if (!status_line_matches_key(line, key)) {
        return 0;
    }

    const char* p = std::strchr(line, ':');
    if (!p) {
        return 0;
    }

    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }

    return std::strtoull(p, nullptr, 16);
}

bool read_thread_signal_masks(pid_t tid,
                              unsigned long long* out_sig_blk,
                              unsigned long long* out_sig_ign,
                              unsigned long long* out_sig_cgt) {
    char path[128];
    std::snprintf(path, sizeof(path), "/proc/self/task/%d/status", tid);

    FILE* fp = std::fopen(path, "r");
    if (!fp) {
        LOGE("[diag] read signal masks failed: tid=%d open %s failed errno=%d, %s",
             tid,
             path,
             errno,
             std::strerror(errno));
        return false;
    }

    unsigned long long sig_blk = 0;
    unsigned long long sig_ign = 0;
    unsigned long long sig_cgt = 0;

    char line[512];
    while (std::fgets(line, sizeof(line), fp)) {
        if (status_line_matches_key(line, "SigBlk")) {
            sig_blk = parse_status_signal_mask(line, "SigBlk");
        } else if (status_line_matches_key(line, "SigIgn")) {
            sig_ign = parse_status_signal_mask(line, "SigIgn");
        } else if (status_line_matches_key(line, "SigCgt")) {
            sig_cgt = parse_status_signal_mask(line, "SigCgt");
        }
    }

    std::fclose(fp);

    if (out_sig_blk) {
        *out_sig_blk = sig_blk;
    }
    if (out_sig_ign) {
        *out_sig_ign = sig_ign;
    }
    if (out_sig_cgt) {
        *out_sig_cgt = sig_cgt;
    }

    return true;
}

int choose_force_exit_signal_for_tid(pid_t tid) {
    unsigned long long sig_blk = 0;
    unsigned long long sig_ign = 0;
    unsigned long long sig_cgt = 0;

    if (!read_thread_signal_masks(tid, &sig_blk, &sig_ign, &sig_cgt)) {
        LOGE("[-] cannot read target signal mask, fallback to default signo=%d", kForceExitSignal);
        return kForceExitSignal;
    }

    const int candidates[] = {SIGUSR1, SIGUSR2};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        int signo = candidates[i];
        unsigned long long mask = signal_mask_for_signo(signo);
        if ((sig_blk & mask) == 0) {
            if (signo != kForceExitSignal) {
                LOGI("[+] default force-exit signal signo=%d is blocked by target tid=%d; use fallback signo=%d",
                     kForceExitSignal,
                     tid,
                     signo);
            }
            LOGI("[diag] choose signal tid=%d selected_signo=%d SigBlk=0x%llx SigIgn=0x%llx SigCgt=0x%llx",
                 tid,
                 signo,
                 sig_blk,
                 sig_ign,
                 sig_cgt);
            return signo;
        }
    }

    LOGE("[-] all candidate force-exit signals are blocked by target tid=%d: SigBlk=0x%llx candidates={%d,%d}",
         tid,
         sig_blk,
         SIGUSR1,
         SIGUSR2);
    return -1;
}

void log_thread_signal_status(pid_t tid, const char* stage) {
    char path[128];
    std::snprintf(path, sizeof(path), "/proc/self/task/%d/status", tid);

    FILE* fp = std::fopen(path, "r");
    if (!fp) {
        LOGE("[diag] signal-status stage=%s tid=%d open %s failed errno=%d, %s",
             stage ? stage : "<unknown>",
             tid,
             path,
             errno,
             std::strerror(errno));
        return;
    }

    char name[256] = "";
    char state[256] = "";
    unsigned long long sig_pnd = 0;
    unsigned long long shd_pnd = 0;
    unsigned long long sig_blk = 0;
    unsigned long long sig_ign = 0;
    unsigned long long sig_cgt = 0;

    char line[512];
    while (std::fgets(line, sizeof(line), fp)) {
        if (status_line_matches_key(line, "Name")) {
            std::snprintf(name, sizeof(name), "%s", line);
        } else if (status_line_matches_key(line, "State")) {
            std::snprintf(state, sizeof(state), "%s", line);
        } else if (status_line_matches_key(line, "SigPnd")) {
            sig_pnd = parse_status_signal_mask(line, "SigPnd");
        } else if (status_line_matches_key(line, "ShdPnd")) {
            shd_pnd = parse_status_signal_mask(line, "ShdPnd");
        } else if (status_line_matches_key(line, "SigBlk")) {
            sig_blk = parse_status_signal_mask(line, "SigBlk");
        } else if (status_line_matches_key(line, "SigIgn")) {
            sig_ign = parse_status_signal_mask(line, "SigIgn");
        } else if (status_line_matches_key(line, "SigCgt")) {
            sig_cgt = parse_status_signal_mask(line, "SigCgt");
        }
    }

    std::fclose(fp);

    int active_signo = static_cast<int>(g_force_exit_active_signo);
    unsigned long long force_sig_mask = signal_mask_for_signo(active_signo);
    LOGI("[diag] signal-status stage=%s tid=%d signo=%d bit=0x%llx "
         "SigPnd=0x%llx hit=%d ShdPnd=0x%llx hit=%d SigBlk=0x%llx hit=%d SigIgn=0x%llx hit=%d SigCgt=0x%llx hit=%d",
         stage ? stage : "<unknown>",
         tid,
         active_signo,
         force_sig_mask,
         sig_pnd,
         (sig_pnd & force_sig_mask) != 0,
         shd_pnd,
         (shd_pnd & force_sig_mask) != 0,
         sig_blk,
         (sig_blk & force_sig_mask) != 0,
         sig_ign,
         (sig_ign & force_sig_mask) != 0,
         sig_cgt,
         (sig_cgt & force_sig_mask) != 0);

    if (name[0] != '\0') {
        LOGI("[diag] signal-status stage=%s tid=%d %s", stage ? stage : "<unknown>", tid, name);
    }
    if (state[0] != '\0') {
        LOGI("[diag] signal-status stage=%s tid=%d %s", stage ? stage : "<unknown>", tid, state);
    }
}

void force_exit_signal_handler(int signo, siginfo_t* info, void* context) {
    (void) info;
    (void) context;

    g_force_exit_handler_enter_count++;
    g_force_exit_handler_last_signo = static_cast<sig_atomic_t>(signo);

    pid_t current_tid = gettid_linux();
    g_force_exit_handler_last_tid = static_cast<sig_atomic_t>(current_tid);
    g_force_exit_handler_last_target_tid = g_force_exit_target_tid;
    g_force_exit_handler_last_exit_code = g_force_exit_code;

    if (signo != static_cast<int>(g_force_exit_active_signo)) {
        g_force_exit_handler_unexpected_signo_count++;
        return;
    }

    if (current_tid != static_cast<pid_t>(g_force_exit_target_tid)) {
        g_force_exit_handler_tid_mismatch_count++;
        return;
    }

    g_force_exit_handler_matched_count++;
    syscall(__NR_exit, static_cast<int>(g_force_exit_code));
    g_force_exit_handler_syscall_returned = 1;
}

bool install_force_exit_signal_handler(int signo) {
    if (g_force_exit_handler_installed == 1 &&
        g_force_exit_handler_installed_signo == static_cast<sig_atomic_t>(signo)) {
        return true;
    }

    pthread_mutex_lock(&g_force_exit_install_lock);

    if (g_force_exit_handler_installed == 1 &&
        g_force_exit_handler_installed_signo == static_cast<sig_atomic_t>(signo)) {
        pthread_mutex_unlock(&g_force_exit_install_lock);
        return true;
    }

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = force_exit_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESTART;

    if (sigaction(signo, &sa, nullptr) != 0) {
        LOGE("[-] sigaction failed: signo=%d errno=%d, %s",
             signo,
             errno,
             std::strerror(errno));
        pthread_mutex_unlock(&g_force_exit_install_lock);
        return false;
    }

    g_force_exit_handler_installed = 1;
    g_force_exit_handler_installed_signo = static_cast<sig_atomic_t>(signo);
    pthread_mutex_unlock(&g_force_exit_install_lock);

    LOGI("[+] force-exit signal handler installed: signo=%d", signo);
    return true;
}

int force_current_process_thread_exit_by_signal(pid_t tid, int exit_code) {
    if (tid == gettid_linux()) {
        LOGE("[-] refusing to force-exit current caller thread: tid=%d", tid);
        return -1;
    }

    int force_exit_signo = choose_force_exit_signal_for_tid(tid);
    if (force_exit_signo <= 0) {
        return -1;
    }

    g_force_exit_active_signo = static_cast<sig_atomic_t>(force_exit_signo);

    if (!install_force_exit_signal_handler(force_exit_signo)) {
        return -1;
    }

    g_force_exit_target_tid = static_cast<sig_atomic_t>(tid);
    g_force_exit_code = static_cast<sig_atomic_t>(exit_code);
    reset_force_exit_handler_diag();
    log_force_exit_handler_diag("before_tgkill");
    log_thread_signal_status(tid, "before_tgkill");

    int ret = static_cast<int>(syscall(__NR_tgkill, getpid(), tid, force_exit_signo));
    if (ret != 0) {
        LOGE("[-] tgkill failed: pid=%d tid=%d signo=%d errno=%d, %s",
             getpid(),
             tid,
             force_exit_signo,
             errno,
             std::strerror(errno));
        return -1;
    }

    LOGI("[+] Sent force-exit signal to current-process thread: pid=%d tid=%d signo=%d exit_code=%d",
         getpid(),
         tid,
         force_exit_signo,
         exit_code);

    log_force_exit_handler_diag("after_tgkill");
    log_thread_signal_status(tid, "after_tgkill");

    if (!wait_tid_disappear_from_current_process(tid, kWaitExitTimeoutMs)) {
        log_force_exit_handler_diag("after_wait_timeout");
        log_thread_signal_status(tid, "after_wait_timeout");
        LOGE("[-] target thread still alive after force-exit signal timeout: tid=%d timeout_ms=%d. "
             "Possible reasons: signal is blocked, handler was replaced, or thread is in uninterruptible state.",
             tid,
             kWaitExitTimeoutMs);
        return -1;
    }

    log_force_exit_handler_diag("after_wait_exited");
    log_thread_signal_status(tid, "after_wait_exited");
    LOGI("[+] Confirmed target thread exited: tid=%d", tid);
    return 0;
}

// 根据不同架构定义寄存器和指令
#if defined(__aarch64__)
// ==================== ARM64 架构配置 ====================
constexpr long kSyscallNrExit = __NR_exit;       // ARM64 中 __NR_exit 通常是 93
constexpr std::uint32_t kOpSyscall = 0xD4000001; // 'svc #0' 指令的机器码，32 位宽
typedef struct user_pt_regs regs_t;

// 设置 ARM64 寄存器
void set_regs_for_exit(regs_t* regs, int exit_code, unsigned long syscall_addr) {
    regs->regs[0] = static_cast<unsigned long>(exit_code); // X0 = arg0 (exit_code)
    regs->regs[8] = kSyscallNrExit;                        // X8 = syscall number
    regs->pc = syscall_addr;                               // PC 指向 svc 指令
}
#elif defined(__arm__)
// ==================== ARM 32位 架构配置 ====================
constexpr long kSyscallNrExit = __NR_exit;       // ARM32 中 __NR_exit 通常是 1
constexpr std::uint32_t kOpSyscall = 0xEF000000; // 'svc #0' 在 ARM 模式下的机器码，32 位宽
typedef struct user_regs regs_t;

// 设置 ARM32 寄存器
void set_regs_for_exit(regs_t* regs, int exit_code, unsigned long syscall_addr) {
    regs->uregs[0] = static_cast<unsigned long>(exit_code); // R0 = arg0 (exit_code)
    regs->uregs[7] = kSyscallNrExit;                        // R7 = syscall number
    regs->uregs[15] = syscall_addr;                         // R15 (PC) 指向 svc 指令
}
#endif

#if FORCE_THREAD_EXIT_ARM_SUPPORTED
bool wait_for_thread_stop(pid_t tid, int* status) {
    if (waitpid(tid, status, __WALL) < 0) {
        LOGE("[-] waitpid failed: errno=%d, %s", errno, std::strerror(errno));
        return false;
    }

    if (!WIFSTOPPED(*status)) {
        LOGE("[-] target thread %d did not stop, status=0x%x", tid, *status);
        return false;
    }

    return true;
}

bool wait_for_thread_exit_or_signal(pid_t tid, int* status) {
    if (waitpid(tid, status, __WALL) < 0) {
        if (errno == ECHILD || errno == ESRCH) {
            return true;
        }
        LOGE("[-] waitpid after PTRACE_CONT failed: errno=%d, %s",
             errno,
             std::strerror(errno));
        return false;
    }

    if (WIFEXITED(*status) || WIFSIGNALED(*status)) {
        return true;
    }

    LOGE("[-] target thread %d did not exit after continue, status=0x%x", tid, *status);
    return false;
}

void detach_quietly(pid_t tid) {
    ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
}
#endif

} // namespace

// 核心注入函数
int force_thread_exit_arm(pid_t tid, int exit_code) {
    if (tid <= 0) {
        return -1;
    }
    if (is_current_process_tid(tid)) {
        LOGI("[+] target tid=%d belongs to current process, use tgkill signal path instead of ptrace", tid);
        return force_current_process_thread_exit_by_signal(tid, exit_code);
    }

#if !FORCE_THREAD_EXIT_ARM_SUPPORTED
    (void) tid;
    (void) exit_code;
    LOGE("[-] force_thread_exit_arm is only implemented for ARM/ARM64; current ABI is unsupported.");
    return -1;
#else
    int status = 0;
    regs_t regs;
    std::memset(&regs, 0, sizeof(regs));

    struct iovec iov;
    iov.iov_base = &regs;
    iov.iov_len = sizeof(regs);

    // 1. ptrace attach 到目标 TID
    if (ptrace(PTRACE_ATTACH, tid, nullptr, nullptr) < 0) {
        LOGE("[-] PTRACE_ATTACH failed: tid=%d errno=%d, %s", tid, errno, std::strerror(errno));
        return -1;
    }
    LOGI("[+] Attaching to thread %d...", tid);

    // 2. waitpid 等目标线程停住
    if (!wait_for_thread_stop(tid, &status)) {
        detach_quietly(tid);
        return -1;
    }

    // 3. 读取目标线程寄存器。ARM/ARM64 推荐使用 PTRACE_GETREGSET。
    if (ptrace(PTRACE_GETREGSET,
               tid,
               reinterpret_cast<void*>(NT_PRSTATUS),
               &iov) < 0) {
        LOGE("[-] PTRACE_GETREGSET failed: tid=%d errno=%d, %s", tid, errno, std::strerror(errno));
        detach_quietly(tid);
        return -1;
    }

    // 获取当前的 PC 寄存器地址
#if defined(__aarch64__)
    unsigned long current_pc = static_cast<unsigned long>(regs.pc);
#elif defined(__arm__)
    unsigned long current_pc = static_cast<unsigned long>(regs.uregs[15]);
#endif

    // 4. 修改目标内存：在当前 PC 位置写入 'svc #0' 触发系统调用。
    // 注意：这会覆盖目标线程当前 PC 处的代码，属于强制退出方案，只建议诊断场景使用。
    errno = 0;
    long original_code = ptrace(PTRACE_PEEKTEXT,
                                tid,
                                reinterpret_cast<void*>(current_pc),
                                nullptr);
    if (original_code == -1 && errno != 0) {
        LOGE("[-] PTRACE_PEEKTEXT failed: tid=%d pc=0x%lx errno=%d, %s",
             tid,
             current_pc,
             errno,
             std::strerror(errno));
        detach_quietly(tid);
        return -1;
    }

    // 将低 32 位替换为 svc #0 指令。
    long syscall_code = static_cast<long>(
            (static_cast<unsigned long>(original_code) & ~0xFFFFFFFFUL) |
            static_cast<unsigned long>(kOpSyscall));

    if (ptrace(PTRACE_POKETEXT,
               tid,
               reinterpret_cast<void*>(current_pc),
               reinterpret_cast<void*>(syscall_code)) < 0) {
        LOGE("[-] PTRACE_POKETEXT failed: tid=%d pc=0x%lx errno=%d, %s",
             tid,
             current_pc,
             errno,
             std::strerror(errno));
        detach_quietly(tid);
        return -1;
    }

    // 5. 修改寄存器：设置系统调用号及参数。
    set_regs_for_exit(&regs, exit_code, current_pc);

    if (ptrace(PTRACE_SETREGSET,
               tid,
               reinterpret_cast<void*>(NT_PRSTATUS),
               &iov) < 0) {
        LOGE("[-] PTRACE_SETREGSET failed: tid=%d errno=%d, %s", tid, errno, std::strerror(errno));
        // 尝试恢复原内存代码。
        ptrace(PTRACE_POKETEXT,
               tid,
               reinterpret_cast<void*>(current_pc),
               reinterpret_cast<void*>(original_code));
        detach_quietly(tid);
        return -1;
    }
    LOGI("[+] Injection prepared. Syscall opcode written at PC: 0x%lx", current_pc);

    // 6. ptrace continue，执行 svc #0。
    if (ptrace(PTRACE_CONT, tid, nullptr, nullptr) < 0) {
        LOGE("[-] PTRACE_CONT failed: tid=%d errno=%d, %s", tid, errno, std::strerror(errno));
        ptrace(PTRACE_POKETEXT,
               tid,
               reinterpret_cast<void*>(current_pc),
               reinterpret_cast<void*>(original_code));
        detach_quietly(tid);
        return -1;
    }
    LOGI("[+] Resumed thread %d to execute exit syscall...", tid);

    // 7. 等待该线程退出或被信号结束。
    if (!wait_for_thread_exit_or_signal(tid, &status)) {
        return -1;
    }

    LOGI("[+] Done. Thread %d should be dead now.", tid);
    return 0;
#endif
}