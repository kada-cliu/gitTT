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

## 四、常用资源汇总

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

## 五、学习路线总结

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
设备移植 / 社区贡献
```

> **提示**：学习过程中遇到问题，优先查阅官方文档和 GitHub Issues，其次在 Telegram 群或 XDA 论坛提问。建议全程使用 **测试机** 进行实验，避免损坏主力机数据。
