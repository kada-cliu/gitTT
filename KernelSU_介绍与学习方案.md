# KernelSU 介绍与学习方案

## 一、KernelSU 简介

**KernelSU** 是一个基于 Linux 内核的 Android Root 方案，由开发者 weishu 于 2022 年发布。与 Magisk 等传统 Root 方案不同，KernelSU 在内核层面实现 Root 权限管理，具有更强的系统级控制能力。

### 核心特点

| 特性 | 说明 |
|------|------|
| 内核级实现 | 直接集成到 Android 内核（GKI），不依赖 Zygisk/Xposed |
| UID 权限管理 | 通过内核态管理 UID，授权粒度更细 |
| 模块系统 | 支持类 Magisk 模块，可无缝叠加文件系统（OverlayFS）|
| 隐蔽性强 | 在内核层面运行，更难被应用检测 |
| 开源免费 | 项目托管于 GitHub，完全开源 |

### KernelSU vs Magisk

| 对比项 | KernelSU | Magisk |
|--------|----------|--------|
| 实现层 | 内核层 | 用户层（init） |
| 支持范围 | GKI 内核（Android 12+）/ 非 GKI 需自行编译 | 几乎所有 Android 设备 |
| Root 授权 | 基于 UID（内核态） | 基于进程（用户态） |
| 模块机制 | OverlayFS | Magic Mount |
| 检测难度 | 较难被检测 | 较容易被检测 |

---

## 二、KernelSU 工作原理

1. **内核补丁**：将 KernelSU 代码补丁应用到 Android 内核源码，或者通过 KernelSU 提供的 `kprobe` 方式动态注入（无需修改内核源码）。
2. **内核模块（LKM）**：部分设备支持以可加载内核模块（.ko）方式集成，无需重新编译整个内核。
3. **Root 授权流程**：应用请求 Root 权限 → KernelSU 内核模块拦截系统调用 → 比对 UID 白名单 → 授予或拒绝权限。
4. **模块系统**：在 `post-fs-data` 阶段挂载 OverlayFS，将模块文件无侵入地叠加到系统分区。

---

## 三、KernelSU 学习方案

### 阶段一：基础准备（1~2 周）

**目标**：掌握 Android 系统和 Linux 内核基础知识，为深入学习做准备。

- [ ] 了解 Android 系统架构（Bootloader → Kernel → Android Runtime → App）
- [ ] 学习 Linux 基础命令及权限体系（UID/GID/capabilities）
- [ ] 理解 Android 分区结构（boot、system、vendor 等）
- [ ] 了解 Android 启动流程（ABL → Bootloader → Kernel → init → Zygote）
- [ ] 学习 ADB 工具的基本使用

**推荐资源**：
- [Android 官方文档 - 架构概览](https://source.android.com/docs/core/architecture)
- 《Linux 内核设计与实现》（Robert Love）第 1~3 章

---

### 阶段二：KernelSU 使用入门（1 周）

**目标**：能够在设备上成功安装并使用 KernelSU。

- [ ] 阅读 [KernelSU 官方文档](https://kernelsu.org/zh_CN/)
- [ ] 了解 GKI（Generic Kernel Image）概念
- [ ] 在支持的设备上刷入带 KernelSU 的内核（或使用官方预编译版本）
- [ ] 安装 KernelSU Manager 应用，进行 Root 授权管理
- [ ] 安装并体验 KernelSU 模块（如 LSPosed for KernelSU）

**注意事项**：
- 操作前请备份重要数据并了解解锁 Bootloader 的风险
- 优先在已解锁 Bootloader 的测试设备上操作

---

### 阶段三：内核编译与集成（2~3 周）

**目标**：能够自己编译集成了 KernelSU 的 Android 内核。

- [ ] 搭建 Linux 编译环境（Ubuntu 20.04/22.04 推荐）
- [ ] 获取设备对应的内核源码（设备厂商开源或 AOSP GKI 源码）
- [ ] 学习内核配置（`make menuconfig` / `defconfig`）
- [ ] 按照 [KernelSU 集成文档](https://kernelsu.org/zh_CN/guide/how-to-integrate-for-non-gki.html) 打补丁或添加代码
- [ ] 编译内核并制作 boot.img
- [ ] 使用 `fastboot` 刷入测试

**关键命令示例**：
```bash
# 克隆内核源码（以 GKI 为例）
git clone https://android.googlesource.com/kernel/common -b android14-6.1

# 在内核源码根目录集成 KernelSU
curl -LSs "https://raw.githubusercontent.com/tiann/KernelSU/main/kernel/setup.sh" | bash -

# 配置并编译
make ARCH=arm64 gki_defconfig
make ARCH=arm64 -j$(nproc)
```

---

### 阶段四：KernelSU 源码分析（3~4 周）

**目标**：深入理解 KernelSU 的实现原理，能够阅读和分析核心源码。

- [ ] 克隆 [KernelSU 源码](https://github.com/tiann/KernelSU) 并阅读目录结构
- [ ] 理解 `kernel/` 目录：内核补丁核心逻辑
  - `core_hook.c`：系统调用拦截与 Root 授权
  - `sucompat.c`：`su` 命令兼容实现
  - `allowlist.c`：UID 白名单管理
- [ ] 理解 `manager/` 目录：Android 管理端 App 实现（Kotlin）
- [ ] 学习 Linux 内核模块开发基础（`init_module` / `cleanup_module`）
- [ ] 学习 `kprobe` / `ftrace` 机制（用于无源码集成方式）

**参考资料**：
- [KernelSU GitHub 仓库](https://github.com/tiann/KernelSU)
- [Linux Kernel Module Programming Guide](https://sysprog21.github.io/lkmpg/)
- 《深入理解 Linux 内核》第 3 版（Bovet & Cesati）

---

### 阶段五：模块开发（2~3 周）

**目标**：能够开发自己的 KernelSU 模块。

- [ ] 理解 KernelSU 模块目录结构（`META-INF/`、`system/`、`module.prop`、`customize.sh`）
- [ ] 参考现有模块（如 [Shamiko](https://github.com/LSPosed/LSPosed.github.io/releases)、[ZygiskNext](https://github.com/Dr-TSNG/ZygiskNext)）学习模块编写规范
- [ ] 编写一个简单模块（如修改 `build.prop`、替换系统文件）
- [ ] 调试模块（使用 `adb logcat`、`dmesg` 查看日志）
- [ ] 了解 KernelSU 的 WebUI 模块功能（使用 WebView 提供图形界面）

**模块结构示例**：
```
my_module/
├── META-INF/
│   └── com/google/android/
│       ├── update-binary      # 安装脚本
│       └── update-script      # 空文件
├── system/
│   └── ...                    # 要叠加的系统文件
├── module.prop                # 模块元信息
├── customize.sh               # 安装时执行的脚本
├── post-fs-data.sh            # 开机 post-fs-data 阶段执行
└── service.sh                 # 开机 late_start 阶段执行
```

---

### 阶段六：进阶与贡献（持续）

**目标**：参与社区，贡献代码或文档，解决实际问题。

- [ ] 关注 [KernelSU 的 GitHub Issues](https://github.com/tiann/KernelSU/issues) 了解社区动态
- [ ] 尝试为不支持的设备进行移植适配
- [ ] 研究与 SELinux、SafetyNet/Play Integrity 的交互
- [ ] 学习内核安全机制（LSM、seccomp、capabilities）
- [ ] 向 KernelSU 提交 Bug 报告、文档改进或代码 PR

---

## 四、KernelSU 在逆向工程与对抗加固领域的应用

KernelSU 由于在 **内核层** 运行，具备用户态方案无法比拟的能力，在 Android 逆向工程、对抗应用加固与安全研究领域有着非常广泛的应用。

---

### 4.1 内核级 Hook 与动态分析

与 Xposed/LSPosed 在 ART 层 Hook 不同，KernelSU 可通过内核模块或 eBPF 直接 Hook 系统调用，拦截面更底层、更难被反制。

| 技术 | 说明 |
|------|------|
| `kprobe` / `kretprobe` | 在任意内核函数入口/出口插入探针，监控 `openat`、`mmap`、`read` 等系统调用 |
| eBPF（扩展伯克利包过滤器）| 在内核中安全地运行沙箱程序，实时追踪进程行为、文件访问、网络流量 |
| 内核模块（LKM）Hook | 通过可加载内核模块替换系统调用表（`sys_call_table`），实现全局函数劫持 |

**典型场景**：
- 追踪目标 App 的所有文件 I/O、网络请求，无需反编译，直接观察运行时行为。
- 监控 `ptrace`、`process_vm_readv` 等调试相关系统调用，分析反调试逻辑。

---

### 4.2 绕过应用加固（对抗加固）

主流加固厂商（梆梆、爱加密、360加固、腾讯乐固等）的防护机制大多工作在用户态，KernelSU 可从更底层对其进行对抗：

#### 4.2.1 DEX/OAT 内存 Dump（脱壳）

加固 App 在运行时会将真实 DEX 解密并映射到内存，KernelSU 可在 `/proc/<pid>/maps` 级别直接读取目标进程内存，实现 **零感知脱壳**。

```
典型工具链：
1. KernelSU + frida-gadget（无守护进程注入）
2. KernelSU + DexDump / FART（内存中强制触发 ART 全量解释执行并 dump）
3. 自定义内核模块：在 mmap 分配 DEX 内存后立即将内存内容写到 /data/local/tmp
```

**优势**：由于读取操作发生在内核层，绕过了加固 SDK 在用户态部署的内存访问检测（如对 `process_vm_readv` 的 Hook 监控）。

#### 4.2.2 对抗反调试

加固 SDK 常见的反调试手段及 KernelSU 的对抗方式：

| 加固反调试手段 | KernelSU 对抗方式 |
|---------------|-----------------|
| 检测 `TracerPid`（`/proc/self/status`）| 内核模块修改 `/proc/<pid>/status` 的输出，将 `TracerPid` 强制返回 0 |
| 检测调试器端口（如 23946）| eBPF 过滤网络包，使检测失效 |
| `ptrace(PTRACE_TRACEME)` 自保护 | Hook `sys_ptrace`，对特定 UID 屏蔽自我 ptrace 检测 |
| 完整性校验（CRC/签名校验）| OverlayFS 替换 so/dex 文件前先通过模块修复校验值 |
| 检测 `/proc/self/maps` 中的 Frida/Xposed so | 内核模块过滤 `/proc/<pid>/maps` 输出，隐藏注入的 so 路径 |

---

### 4.3 Frida 与 KernelSU 协同使用

[Frida](https://frida.re/) 是最主流的动态插桩框架，配合 KernelSU 可以极大增强其能力：

#### 注入方式升级

| 方式 | 说明 |
|------|------|
| 传统 frida-server（用户态）| 需要 Root，且 frida-server 进程容易被检测 |
| KernelSU + frida-gadget 注入 so | 通过 OverlayFS 将 frida-gadget.so 替换/注入到目标 App 的 so 搜索路径，无 frida-server 进程 |
| KernelSU + zygisk-frida | 在 Zygote fork 阶段通过 ZygiskNext 注入，时机极早，可在 App 反调试初始化前完成 Hook |

#### 绕过 Frida 检测

加固 App 通常会扫描 `/proc/<pid>/maps`、D-Bus 端口、已知 Frida 特征字节等来检测 Frida。KernelSU 的对抗手段：

- **[Shamiko](https://github.com/LSPosed/LSPosed.github.io/releases)**：KernelSU 模块，通过 Zygisk 机制隐藏 Root 环境，使 `RootBeer`、`SafetyNet` 等检测失效。
- **自定义 `/proc` 过滤模块**：在内核模块中拦截对 `/proc/<pid>/maps` 的读取，过滤掉 Frida 相关的内存段。
- **端口隐藏**：通过 eBPF/netfilter Hook，使 Frida 监听端口对目标进程不可见。

---

### 4.4 SSL Pinning 绕过（内核级）

HTTPS 流量抓包时，App 的 SSL Pinning 会阻止中间人证书。KernelSU 提供了比用户态更彻底的绕过方案：

- **Hook `SSL_read` / `SSL_write`**（BoringSSL 函数）：通过内核模块或 Frida + KernelSU，在 TLS 握手完成后直接读取明文数据，无需关心证书校验。
- **`/system/etc/security/cacerts` OverlayFS 替换**：通过 KernelSU 模块将自定义 CA 证书注入系统信任证书列表，使 App 的证书验证自动通过（针对只依赖系统证书的 App）。
- **`conscrypt` Hook via LSPosed**：在支持 LSPosed 的 KernelSU 环境中，直接 Hook `javax.net.ssl.TrustManager`，使所有 SSL 验证返回通过。

**推荐工具**：[TrustMeAlready](https://github.com/ViRb3/TrustMeAlready)、[HTTPToolkit](https://httptoolkit.com/)、[apk-mitm](https://github.com/shroudedcode/apk-mitm)（配合 KernelSU 使用）

---

### 4.5 对抗 Root 检测与完整性校验

| 检测手段 | KernelSU 对抗方式 |
|---------|-----------------|
| 检测 `/system/bin/su` 等 su 路径 | KernelSU 不在文件系统创建 su，不留可见痕迹 |
| `RootBeer` / 各类 Root 检测库 | Shamiko + MagiskHide（兼容模式）隐藏 Root |
| Google Play Integrity（原 SafetyNet）| [PlayIntegrityFix](https://github.com/chiteroman/PlayIntegrityFix) 模块伪造设备完整性响应 |
| SELinux 状态检测 | KernelSU 模块可在内核层面保持 SELinux 显示为 `Enforcing`，同时实际放行特定规则 |
| 检测 Magisk Manager 包名 | KernelSU Manager 包名可随意修改（重命名 APK），规避包名黑名单 |
| 检测 `/proc/mounts` 中的 OverlayFS | 内核模块过滤 `/proc/mounts` 输出 |

---

### 4.6 典型实战工作流：对一款加固 App 进行逆向分析

```
1. 环境准备
   └─ 设备刷入 KernelSU 内核
   └─ 安装：ZygiskNext + LSPosed + Shamiko + PlayIntegrityFix

2. 绕过检测
   └─ Shamiko 隐藏 Root → 通过 Root 检测
   └─ PlayIntegrityFix → 通过 Play Integrity 校验

3. 脱壳（Unpacking）
   └─ 安装 FART / DexDump 模块（KernelSU 模块形式）
   └─ 启动目标 App → 自动 Dump 内存中解密后的 DEX
   └─ 使用 jadx / jd-gui 反编译 Dump 出的 DEX

4. 动态调试
   └─ 通过 ZygiskNext 注入 frida-gadget.so
   └─ 连接 Frida REPL，Hook 关键业务函数
   └─ 使用 objection 快速枚举类、方法、参数

5. 抓包分析
   └─ OverlayFS 注入自定义 CA 证书
   └─ Frida Hook SSL_read/SSL_write 获取明文流量
   └─ Charles / Burp Suite 进行流量分析

6. 修改与重打包（可选）
   └─ 通过 OverlayFS 模块替换目标 so/dex
   └─ 无需重新签名，直接生效
```

---

### 4.7 相关工具与模块推荐

| 工具 / 模块 | 用途 | 链接 |
|------------|------|------|
| **ZygiskNext** | 在 KernelSU 上提供 Zygisk 支持（注入 Frida、LSPosed 等）| [GitHub](https://github.com/Dr-TSNG/ZygiskNext) |
| **LSPosed** | 基于 Zygisk 的 Xposed 框架，支持 KernelSU | [GitHub](https://github.com/LSPosed/LSPosed) |
| **Shamiko** | 隐藏 Root / Zygisk 环境，绕过 Root 检测 | [Releases](https://github.com/LSPosed/LSPosed.github.io/releases) |
| **PlayIntegrityFix** | 绕过 Google Play Integrity 校验 | [GitHub](https://github.com/chiteroman/PlayIntegrityFix) |
| **Frida** | 动态插桩框架，Hook Java/Native 函数 | [frida.re](https://frida.re/) |
| **FART** | ART 层强制全量解释执行 + 内存 Dump DEX（脱壳）| [GitHub](https://github.com/hanbinglengyue/FART) |
| **DexDump** | Zygisk 模块，自动 Dump 内存中的 DEX | [GitHub](https://github.com/ryze312/dexdump-zygisk) |
| **jadx** | DEX / APK 静态反编译工具 | [GitHub](https://github.com/skylot/jadx) |
| **objection** | 基于 Frida 的 Runtime 探索工具，一键 SSL Unpinning | [GitHub](https://github.com/sensepost/objection) |
| **HTTPToolkit** | 配合系统证书替换模块进行 HTTPS 抓包 | [httptoolkit.com](https://httptoolkit.com/) |

---

> ⚠️ **免责声明**：上述技术仅供安全研究、漏洞挖掘、CTF 竞赛及合法的软件安全审计使用。对未经授权的软件进行逆向工程可能违反相关法律法规及最终用户许可协议（EULA），请在法律允许的范围内使用。

---

## 五、常用资源汇总

| 资源 | 链接 |
|------|------|
| KernelSU 官网 | https://kernelsu.org/zh_CN/ |
| KernelSU GitHub | https://github.com/tiann/KernelSU |
| KernelSU 官方文档 | https://kernelsu.org/zh_CN/guide/what-is-kernelsu.html |
| Telegram 讨论群 | https://t.me/KernelSU_group |
| XDA 论坛相关帖子 | https://xdaforums.com/（搜索 KernelSU） |
| Android 内核文档 | https://source.android.com/docs/core/architecture/kernel |
| Linux 内核文档 | https://www.kernel.org/doc/html/latest/ |

---

## 六、学习路线总结

```
基础知识（Linux + Android 架构）
        ↓
KernelSU 使用体验（安装 + 模块）
        ↓
内核编译与 KernelSU 集成
        ↓
KernelSU 源码阅读与分析
        ↓
KernelSU 模块开发
        ↓
逆向工程与对抗加固（Frida + 脱壳 + SSL Pinning 绕过）
        ↓
设备移植 / 社区贡献
```

> **提示**：学习过程中遇到问题，优先查阅官方文档和 GitHub Issues，其次在 Telegram 群或 XDA 论坛提问。建议全程使用 **测试机** 进行实验，避免损坏主力机数据。
