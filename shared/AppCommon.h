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

/**
 * @name Core Audio Quality Parameters
 * @brief Matches studio grade: arecord -f dat (48000Hz, Stereo, S16_LE)
 * @{
 */
#define PERIOD_FRAMES               960   /**< 20ms per period, perfectly fits Linux ALSA scheduling (每周期 20ms，完美适应 Linux 调度) */
#define BYTES_PER_FRAME             4     /**< Stereo * 16-bit (2 bytes) = 4 bytes per frame (双声道*16bit=4字节) */
#define BLOCK_SIZE                  (PERIOD_FRAMES * BYTES_PER_FRAME) /**< 3840 bytes per block (单块字节数) */
#define BLOCK_COUNT                 20    /**< 20 blocks per direction, total 400ms buffer pool (单向20块，共400ms缓冲池) */
/** @} */

#define HALF_BUFFER_SIZE            (BLOCK_SIZE * BLOCK_COUNT)
#define FULL_BUFFER_SIZE            (HALF_BUFFER_SIZE * 2)

#define INDEX_Q_SIZE                64

/**
 * @name IPC Command Payload Definitions
 * @brief Low 16 bits contains the absolute index, high 16 bits contains the command
 * @{
 */
#define CMD_APP_TO_SERVER_DATA_READY  0x0001 /**< Host -> DSP: Record data is ready (录音数据就绪) */
#define CMD_APP_TO_SERVER_PLAY_DONE   0x0002 /**< Host -> DSP: Playback block is empty and done (播放块已空闲) */
#define CMD_SERVER_TO_APP_DATA_READY  0x0003 /**< DSP -> Host: Processed data is ready to play (算法处理完成，待播放) */
#define CMD_SERVER_TO_APP_RECORD_DONE 0x0004 /**< DSP -> Host: Record block consumed and empty (录音块已被消耗) */
/** @} */

#endif /* APPCOMMON_H_ */