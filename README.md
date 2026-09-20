# 基于 DM8168 的异构多核语音信号处理

基于 TI DM8168（ARM Cortex-A8 + DSP C674x）的双核音频处理系统。ARM 运行 Linux，负责调度与 I/O；DSP 运行 SYS/BIOS，负责实时算法。两核之间通过 SysLink IPC（Notify + SharedRegion）实现共享内存零拷贝的实时音频流传输。

## 数据流

```
采集(ADC) → ARM 录制线程 → 共享内存 TX 区 → DSP 处理 → 共享内存 RX 区 → ARM 播放线程 → 播放(DAC)
```

## 系统架构

```
外部音频编解码器 (ADC/DAC)
        │  I2S / DMA
        ▼
ARM Cortex-A8 (Linux)
  main_host.c / App.c
  录制线程 · 播放线程
        │  SysLink (Notify + SharedRegion)
        ▼
共享内存 SR1 (153600 字节 / 150 KiB)
  TX 录制区 · RX 播放区
        ▲
        │  SysLink (Notify + SharedRegion)
DSP C674x (SYS/BIOS)
  main_dsp.c / Server.c
  音频处理任务 (当前为 memcpy 直通)
```

## 功能特性

- 音频规格：48 kHz、双声道、16 位小端（S16_LE），单块周期 20 ms（960 帧）。
- 零拷贝通信：音频数据仅在共享内存中传递，核间不做逻辑复制。
- 双缓冲环形队列：录制与播放各 20 块，缓冲深度共 400 ms，用于吸收 Linux 调度抖动。
- 抗撕裂拼装：对 ALSA 分段返回的残缺数据按 960 帧强制拼装，避免音频帧错位。
- 播放预充水：播放启动阈值设为 3 个周期（60 ms），避免冷启动爆音与欠载。
- 优雅退出：ARM 与 DSP 通过四次握手退出，安全释放信号量与 ALSA 句柄。

## 目录结构

- `host/`：ARM 端应用源码，含 `main_host.c`、`App.c`。
- `dsp/`：DSP 端固件源码，含 `main_dsp.c`、`Server.c`、`Dsp.cfg`。
- `shared/`：双核共享头文件，含 `AppCommon.h`、`SystemCfg.h`。
- `tests/`：自动化测试脚本与测试计划。
- `docs/`：需求、设计、测试报告文档。

## 环境依赖

- SYS/BIOS（TI-RTOS kernel）
- XDCtools
- SysLink / IPC
- CGT ARM（交叉编译器，如 `arm-none-linux-gnueabi`）
- CGT C6000（DSP 编译器）

## 编译与运行

1. 将 `products.mak.example` 复制为 `products.mak`，按实际安装路径修改。
2. 在项目根目录执行 `make` 构建，再执行 `make install` 生成 `install/`。
3. 将 `install/` 拷贝到开发板（ARM Linux），执行 `./run.sh` 启动系统。
4. 控制台出现运行提示后，按回车键触发优雅退出。

## 测试

测试脚本位于 `tests/`：

- `test_case1_basic.sh`：基础连通性测试。
- `test_case2_stress.sh`：CPU 高负载抗撕裂测试。
- `test_case3_ipc_lifecycle.sh`：频繁启停防死锁测试。
- `setup_alsa_snoop.sh`：ALSA 旁路抓包。

## 文档

- [需求规格说明](docs/requirements.md)
- [系统设计说明](docs/system_design.md)
- [软件测试报告](docs/software_test_report.md)
- [音频测试计划](tests/audio_test_plan.md)

## 许可证

BSD-3-Clause。
