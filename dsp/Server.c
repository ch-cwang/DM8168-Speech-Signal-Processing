#define Registry_CURDESC Test__Desc
#define MODULE_NAME "Server"

#include <xdc/std.h>
#include <string.h>
#include <ti/ipc/Notify.h>
#include <ti/ipc/SharedRegion.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Task.h>
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Diags.h>
#include <xdc/runtime/Log.h>
#include <xdc/runtime/Registry.h>

#include "../shared/AppCommon.h"
#include "../shared/SystemCfg.h"
#include "Server.h"

#define QUEUESIZE 8

/* 控制命令专用队列，用于接收 Host 发来的非业务类指令 (如 Shutdown) */
typedef struct {
  UInt32 queue[QUEUESIZE];
  UInt head;
  UInt tail;
  Semaphore_Struct semObj;
  Semaphore_Handle semH;
} Event_Queue;

/* DSP 核心全局上下文结构体 */
typedef struct {
  Event_Queue eventQueue;
  UInt16 remoteProcId;
  UInt16 lineId;
  UInt32 eventId;

  /*
   * 流控信号量：
   * full_in: 记录当前有多少块“填满录音数据”的内存可以供 DSP 处理
   * empty_out: 记录当前有多少块“空闲”的内存可以供 DSP 写入播放数据
   */
  Semaphore_Struct full_in_obj;
  Semaphore_Handle full_in;
  Semaphore_Struct empty_out_obj;
  Semaphore_Handle empty_out;

  /*
   * 绝对 Index 队列：防止中断乱序导致内存读写错位
   * tx_ready_idx_queue: 存放准备好的录音数据块索引
   * rx_empty_idx_queue: 存放可用于写入播放数据的空闲块索引
   */
  UInt16 tx_ready_idx_queue[INDEX_Q_SIZE];
  UInt tx_q_head;
  UInt tx_q_tail;

  UInt16 rx_empty_idx_queue[INDEX_Q_SIZE];
  UInt rx_q_head;
  UInt rx_q_tail;
} Server_Module;

Registry_Desc Registry_CURDESC;
static Int Module_curInit = 0;
static Server_Module Module;

/* 全局运行标志位，设为 0 时，DSP 主任务将跳出死循环，走向正常释放流程 */
static volatile Int g_running = 1;

static UInt32 Server_waitForEvent(Event_Queue *eventQueue);
static Void Server_notifyCB(UInt16 procId, UInt16 lineId, UInt32 eventId,
                            UArg arg, UInt32 payload);

/* ========================================================================== */
/* 模块生命周期：供主机引导程序 (main_dsp.c) 在启动和退出时调用               */
/* ========================================================================== */
Void Server_init(Void) {
  if (Module_curInit++ != 0)
    return;
  Registry_addModule(&Registry_CURDESC, MODULE_NAME);
}

Void Server_exit(Void) {
  if (Module_curInit-- != 1)
    return;
}

/* ========================================================================== */
/* DSP 资源创建与初始化                                                       */
/* ========================================================================== */
Int Server_create(UInt16 remoteProcId) {
  Int status = 0;
  Semaphore_Params semParams;
  int i;

  memset(&Module, 0, sizeof(Server_Module));
  Module.lineId = SystemCfg_LineId;
  Module.eventId = SystemCfg_EventId;
  Module.remoteProcId = remoteProcId;

  /* 初始化计数型信号量 */
  Semaphore_Params_init(&semParams);
  semParams.mode = Semaphore_Mode_COUNTING;

  Semaphore_construct(&Module.eventQueue.semObj, 0, &semParams);
  Module.eventQueue.semH = Semaphore_handle(&Module.eventQueue.semObj);

  /* 刚启动时，没有任何数据可供处理，full_in 初始值为 0 */
  Semaphore_construct(&Module.full_in_obj, 0, &semParams);
  Module.full_in = Semaphore_handle(&Module.full_in_obj);

  /* 刚启动时，播放区完全空闲，赋予 DSP 全部 (BLOCK_COUNT) 的写入额度 */
  Semaphore_construct(&Module.empty_out_obj, BLOCK_COUNT, &semParams);
  Module.empty_out = Semaphore_handle(&Module.empty_out_obj);

  /* 【极度关键】：既然给了 BLOCK_COUNT
     个写出额度，就必须预先填入对应的空闲车位号 (Index)， 否则 DSP
     第一次写出时就会取到随机垃圾值，导致覆盖未知物理内存！ */
  for (i = 0; i < BLOCK_COUNT; i++) {
    Module.rx_empty_idx_queue[i] = i;
  }
  Module.rx_q_head = BLOCK_COUNT % INDEX_Q_SIZE;
  Module.rx_q_tail = 0;

  /* 注册 IPC 中断回调，并不断尝试与 Host 握手，直到 Host 响应 NOP */
  Notify_registerEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                       Server_notifyCB, (UArg)&Module);
  do {
    status = Notify_sendEvent(Module.remoteProcId, Module.lineId,
                              Module.eventId, APP_CMD_NOP, TRUE);
    if (status == Notify_E_EVTNOTREGISTERED)
      Task_sleep(100);
  } while (status == Notify_E_EVTNOTREGISTERED);

  return 0;
}

/* ========================================================================== */
/* DSP 音频处理主循环 (跑在 SYS/BIOS 的 Task 中)                              */
/* ========================================================================== */
Int Server_exec() {
  SharedRegion_SRPtr sharedBufferPtr = 0;
  UInt32 event;
  Char *tx_base;
  Char *rx_base;

  /* 分两次接收 Host 传来的 32 位共享内存指针 (高16位和低16位拼接) */
  event = Server_waitForEvent(&Module.eventQueue);
  sharedBufferPtr = event & APP_SPTR_MASK;
  Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                   APP_SPTR_ADDR_ACK, TRUE);

  event = Server_waitForEvent(&Module.eventQueue);
  sharedBufferPtr = ((event & APP_SPTR_MASK) << 16) | sharedBufferPtr;
  Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                   APP_SPTR_ADDR_ACK, TRUE);

  /* 将 SRPtr 转换为 DSP 本地可用的物理地址指针 */
  tx_base = SharedRegion_getPtr(sharedBufferPtr);
  rx_base = tx_base + HALF_BUFFER_SIZE; /* 播放区在内存后半段 */

  /* 只要未收到关机命令，就一直循环处理 */
  while (g_running) {
    /* 1. 等待 Host 发来装满音频的录音块 */
    Semaphore_pend(Module.full_in, BIOS_WAIT_FOREVER);
    if (!g_running) break;

    /* 提取真实数据所在的绝对索引和内存指针 */
    UInt16 active_rx_idx = Module.tx_ready_idx_queue[Module.tx_q_tail];
    Module.tx_q_tail = (Module.tx_q_tail + 1) % INDEX_Q_SIZE;
    char *read_ptr = tx_base + (active_rx_idx * BLOCK_SIZE);

    /* 2. 等待播放区腾出空闲的数据块 */
    Semaphore_pend(Module.empty_out, BIOS_WAIT_FOREVER);
    if (!g_running) break;

    /* 提取空闲区的绝对索引和内存指针 */
    UInt16 active_tx_idx = Module.rx_empty_idx_queue[Module.rx_q_tail];
    Module.rx_q_tail = (Module.rx_q_tail + 1) % INDEX_Q_SIZE;
    char *write_ptr = rx_base + (active_tx_idx * BLOCK_SIZE);

    /* === 你的音频算法核心接驳点 === */
    memcpy(write_ptr, read_ptr, BLOCK_SIZE); /* 当前为透传拷贝 */
    /* ============================== */

    /* 3. 双向触发底层硬件中断，通知 Host 认领数据 */
    /* 告诉 Host：这块录音我已经吸干了，你拿去重新录制 */
    /* 增加重试和 OS Tick 休眠机制，防止底层 IPC 队列满时死锁霸占 DSP 造成系统瘫痪 */
    while (Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                            (CMD_SERVER_TO_APP_RECORD_DONE << 16) | active_rx_idx,
                            TRUE) < 0) {
      Task_sleep(1);
    }

    /* 告诉 Host：这块播放我已经算好了，你赶紧拿去播放 */
    /* 增加重试和 OS Tick 休眠机制，防止底层 IPC 队列满时死锁霸占 DSP 造成系统瘫痪 */
    while (Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                            (CMD_SERVER_TO_APP_DATA_READY << 16) | active_tx_idx,
                            TRUE) < 0) {
      Task_sleep(1);
    }
  }

  return 0;
}

/* ========================================================================== */
/* 底层硬件中断回调函数 (极其严苛的执行环境，绝不能包含任何阻塞逻辑) */
/* ========================================================================== */
Void Server_notifyCB(UInt16 procId, UInt16 lineId, UInt32 eventId, UArg arg,
                     UInt32 payload) {
  Server_Module *module = (Server_Module *)arg;

  /* 剥离 32位 Payload：高 16 位是指令码，低 16 位是绝对 Index */
  UInt16 cmd = (payload >> 16) & 0xFFFF;
  UInt16 index = payload & 0xFFFF;

  if (cmd == CMD_APP_TO_SERVER_DATA_READY) {
    /* Host 录好了：把 Index 存入队列，释放令牌唤醒 DSP 的 read 逻辑 */
    module->tx_ready_idx_queue[module->tx_q_head] = index;
    module->tx_q_head = (module->tx_q_head + 1) % INDEX_Q_SIZE;
    Semaphore_post(module->full_in);
    return;
  } else if (cmd == CMD_APP_TO_SERVER_PLAY_DONE) {
    /* Host 播完了：把空闲 Index 存入队列，释放令牌唤醒 DSP 的 write 逻辑 */
    module->rx_empty_idx_queue[module->rx_q_head] = index;
    module->rx_q_head = (module->rx_q_head + 1) % INDEX_Q_SIZE;
    Semaphore_post(module->empty_out);
    return;
  }

  /* 【极度关键的防死锁机制】：拦截 Host 的关机命令。
     若此时 DSP 主任务卡在 full_in 或 empty_out 等待数据，如果不手动 post
     信号量， 主任务将永远无法醒来看到 g_running=0
     的变化，从而沦为僵尸进程卡死整板！*/
  if (payload == APP_CMD_SHUTDOWN) {
    g_running = 0;
    Semaphore_post(module->full_in);
    Semaphore_post(module->empty_out);
    /* 故意不 return，让该指令继续跌落到 eventQueue 中，供 Server_delete 提取 */
  }

  /* 过滤心跳包 */
  if (payload == APP_CMD_NOP)
    return;

  /* 将非数据类的控制事件存入控制队列 */
  Event_Queue *q = &module->eventQueue;
  UInt next = (q->head + 1) % QUEUESIZE;
  if (next != q->tail) {
    q->queue[q->head] = payload;
    q->head = next;
    Semaphore_post(q->semH);
  }
}

static UInt32 Server_waitForEvent(Event_Queue *eventQueue) {
  Semaphore_pend(eventQueue->semH, BIOS_WAIT_FOREVER);
  UInt32 event = eventQueue->queue[eventQueue->tail];
  eventQueue->tail = (eventQueue->tail + 1) % QUEUESIZE;
  return event;
}

/* ========================================================================== */
/* DSP 资源销毁与回收 (主任务退出循环后才会执行到这里)                        */
/* ========================================================================== */
Int Server_delete() {
  Int status = 0;
  UInt32 event;

  /* 从控制队列中取出刚才漏下来的 SHUTDOWN 命令 */
  event = Server_waitForEvent(&Module.eventQueue);
  if (event >= APP_E_FAILURE)
    return -1;

  /* 发送关机 ACK 确认，完成完美闭环的四次挥手 */
  Notify_sendEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                   APP_CMD_SHUTDOWN_ACK, TRUE);
  Notify_unregisterEvent(Module.remoteProcId, Module.lineId, Module.eventId,
                         Server_notifyCB, (UArg)&Module);

  /* 彻底粉碎所有信号量，归还 BIOS 内存 */
  Semaphore_destruct(&Module.eventQueue.semObj);
  Semaphore_destruct(&Module.full_in_obj);
  Semaphore_destruct(&Module.empty_out_obj);

  return status;
}