# 音频测试计划

## 1. 测试目标

验证双核音频系统的连通性、抗撕裂与稳定性。

## 2. 测试环境

- 硬件：DM8168 开发板、3.5 mm 音频线、PC 音源（在线信号发生器）与 Audacity。
- 软件：`app`、`server.xe674`、alsa-utils。
- 音频参数：48 kHz / 16 位 / 双声道。

## 3. 旁路抓包（可选）

在开发板上执行 `setup_alsa_snoop.sh on` 开启 ALSA 旁路抓包。录制完成后，开发板当前目录会生成 `dsp_playback.raw`，将其导入 PC 端 Audacity 即可分析波形连续性。测试完成后执行 `setup_alsa_snoop.sh off` 恢复声卡默认配置。

## 4. 测试用例

### 用例 1：基础直通

- 步骤：开启抓包 → 运行 `./run.sh` → 输入 1 kHz 正弦波 → 停止。
- 验收标准：无 Underrun/Overrun 提示；Audacity 中波形连续、无丢帧断点。

### 用例 2：CPU 满载抗撕裂

- 步骤：运行压力脚本使 CPU 满载 → 录制 → Audacity 分析。
- 验收标准：波形连续，无错位撕裂，无相位突变。

### 用例 3：频繁启停防死锁

- 步骤：自动启停系统 10 次。
- 验收标准：每次正常退出，无死锁、无资源泄漏、无 IPC 占用。

### 用例 4：冷启动预充水

- 步骤：启动系统后立即录制首秒音频。
- 验收标准：无初始爆音，波形从静音平滑过渡。

## 5. 测试脚本

- `test_case1_basic.sh`
- `test_case2_stress.sh`
- `test_case3_ipc_lifecycle.sh`
- `setup_alsa_snoop.sh`
