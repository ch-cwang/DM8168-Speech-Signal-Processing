#ifndef APPCOMMON_H_
#define APPCOMMON_H_

#define SHARED_REGION_1             1

#define APP_CMD_NOP                 0x00000000
#define APP_SPTR_LADDR              0x10000000
#define APP_SPTR_HADDR              0x20000000
#define APP_SPTR_ADDR_ACK           0x30000000
#define APP_CMD_SHUTDOWN            0x40000000
#define APP_CMD_SHUTDOWN_ACK        0x50000000

#define APP_E_FAILURE               0xE0000000
#define APP_E_OVERFLOW              0xE0000001
#define APP_SPTR_MASK               0x0000FFFF

/* ========================================================================== */
/* 核心音质参数：对标 arecord -f dat (48000Hz, Stereo, S16_LE)                  */
/* ========================================================================== */
#define PERIOD_FRAMES               960   /* 每周期 20ms，完美适应 Linux 调度 */
#define BYTES_PER_FRAME             4     /* 双声道 * 16bit(2字节) = 4字节 */
#define BLOCK_SIZE                  (PERIOD_FRAMES * BYTES_PER_FRAME) /* 3840 字节 */
#define BLOCK_COUNT                 20    /* 单向 20 块，共 400ms 缓冲池 */

#define HALF_BUFFER_SIZE            (BLOCK_SIZE * BLOCK_COUNT)
#define FULL_BUFFER_SIZE            (HALF_BUFFER_SIZE * 2)

#define INDEX_Q_SIZE                64

#define CMD_APP_TO_SERVER_DATA_READY  0x0001 
#define CMD_APP_TO_SERVER_PLAY_DONE   0x0002 
#define CMD_SERVER_TO_APP_DATA_READY  0x0003 
#define CMD_SERVER_TO_APP_RECORD_DONE 0x0004 

#endif /* APPCOMMON_H_ */