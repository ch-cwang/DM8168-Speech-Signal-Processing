
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include <ti/syslink/Std.h>
#include <ti/ipc/Notify.h>
#include <ti/ipc/SharedRegion.h>
#include <ti/syslink/utils/Memory.h>

#include "../shared/AppCommon.h"
#include "../shared/SystemCfg.h"
#include "App.h"

#define QUEUESIZE 8

/* 控制命令队列 (接收 DSP 发来的 ACK) */
typedef struct {
  UInt32 queue[QUEUESIZE];
  UInt head;
  UInt tail;
  UInt32 error;
  sem_t semH;
} Event_Queue;

/* Host 核心全局上下文结构体 */
typedef struct {
  Event_Queue eventQueue;
  UInt16 remoteProcId;
  UInt16 lineId;
  UInt32 eventId;
  Char *bufferPtr;

  /* POSIX 信号量，用于线程间的流控同步 */
  sem_t empty_in;
  sem_t full_out;

  /* 播放数据的绝对索引队列，跨线程传输必须依赖它防乱序 */
  UInt16 play_idx_queue[INDEX_Q_SIZE];
  UInt play_q_head;
  UInt play_q_tail;
} App_Module;

static App_Module Module;
static volatile int g_running = 1;



/* 全局化 ALSA
 * 文件句柄，是为了在主线程要求退出时，能够强行中断底层硬件的读写阻塞 */
static snd_pcm_t *handle_cap = NULL;
static snd_pcm_t *handle_play = NULL;

static UInt32 App_waitForEvent(Event_Queue *eventQueue);
static Void App_notifyCB(UInt16 procId, UInt16 lineId, UInt32 eventId, UArg arg,
                         UInt32 payload);
void *thread_record(void *arg);
void *thread_play(void *arg);

/* ========================================================================== */
/* 工业级 ALSA 硬件与软件参数配置函数 */
/* 参数 handle: 指向声卡句柄指针的指针，配置成功后通过它把声卡操作权交回给调用者
 */
/* 参数 stream: 枚举类型，表明当前是要求配置录音 (CAPTURE) 还是播放 (PLAYBACK)
 */
/* ========================================================================== */
int setup_alsa(snd_pcm_t **handle, snd_pcm_stream_t stream) { //[cite: 2]
  snd_pcm_hw_params_t
      *params; // 声明硬件参数结构体指针，用于配置采样率、位深、物理通道等[cite:
               // 2]
  snd_pcm_sw_params_t *
      swparams; // 声明软件参数结构体指针，用于配置内核层面的触发阈值、水位线等流控策略[cite:
                // 2]

  /* 核心音质定义 */
  unsigned int val =
      48000;   // 强制采用 48000Hz (演播室/DAT级) 采样率。避免因低端声卡不支持
               // 8000Hz 导致的内核强行重采样破音[cite: 2]
  int dir = 0; // 存放配置时的方向指示 (0 表示精确匹配，无上下浮动)[cite: 2]

  /* 核心流控参数定义 */
  snd_pcm_uframes_t frames =
      PERIOD_FRAMES; // 设定“中断周期(Period)”：声卡每处理完 960 帧 (20ms)
                     // 触发一次中断，通知 ARM 搬数据[cite: 2]

  /* 将硬件底层的 DMA 环形缓冲总池子放大到 6 个周期 (即 6 * 20ms = 120ms)。
     这意味着即使 Linux 操作系统因为调度其他任务卡顿了长达 100 毫秒，
     底层的声卡芯片依然有数据可播、有空间可录，绝对不会发生 Underrun/Overrun
     报错！*/
  snd_pcm_uframes_t buffer_size = frames * 6; //[cite: 2]

  /* 尝试打开默认的声卡设备 ("default")。若设备被独占或不存在则返回 -1 销毁线程
   */
  if (snd_pcm_open(handle, "default", stream, 0) < 0)
    return -1; //[cite: 2]

  /* --------------------------------------------------------------------------
   */
  /* 第一阶段：配置物理声卡的硬件参数 (Hardware Parameters) */
  /* --------------------------------------------------------------------------
   */
  snd_pcm_hw_params_malloc(
      &params); // 在堆内存中为硬件参数结构体分配空间[cite: 2]
  snd_pcm_hw_params_any(*handle,
                        params); // 将该声卡支持的所有全量默认配置选项填入
                                 // params 中，作为修改的基础[cite: 2]

  /* 设定数据交错模式。INTERLEAVED 意味着双声道数据在内存中是左右左右交错存放的
   * (L R L R L R) */
  snd_pcm_hw_params_set_access(*handle, params,
                               SND_PCM_ACCESS_RW_INTERLEAVED); //[cite: 2]

  /* 设定量化位深格式：Signed 16-bit Little Endian
   * (16位有符号小端序)。这是最经典的 CD 级位深格式 */
  snd_pcm_hw_params_set_format(*handle, params,
                               SND_PCM_FORMAT_S16_LE); //[cite: 2]

  /* 设定物理声道数：2 (双声道立体声) */
  snd_pcm_hw_params_set_channels(*handle, params, 2); //[cite: 2]

  /* 设定采样率 (将前面定义的 48000 注入)。使用 _near 是因为如果声卡死活不支持
   * 48k，它会选一个最接近的以防直接崩溃 */
  snd_pcm_hw_params_set_rate_near(*handle, params, &val, &dir); //[cite: 2]

  /* 设定中断周期大小 (960帧)，控制声卡打断 CPU 的频率 */
  snd_pcm_hw_params_set_period_size_near(*handle, params, &frames,
                                         &dir); //[cite: 2]

  /* 设定底层 DMA 环形缓冲区的总容量 (5760帧/120ms) */
  snd_pcm_hw_params_set_buffer_size_near(*handle, params,
                                         &buffer_size); //[cite: 2]

  /* 所有的硬件需求打包完毕，正式下发给 Linux
   * 内核与底层硬件执行。若配置存在冲突则返回负数 */
  if (snd_pcm_hw_params(*handle, params) < 0)
    return -1; //[cite: 2]
  snd_pcm_hw_params_free(
      params); // 配置生效，释放参数结构体占用的堆内存[cite: 2]

  /* --------------------------------------------------------------------------
   */
  /* 第二阶段：配置 ALSA 内核流控的软件参数 (Software Parameters) -
   * 预充水机制防线      */
  /* --------------------------------------------------------------------------
   */
  snd_pcm_sw_params_malloc(&swparams); // 为软件流控参数分配内存[cite: 2]
  snd_pcm_sw_params_current(
      *handle, swparams); // 获取当前声卡的默认软件控制策略[cite: 2]

  /* 预充水机制只需针对“播放端”进行干预 */
  if (stream == SND_PCM_STREAM_PLAYBACK) { //[cite: 2]
    /* 【极其关键的防撕裂机制：Start Threshold】
       如果不设置这行，默认策略是“给一帧数据就立刻开喇叭播一帧”。在冷启动瞬间，
       由于第一块数据刚到，第二块还在 DSP
       里算，声卡会瞬间把第一块播完然后立刻发生 Underrun 报错！
       设置这行，等于警告 ALSA 内核引擎：
       “在我往缓冲区里填满 3 个周期 (3 * 960 = 2880帧) 的数据之前，底层的 DMA
       绝对不允许启动喇叭！”
       这样就能强制积攒出一个巨大的抗抖动水库，彻底扼杀冷启动破音。 */
    snd_pcm_sw_params_set_start_threshold(*handle, swparams,
                                          frames * 3); //[cite: 2]

    /* 设定最小唤醒水位线：只要硬件缓冲区里腾出哪怕 1 个周期 (960帧)
       的空闲空间， ALSA 就应该立刻唤醒被阻塞的 writei
       线程去填补数据。保证填水足够积极。 */
    snd_pcm_sw_params_set_avail_min(*handle, swparams, frames); //[cite: 2]
  }

  /* 软件策略打包完毕，下发给 ALSA 内核使其生效 */
  snd_pcm_sw_params(*handle, swparams); //[cite: 2]
  snd_pcm_sw_params_free(swparams);     // 释放资源[cite: 2]

  return 0; // 配置大功告成，成功返回[cite: 2]
}

Int App_create(UInt16 remoteProcId) {
  Int status;
  IHeap_Handle heap;

  memset(&Module, 0, sizeof(App_Module));
  Module.lineId = SystemCfg_LineId;
  Module.eventId = SystemCfg_EventId;
  Module.remoteProcId = remoteProcId;

  sem_init(&Module.eventQueue.semH, 0, 0);

  /* 录音端作为系统的源头，初始拥有全部 (BLOCK_COUNT) 的录音额度 */
  sem_init(&Module.empty_in, 0, BLOCK_COUNT);

  /* 播放端作为系统的末端，初始必须被锁死(0)，等待 DSP 处理完数据来唤醒 */
  sem_init(&Module.full_out, 0, 0);

  Notify_registerEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                       App_notifyCB, (UArg)&Module);

  do {
    status = Notify_sendEvent(Module.remoteProcId, Module.lineId,
                              Module.eventId, APP_CMD_NOP, TRUE);
    if (status == Notify_E_EVTNOTREGISTERED)
      usleep(100);
  } while (status == Notify_E_EVTNOTREGISTERED);

  /* 从多核共享空间中申请大块物理连续内存 */
  heap = (IHeap_Handle)SharedRegion_getHeap(SHARED_REGION_1);
  Module.bufferPtr = (Char *)Memory_calloc(heap, FULL_BUFFER_SIZE, 0, NULL);

  return 0;
}

Int App_exec() {
  SharedRegion_SRPtr sharedBufferPtr = 0;
  pthread_t tid_record, tid_play;

  /* 握手：分两次将共享内存的高低16位指针传递给 DSP */
  sharedBufferPtr = SharedRegion_getSRPtr(Module.bufferPtr, SHARED_REGION_1);
  Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                   APP_SPTR_LADDR | (sharedBufferPtr & 0xFFFF), TRUE);
  App_waitForEvent(&Module.eventQueue);

  Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                   APP_SPTR_HADDR | ((sharedBufferPtr >> 16) & 0xFFFF), TRUE);
  App_waitForEvent(&Module.eventQueue);

  /* 启动双路数据流线程 */
  pthread_create(&tid_record, NULL, thread_record, NULL);
  pthread_create(&tid_play, NULL, thread_play, NULL);

  /* 阻塞主线程，保持程序运行 */
  printf(
      "\n>>> System running perfectly. Press [ENTER] to exit smoothly <<<\n");
  getchar();
  g_running = 0; /* 标志置 0，各子线程看到后会自动退出 while 循环 */

  /* 【核心退场机制】：强制击穿底层的 DMA 搬运与硬件阻塞。
     若不 drop，ALSA API 会死死卡在系统内核空间，连 pthread_join
     都无法将其结束。*/
  if (handle_cap)
    snd_pcm_drop(handle_cap);
  if (handle_play)
    snd_pcm_drop(handle_play);

  /* 强行解开死等令牌的线程，让它们能跑出循环并顺利终结 */
  sem_post(&Module.empty_in);
  sem_post(&Module.full_out);

  /* 回收线程 */
  pthread_join(tid_record, NULL);
  pthread_join(tid_play, NULL);

  /* 线程安全退出后，统一释放声卡资源 */
  if (handle_cap)
    snd_pcm_close(handle_cap);
  if (handle_play)
    snd_pcm_close(handle_play);

  return 0;
}

/* ========================================================================== */
/* 音频录制线程 (生产者): 强壮的数据拼装引擎                                  */
/* ========================================================================== */
void *thread_record(void *arg) {
  int tx_write_idx = 0;
  char *tx_base = Module.bufferPtr;

  if (setup_alsa(&handle_cap, SND_PCM_STREAM_CAPTURE) < 0)
    return NULL;

  while (g_running) {
    sem_wait(&Module.empty_in);
    if (!g_running)
      break;

    /* 防撕裂机制的核心：即使由于抖动导致 ALSA 一次给不出完整的 960 帧，
       我们也利用 offset 将残余帧强制拼装，坚决不把残缺的碎数据发给 DSP。*/
    int frames_left = PERIOD_FRAMES;
    int offset = 0;

    while (frames_left > 0 && g_running) {
      int rc = snd_pcm_readi(handle_cap,
                             tx_base + (tx_write_idx * BLOCK_SIZE) + offset,
                             frames_left);
      if (rc < 0) { /* 捕捉到底层错误 (如 Overrun) */
        if (!g_running)
          break;
        if (rc == -EAGAIN)
          continue; /* 非阻塞警告，忽略 */

        /* 呼叫 ALSA 原生急救函数，瞬间修复底层指针与声卡状态，极大优于手写
         * prepare */
        rc = snd_pcm_recover(handle_cap, rc, 0);
        if (rc < 0)
          break; /* 声卡拔出等硬件级断绝，只能跳出 */
      } else {
        frames_left -= rc;              /* 扣除已成功获取的帧数 */
        offset += rc * BYTES_PER_FRAME; /* 将写入游标后移，准备下次拼装 */
      }
    }

    if (!g_running)
      break;

    /* 数据准备就绪，包含指令头和分配到的内存块绝对 Index，触发 IPC 中断 */
    /* 增加重试与微秒级休眠机制，防止底层 IPC 硬件队列满时触发 CPU 忙等死锁 */
    UInt32 payload = (CMD_APP_TO_SERVER_DATA_READY << 16) | tx_write_idx;
    while (Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                            payload, TRUE) < 0) {
      usleep(100);
    }

    tx_write_idx = (tx_write_idx + 1) % BLOCK_COUNT;
  }

  return NULL;
}

/* ========================================================================== */
/* 音频播放线程 (消费者): 同样严密的数据拼装输出                              */
/* ========================================================================== */
void *thread_play(void *arg) {
  char *rx_base = Module.bufferPtr + HALF_BUFFER_SIZE;

  if (setup_alsa(&handle_play, SND_PCM_STREAM_PLAYBACK) < 0)
    return NULL;

  while (g_running) {
    sem_wait(&Module.full_out);
    if (!g_running)
      break;

    /* 提取 DSP 加工好的目标数据块 Index */
    UInt16 rx_read_idx = Module.play_idx_queue[Module.play_q_tail];
    Module.play_q_tail = (Module.play_q_tail + 1) % INDEX_Q_SIZE;

    /* 同样开启防撕裂拼装写出，保证 960 帧全部完整塞入扬声器 */
    int frames_left = PERIOD_FRAMES;
    int offset = 0;

    while (frames_left > 0 && g_running) {
      int rc = snd_pcm_writei(handle_play,
                              rx_base + (rx_read_idx * BLOCK_SIZE) + offset,
                              frames_left);
      if (rc < 0) { /* 捕捉到底层错误 (如 Underrun) */
        if (!g_running)
          break;
        if (rc == -EAGAIN)
          continue;

        /* 让原生内核进行底层急救复位 */
        rc = snd_pcm_recover(handle_play, rc, 0);
        if (rc < 0)
          break;
      } else {
        frames_left -= rc;
        offset += rc * BYTES_PER_FRAME;
      }
    }

    if (!g_running)
      break;

    /* 通知 DSP 这块内存已经播完听响了，你可以随意覆写了 */
    /* 增加重试与微秒级休眠机制，防止底层 IPC 硬件队列满时触发 CPU 忙等死锁 */
    UInt32 payload = (CMD_APP_TO_SERVER_PLAY_DONE << 16) | rx_read_idx;
    while (Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                            payload, TRUE) < 0) {
      usleep(100);
    }
  }

  return NULL;
}

/* ========================================================================== */
/* Host 端底层硬件中断回调                                                    */
/* ========================================================================== */
Void App_notifyCB(UInt16 procId, UInt16 lineId, UInt32 eventId, UArg arg,
                  UInt32 payload) {
  App_Module *module = (App_Module *)arg;
  UInt16 cmd = (payload >> 16) & 0xFFFF;
  UInt16 index = payload & 0xFFFF;

  if (cmd == CMD_SERVER_TO_APP_RECORD_DONE) {
    /* DSP 处理完毕，归还一个空位令牌供录制线程使用 */
    sem_post(&module->empty_in);
    return;
  } else if (cmd == CMD_SERVER_TO_APP_DATA_READY) {
    /* DSP 算好了新数据：将数据的地址索引塞入防护队列，并发令牌叫醒播放线程 */
    module->play_idx_queue[module->play_q_head] = index;
    module->play_q_head = (module->play_q_head + 1) % INDEX_Q_SIZE;
    sem_post(&module->full_out);
    return;
  }

  if (payload == APP_CMD_NOP)
    return;

  /* 保存所有的框架级控制信息 */
  Event_Queue *q = &module->eventQueue;
  UInt next = (q->head + 1) % QUEUESIZE;
  if (next != q->tail) {
    q->queue[q->head] = payload;
    q->head = next;
    sem_post(&q->semH);
  }
}

static UInt32 App_waitForEvent(Event_Queue *eventQueue) {
  sem_wait(&eventQueue->semH);
  UInt32 event = eventQueue->queue[eventQueue->tail];
  eventQueue->tail = (eventQueue->tail + 1) % QUEUESIZE;
  return event;
}

/* ========================================================================== */
/* 整体资源清理：彻底的四次挥手                                               */
/* ========================================================================== */
Int App_delete() {
  Int status = 0;
  UInt32 event = 0;
  IHeap_Handle heap;

  /* 向 DSP 发出第一把断交剑 (SHUTDOWN 命令)，并等待 DSP 的死亡确认 (ACK) */
  Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                   APP_CMD_SHUTDOWN, TRUE);
  event = App_waitForEvent(&Module.eventQueue);

  /* 归还共享内存给 Linux 操作系统 */
  heap = (IHeap_Handle)SharedRegion_getHeap(SHARED_REGION_1);
  Memory_free(heap, Module.bufferPtr, FULL_BUFFER_SIZE);

  /* 注销 IPC 回调钩子，解除绑定 */
  Notify_unregisterEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                         App_notifyCB, (UArg)&Module);

  /* 摧毁所有流控设施 */
  sem_destroy(&Module.empty_in);
  sem_destroy(&Module.full_out);
  sem_destroy(&Module.eventQueue.semH);

  return status;
}