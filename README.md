# 基于DM8168的语音信号处理 (DM8168 Speech Signal Processing)

[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)
[![Platform](https://img.shields.io/badge/Platform-DM8168%20%7C%20OMAP--L138-orange.svg)]()
[![Language](https://img.shields.io/badge/Language-C-green.svg)]()

本项目是一个基于德州仪器（Texas Instruments, TI）异构多核平台（如 DM8168、OMAP-L138、KeyStone 或 AM5728 等）的**工业级实时音频处理系统**。

系统采用 **ARM (Linux) + DSP (SYS/BIOS RTOS)** 的经典双核协同架构，通过 TI **SysLink / IPC (Inter-Processor Communication)** 技术实现了物理共享内存的零拷贝音频流传输，并对标了专业演播室级的 DAT 音频采集与播放标准。

---

## 🌟 核心特性

*   **演播室级音质标准**：48000Hz 采样率、双声道 (Stereo)、16位小端 (S16_LE)，单物理周期时延为 20ms。
*   **零拷贝双核通信**：基于 TI IPC (Notify & SharedRegion) 实现 ARM 与 DSP 之间的物理共享内存传输。
*   **双向互锁流控队列**：Host (Linux ARM) 与 Server (SYS/BIOS DSP) 之间建立严格的绝对索引队列，彻底根除多核中断冲突导致的音频帧乱序。
*   **IPC 防死锁重试 (Anti-Deadlock IPC Retry)**：针对底层硬件 IPC 队列满载抛出的发送失败异常，在核心数据流（录制/播放）的握手节点实现了带有 CPU 调度出让（微秒/Tick 级休眠）的轻量级非阻塞重试机制，彻底终结因信号量指令漏发导致的系统全局死锁。
*   **防撕裂拼装机制 (Anti-tearing)**：针对 Linux ALSA 底层声卡驱动调度抖动，实现硬件分段数据的完整周期（960帧）强行拼装。
*   **预充水机制 (Pre-charging)**：强制设定 ALSA 播放缓冲阈值（启动前积攒至少 3 个周期/60ms 数据），杜绝冷启动爆音和 Underrun。
*   **优雅注销机制 (Graceful Shutdown)**：支持 ARM 与 DSP 之间的“四次挥手”双核闭环退出，安全释放信号量与底层 ALSA DMA 句柄，避免系统卡死。

---

## 🏗️ 系统架构

整个系统分为三大核心模块：

1.  **Host (ARM / Linux)**：负责系统生命周期管理、ALSA 硬件声卡的实时录制与播放驱动、维护物理共享内存的索引与互斥量。
2.  **DSP (SYS/BIOS RTOS)**：作为 Server 端运行，监听中断并执行音频算法核心逻辑（当前代码默认为直通拷贝 `memcpy`，可随时挂接滤波、降噪等 DSP 算法）。
3.  **Shared Region (共享内存)**：在跨核物理内存（SR1）中开辟 150KB 空间，划分录制与播放两个 20 块（400ms）的深水位环形缓冲池。

---

## 📂 目录结构

*   `host/` - ARM 端运行的 Linux 应用程序源码。包含 `main_host.c` (引导与管理) 和 `App.c` (音频流控与 ALSA 驱动)。
*   `dsp/` - DSP 端运行的 SYS/BIOS 固件源码。包含 `main_dsp.c` (OS引导)、`Server.c` (音频处理算法节点) 和 `Dsp.cfg` (系统与内存配置文件)。
*   `shared/` - 双核共享头文件，定义通信握手指令、音频参数与 IPC 宏。
*   `tests/` - 自动化测试脚本目录，包含 `test_case1_basic.sh` 等独立测试脚本。
*   `makefile` / `products.mak.example` - 项目构建脚本与开发环境配置模板。
*   `run.sh` - 目标板部署运行脚本。

---

## 🌍 开源规范与移植支持

本项目已进行了全面的开源规范化适配，代码注释已更新为 Doxygen 双语工程风格，支持灵活的跨平台移植：
*   **ALSA 环境解耦**：清除了所有硬编码库路径，新增 `ALSA_INSTALL_DIR` 环境变量，自适应各类 Linux 发行版与交叉编译 sysroot。
*   **DSP 平台动态化**：通过 `DSP_PLATFORM` 变量，轻松一键切换目标板芯片型号（如从 `ti.platforms.evmTI816X:dsp` 切换到 OMAP-L138）。
*   **标准开源协议**：采用标准的 BSD-3-Clause 许可证，便于商业与非商业场景自由引用。

---

## 🚀 编译与部署

### 1. 环境依赖
本项目依赖于 TI 的跨平台开发套件：
*   **SYS/BIOS** (TI-RTOS kernel)
*   **XDCtools**
*   **SysLink / IPC**
*   **CGT ARM** (GCC交叉编译器，如 `arm-none-linux-gnueabi`)
*   **CGT C6000** (DSP 编译器)

### 2. 编译步骤
1. 将 `products.mak.example` 复制为 `products.mak`，并根据本地机器上的实际安装路径进行修改。
2. 在项目根目录下直接运行 `make`。
3. 运行 `make install` 提取生成文件至 `install/` 目录。

### 3. 运行程序
将生成的 `install/` 目录拷贝至目标开发板（ARM Linux系统）。
1. 运行脚本加载并启动系统：
    ```bash
    ./run.sh
    ```
2. 控制台提示 `>>> System running perfectly. Press [ENTER] to exit smoothly <<<` 时，说明音频系统已在实时运转。
3. 按 `Enter` (回车) 键可触发系统的优雅注销流程。

---

## 🧪 自动化压力测试与底层波形抓取

本项目自带严密的防撕裂和防死锁机制测试脚本。支持 **ALSA Snoop 底层旁路抓包验证**，无需硬件音频分析仪或双路连接线即可做到无损验证：
1. 执行 `../tests/setup_alsa_snoop.sh on` 开启底层录音插件。
2. 用任意单根 3.5mm 音频线输入 1kHz 纯正弦波。
3. 运行测试脚本，在开发板当前目录下 `./dsp_playback.raw` 将生成 DSP 采播的原始数据流。导入 PC 的 Audacity 即可分析波形的绝对连续性。

在 `install/` 目录下（或者已挂载板卡的目录），直接运行您想测试的脚本：
```bash
../tests/test_case1_basic.sh
../tests/test_case2_stress.sh
../tests/test_case3_ipc_lifecycle.sh
```

此外，您可以参考以下文档了解详细的技术背景与验证方案：
*   [Audio Test Plan (`tests/audio_test_plan.md`)](tests/audio_test_plan.md)：完整的软硬件测试方案与验收标准。
*   [System Design Document (`docs/system_design.md`)](docs/system_design.md)：关于架构设计、IPC 流控、物理内存分配与抗撕裂机制的深度技术剖析文档。

该测试集包含：
1. **基础连通性**：5秒内的无丢帧收发。
2. **高负载抗撕裂测试**：榨干 ARM CPU 时的音频拼装稳定性。
3. **频繁启停抗死锁测试**：快速拉起并强制注销 10 次，验证 IPC 释放的安全闭环。