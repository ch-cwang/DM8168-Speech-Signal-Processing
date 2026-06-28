# 基于 DM8168 音频信号处理系统测试方案

## 一、 测试概述

本方案旨在对 DM8168 异构多核平台上的音频处理系统进行全面的功能性、鲁棒性与稳定性验证。重点测试双核间（ARM + DSP）共享内存数据流转的准确性，以及系统在恶劣操作系统调度环境下的抗抖动能力（防撕裂）与安全注销机制。

## 二、 测试环境准备

### 1. 硬件准备

* **DM8168 / OMAP-L138 开发板**：正常运行 Linux 及装载 SysLink/IPC 驱动。
* **外部纯音音源 (单台 PC/手机即可)**：打开浏览器 [Online Tone Generator](https://www.szynalski.com/tone-generator/)。
* **音频波形分析端 (PC)**：安装开源音频编辑软件 [Audacity](https://www.audacityteam.org/) 的电脑（用于事后分析文件）。
* **音频线**：仅需一根 3.5mm 公对公 Aux 对录线（连接 **音源耳机孔** 到开发板的 **Line In**）。

### 2. 软件准备

* 已编译完成的 `app` 与 `server.xe674` (DSP 固件)。
* 音频测试辅助工具：ALSA 工具集 (`alsa-utils`，包含 `arecord`、`aplay`、`speaker-test`)。
* 将测试脚本 `run_tests.sh` 拷贝至部署目录 `install/` 下。

---

## 三、 测试用例设计

### [用例 1] 基础音频透传与音质测试 (Basic Passthrough & Quality)

**目的**：验证 ALSA 的 48kHz, 16bit, Stereo 硬件配置是否成功，以及 DSP 的内存直通拷贝（`memcpy`）是否按预期工作。
**步骤**：

1. 使用 3.5mm 音频线将 **播放源 (手机/PC)** 连接至开发板的 **Line In**。
2. 运行脚本开启旁路抓包：`./tests/setup_alsa_snoop.sh on`
3. 运行系统的 `./run.sh` 启动 DSP 和 ARM 线程。
4. 在播放源设备上持续播放 1kHz 的标准正弦波，维持 10 秒后停止 `./run.sh`。
5. 将开发板当前目录下生成的 `./dsp_playback.raw` 文件拷贝至 PC，用 Audacity 选择“导入 -> 原始数据 (Raw Data)” (格式：16-bit PCM, Little-endian, 双声道, 48000Hz)。使用鼠标滚轮将波形**放大至毫秒级别**。
   **验收标准**：
- [x] 系统无任何报错，无 Underrun/Overrun 提示。
- [x] 听感上正弦波纯净，无“哒哒”声或“噼啪”爆音杂音。
- [x] 在 Audacity 放大波形观察，1kHz 正弦波曲线极其平滑连续，**无垂直断崖（无断层）**，**中间无空白丢帧断点**。

> [!TIP]
> 这一步证明了“数据高架桥”已经完全打通，物理内存共享与 IPC Notify 的握手通讯正常。

### [用例 2] CPU 极端高负载抗撕裂测试 (Stress & Anti-tearing)

**目的**：验证代码中精心设计的 ALSA 分段组装逻辑（`frames_left` 机制），确保即使 Linux 系统调度产生长达数十毫秒的卡顿，依然不会将残缺的缓冲帧发给 DSP 引发音频错乱。
**步骤**：

1. 确保已开启旁路抓包 (`setup_alsa_snoop.sh on`)，开启外部正弦波播放，并启动 `./run.sh` 使音频流运转。
2. 运行高负载测试脚本，例如执行 `dd if=/dev/urandom of=/dev/null` 生成 2-4 个吃满 CPU 资源的后台进程。
3. 录制 10 秒后停止，在 Audacity 中放大观察这 10 秒内的高压输出波形。
   **验收标准**：
- [x] CPU 占用率达 99% 甚至 100%。
- [x] Audacity 中导入的旁路录制波形依然连续流畅，**坚决不能出现相位突变的垂直撕裂声或明显的音调突变**（偶尔的极短暂由于硬件 DMA 耗尽造成的停顿空白是允许的，但数据帧决不能错位）。
4. 测试完毕后，可执行 `./tests/setup_alsa_snoop.sh off` 恢复系统默认声卡配置。

### [用例 3] 频繁启停与防僵尸进程测试 (IPC Deadlock & Shutdown)

**目的**：验证代码中的“四次挥手”注销流程，以及 IPC 邮箱写满时的微秒级出让防死锁机制。
**步骤**：

1. 运行测试脚本 `test_case3_ipc_lifecycle.sh` 启动自动化启停模块。
2. 脚本将快速连续地启动 `./run.sh`，等待 1 秒后输入 `Enter` 回车键退出。重复 10 次。
   **验收标准**：
- [x] 每次都能打印出 `System running perfectly...`。
- [x] 每次退出后进程干净销毁，系统控制台不会卡死。
- [x] 再次启动时不会报 IPC 端口被占用或共享内存申请失败（证明 DSP 资源也成功回收）。

> [!CAUTION]
> 如果在此测试中控制台无响应，说明 DSP 的主循环由于某种原因未收到 Shutdown 信号，或者底层 ALSA 接口挂死，这就说明系统的“优雅注销机制”存在缺陷。

### [用例 4] 冷启动预充水机制验证 (Pre-charging Threshold)

**目的**：验证 ALSA 的 `start_threshold` 是否起到了防止第一声爆音的作用。
**步骤**：

1. 保持输入端 (Line In) 持续输入稳定的 1kHz 正弦波信号，并执行 `./tests/setup_alsa_snoop.sh on` 开启底层抓包。
2. 启动 `./run.sh`，运行 2-3 秒后立即终止。
3. 将当前目录下生成的 `./dsp_playback.raw` 导入 PC 端 Audacity 放大观察启动瞬间的波形（或者直接用耳朵戴耳机听）。
   **验收标准**：
- [x] 启动时的波形连续，或者听感上只有平滑的“哔”声，无由于底层 DMA 饥饿导致的极其尖锐刺耳的初始“叭！”声数字爆音（注意：前 100ms 的 ADC 模拟电路上电极小起伏属于正常硬件现象，不属于数字爆音）。

---

## 四、 自动化测试脚本 (`tests/test_case*.sh`)

为方便在开发板上实施测试，我已在工程目录的 `tests/` 下为您编写了分立的测试脚本。

* `test_case1_basic.sh`：执行基础音频连通性验证。
* `test_case2_stress.sh`：在极限 CPU 负载下测试抗撕裂性。
* `test_case3_ipc_lifecycle.sh`：执行快速启停防死锁和防僵尸进程压力测试。

您可以按需在开发板上分别运行它们，无需人工干预即可自动化完成测试。
