# 空间音效集成与构建运行指南

本文档说明如何把空间音效处理（HRIR 卷积）集成进 DSP 固件 `Server.c`，并在 Linux 虚拟机里交叉编译、部署到开发板运行。

## 1. 概述

把第二阶段的「直通拷贝 `memcpy`」替换为「空间音效处理」：DSP 每收到一块交织立体声音频（960 帧），先左右声道分离，再分别与左/右 HRIR 做流式 FIR 卷积，最后合并写回。卷积采用 Q15 定点：Q15 × Q15 → Q30，64 位累加，右移 15 位饱和输出。

## 2. 涉及文件

| 文件 | 位置 | 说明 |
|------|------|------|
| `Hrir.h` | `dsp/` | HRIR 系数（L60e250a / R60e250a，Q15，各 512 点） |
| `Server.c` | `dsp/` | 修改后的 DSP 固件（替换 `memcpy` 为 `process_spatial`） |
| `makefile` | `dsp/` | 增加 `Server.oe674` 对 `Hrir.h` 的依赖（可选） |

## 3. 集成步骤

1. 把 `Hrir.h` 复制到 `dsp/`。
2. 用新的 `Server.c` 覆盖 `dsp/Server.c`。
3.（可选）用新的 `makefile` 覆盖 `dsp/makefile`，使修改 HRIR 系数后 `make` 能自动重编译。

## 4. 构建前准备：配置 products.mak

在项目根目录编辑 `products.mak`，把各工具链路径指向本机 TI SDK 实际安装目录：

```make
DEPOT                  = /path/to/ti-ezsdk_dm816x-evm_5_05_01_04
ALSA_INSTALL_DIR       = $(DEPOT)/linux-devkit/arm-none-linux-gnueabi/usr
DSP_PLATFORM           = ti.platforms.evmTI816X:dsp
BIOS_INSTALL_DIR       = $(DEPOT)/component-sources/...
CGT_ARM_PREFIX         = $(DEPOT)/linux-devkit/bin/arm-none-linux-gnueabi-
IPC_INSTALL_DIR        = $(DEPOT)/component-sources/ipc_...
SYSLINK_INSTALL_DIR    = $(DEPOT)/component-sources/syslink_...
CGT_C674_ELF_INSTALL_DIR = $(DEPOT)/.../ti-cgt-c6000_...
XDC_INSTALL_DIR        = $(DEPOT)/xdctools_...
```

路径以你自己的虚拟机里 TI EZSDK 的实际位置为准。

## 5. 构建

在项目根目录执行：

```bash
make clean
make            # 同时构建 host 端 app 与 dsp 端固件
make install    # 生成 install/ 目录
```

构建产物：

- `host/bin/{debug,release}/app_host`
- `dsp/bin/{debug,release}/server_dsp.xe674`

## 6. 部署与运行

把 `install/` 整个目录拷贝到开发板（ARM Linux），进入对应子目录执行：

```bash
./run.sh
```

`run.sh` 的流程是：`slaveloader` 加载 DSP 固件 `server_dsp.xe674` → 运行 `app_host DSP` → 结束后关闭 DSP。控制台出现 `System running perfectly` 即表示实时音频流已跑通，按回车触发优雅退出。

## 7. 验证

- 输入：3.5 mm Line In 接入 1 kHz 正弦波（或播放带方位感的音频）。
- 输出：耳机里应能听到被叠加了空间方位信息的立体声。
- 数值验证：本集成在 PC 上用 gcc 独立测试过，流式 FIR（带历史）与整段卷积结果逐点 0 LSB 一致。

## 8. 关键设计

- **流式 FIR + 历史缓冲**：每块处理完，把当前块末尾 511 个样本存入 `hist_L`/`hist_R`，作为下一块卷积的「历史」（即整文件 overlap-add 里那段「重叠」在实时流式下的等价形式）。
- **Q15 定点**：`Q15 × Q15 = Q30`，`long long` 累加防溢出，`>> 15` 还原，`sat16` 饱和截断。
- **系数自然序、不翻转**：`fir_stream` 用标准延迟线形式 `acc += h[k] * x[j-k]`，与例程 `fir_conv_q15` 一致。
- **静态缓冲**：状态与工作缓冲都用 `static`，避免占用 SYS/BIOS Task 的 4 KB 栈。

## 9. 注意事项

- **削波**：HRIR 增益 `Σ|h|≈7.7`（左）/ 3.9（右）大于 1，满幅输入时卷积输出会饱和削波。当前用 `sat16` 截断；如需避免削波，可在 `sat16` 中加固定衰减（如 `acc >> 3`，约 ÷8）。
- **块长**：使用框架的 `PERIOD_FRAMES = 960`（20 ms），不是例程的 1024。
- **换方位**：把 `Hrir.h` 里的系数换成其它方位（如 40e045）即可改变声源方位；要做「跑步动态」需在运行时按时间切换两套系数并平滑过渡。
