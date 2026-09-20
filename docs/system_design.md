# 系统设计说明（SDD）

## 1. 架构概述

系统采用非对称多核（AMP）架构。ARM Cortex-A8 运行 Linux，负责系统调度、ALSA 驱动与生命周期管理；DSP C674x 运行 SYS/BIOS，负责实时音频处理。两核通过 SysLink IPC（Notify + SharedRegion）通信。

```
外部音频编解码器 (ADC/DAC)
        │  I2S / DMA
        ▼
ARM Cortex-A8 (Linux)
  main_host.c / App.c
  录制线程 · 播放线程
        │  Notify / SharedRegion
        ▼
共享内存 SR1 (153600 字节)
  TX 录制区 (76800 字节) · RX 播放区 (76800 字节)
        ▲
        │  Notify / SharedRegion
DSP C674x (SYS/BIOS)
  main_dsp.c / Server.c
  音频处理任务
```

## 2. 模块设计

### 2.1 Host（ARM / Linux）

- `main_host.c`：SysLink 初始化、加载并启动 DSP、生命周期管理。
- `App.c`：ALSA 参数配置、录制/播放线程、共享内存索引与信号量流控。

### 2.2 DSP（SYS/BIOS）

- `main_dsp.c`：BIOS 启动、IPC 启动与核间连接。
- `Server.c`：音频处理主循环，监听中断、取数、处理、写回。
- `Dsp.cfg`：配置 SharedRegion、多核名称、缓存与内存映射。

### 2.3 共享内存

在 SharedRegion SR1 中申请 153600 字节，分为 TX 录制区（76800 字节）与 RX 播放区（76800 字节）。

## 3. 内存布局与环形缓冲

| 参数 | 值 |
|------|----|
| 采样率 | 48000 Hz |
| 声道 | 2（Stereo） |
| 位深 | 16 bit（S16_LE） |
| 每帧字节数 | 4 |
| 周期帧数 `PERIOD_FRAMES` | 960（20 ms） |
| 块大小 `BLOCK_SIZE` | 3840 字节 |
| 块数 `BLOCK_COUNT` | 20 |
| 单区大小 `HALF_BUFFER_SIZE` | 76800 字节 |
| 总大小 `FULL_BUFFER_SIZE` | 153600 字节（150 KiB） |

TX 与 RX 各含 20 个 3840 字节的块，通过绝对索引（0～19）在核间传递所有权。

## 4. IPC 协议

Notify 载荷为 32 位：高 16 位为命令，低 16 位为块索引。

### 4.1 数据流命令

| 命令 | 值 | 含义 |
|------|----|------|
| CMD_APP_TO_SERVER_DATA_READY | 0x0001 | ARM 采集完成，交 DSP 处理 |
| CMD_APP_TO_SERVER_PLAY_DONE | 0x0002 | ARM 播放完成，块可复用 |
| CMD_SERVER_TO_APP_DATA_READY | 0x0003 | DSP 处理完成，通知 ARM 播放 |
| CMD_SERVER_TO_APP_RECORD_DONE | 0x0004 | DSP 消费完录制块，归还 ARM |

### 4.2 控制命令

| 命令 | 值 | 含义 |
|------|----|------|
| APP_CMD_NOP | 0x00000000 | 启动握手（心跳） |
| APP_SPTR_LADDR | 0x10000000 | 共享内存指针低 16 位 |
| APP_SPTR_HADDR | 0x20000000 | 共享内存指针高 16 位 |
| APP_SPTR_ADDR_ACK | 0x30000000 | 指针握手确认 |
| APP_CMD_SHUTDOWN | 0x40000000 | 请求退出 |
| APP_CMD_SHUTDOWN_ACK | 0x50000000 | 退出确认 |

共享内存指针通过两次 16 位传输（`APP_SPTR_LADDR` / `APP_SPTR_HADDR`）完成握手。

## 5. 数据流（20 ms 周期）

1. ARM 录制线程读满 960 帧，写入 TX 区索引 N，发送 `DATA_READY(N)`。
2. DSP 收到中断，从 TX 区读取块 N，执行处理（当前为 `memcpy`），结果写入 RX 区索引 N，发送 `RECORD_DONE(N)` 与 `DATA_READY(N)`。
3. ARM 播放线程收到 `DATA_READY(N)`，从 RX 区读取块 N，写入 DAC，发送 `PLAY_DONE(N)`。
4. DSP 收到 `PLAY_DONE(N)`，归还块 N 供下一轮使用。

## 6. 线程与流控

ARM 端包含三个线程：

- Main 线程：初始化与退出控制。
- Record 线程：生产者，受 `empty_in` 信号量约束（初始值 20）。
- Play 线程：消费者，受 `full_out` 信号量约束（初始值 0）。

DSP 端包含一个音频处理任务，受 `full_in`（初始值 0）与 `empty_out`（初始值 20）约束。

两组信号量保持守恒：`empty_in + full_in = 20`，`empty_out + full_out = 20`。

## 7. ALSA 流控与抗撕裂

- 播放启动阈值 `start_threshold = 3 × 960`，`avail_min = 960`。
- 读写采用偏移量（offset）拼装循环，残缺数据强制拼满 960 帧后才发送。
- 出错时调用 `snd_pcm_recover` 恢复底层指针。

## 8. 启动与退出

### 8.1 启动

1. SysLink 加载并启动 DSP 固件。
2. 双核互发 `APP_CMD_NOP` 完成握手。
3. ARM 分配共享内存，分两次发送 32 位指针（LADDR/HADDR），DSP 逐次确认。
4. ARM 启动录制线程与播放线程，进入 20 ms 数据循环。

### 8.2 退出

1. ARM 调用 `snd_pcm_drop` 打断阻塞中的 ALSA 读写。
2. ARM 调用 `sem_post` 唤醒录制/播放线程，线程退出循环。
3. ARM 发送 `APP_CMD_SHUTDOWN`，DSP 收到后退出处理循环并回复 `APP_CMD_SHUTDOWN_ACK`。
4. ARM 注销 Notify 回调，释放共享内存与信号量。
