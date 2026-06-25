import re

def process_app_c():
    with open('host/App.c', 'r', encoding='utf-8') as f:
        content = f.read()

    # Block 1
    content = content.replace(
        '/* 控制命令队列 (接收 DSP 发来的 ACK) */',
        '/**\n * @brief Control command queue (Receives ACK from DSP)\n *        (控制命令队列，接收 DSP 发来的 ACK)\n */'
    )
    content = content.replace(
        '/* Host 核心全局上下文结构体 */',
        '/**\n * @brief Host core global context structure\n *        (Host 核心全局上下文结构体)\n */'
    )
    content = content.replace(
        '/* POSIX 信号量，用于线程间的流控同步 */',
        '/* POSIX semaphores for flow control synchronization between threads (POSIX 信号量，用于线程间的流控同步) */'
    )
    content = content.replace(
        '/* 播放数据的绝对索引队列，跨线程传输必须依赖它防乱序 */',
        '/* Absolute index queue for playback data. Essential for cross-thread anti-tearing (播放数据的绝对索引队列，跨线程传输必须依赖它防乱序) */'
    )
    content = content.replace(
        '/* 全局化 ALSA\n * 文件句柄，是为了在主线程要求退出时，能够强行中断底层硬件的读写阻塞 */',
        '/* Global ALSA file handles, used to forcefully break hardware read/write blocking when main thread requests exit (全局化 ALSA 句柄，用于强行打断底层阻塞) */'
    )
    
    # Setup ALSA
    content = content.replace(
        '/* ========================================================================== */\n/* 工业级 ALSA 硬件与软件参数配置函数 */\n/* 参数 handle: 指向声卡句柄指针的指针，配置成功后通过它把声卡操作权交回给调用者\n */\n/* 参数 stream: 枚举类型，表明当前是要求配置录音 (CAPTURE) 还是播放 (PLAYBACK)\n */\n/* ========================================================================== */',
        '/**\n * @brief Industrial-grade ALSA hardware and software parameter configuration\n *        (工业级 ALSA 硬件与软件参数配置函数)\n * @param handle Pointer to PCM handle pointer (指向声卡句柄指针的指针)\n * @param stream SND_PCM_STREAM_CAPTURE or SND_PCM_STREAM_PLAYBACK\n * @return 0 on success, -1 on failure\n */'
    )
    
    # Thread record
    content = content.replace(
        '/* ========================================================================== */\n/* 音频录制线程 (生产者): 强壮的数据拼装引擎                                  */\n/* ========================================================================== */',
        '/**\n * @brief Audio recording thread (Producer): Robust data assembly engine\n *        (音频录制线程: 强壮的数据拼装引擎)\n * @param arg Unused\n * @return NULL\n */'
    )

    # Thread play
    content = content.replace(
        '/* ========================================================================== */\n/* 音频播放线程 (消费者): 同样严密的数据拼装输出                              */\n/* ========================================================================== */',
        '/**\n * @brief Audio playback thread (Consumer): Strict data assembly output\n *        (音频播放线程: 同样严密的数据拼装输出)\n * @param arg Unused\n * @return NULL\n */'
    )

    # notify CB
    content = content.replace(
        '/* ========================================================================== */\n/* Host 端底层硬件中断回调                                                    */\n/* ========================================================================== */',
        '/**\n * @brief Host-side low-level hardware interrupt callback\n *        (Host 端底层硬件中断回调)\n * @param procId Remote processor ID\n * @param lineId Interrupt line ID\n * @param eventId Interrupt event ID\n * @param arg User argument (App_Module instance)\n * @param payload 32-bit IPC payload\n */'
    )
    
    # App delete
    content = content.replace(
        '/* ========================================================================== */\n/* 整体资源清理：彻底的四次挥手                                               */\n/* ========================================================================== */',
        '/**\n * @brief Global resource cleanup: Complete 4-way handshake teardown\n *        (整体资源清理：彻底的四次挥手)\n * @return 0 on success\n */'
    )

    with open('host/App.c', 'w', encoding='utf-8') as f:
        f.write(content)

def process_server_c():
    with open('dsp/Server.c', 'r', encoding='utf-8') as f:
        content = f.read()

    # Struct comments
    content = content.replace(
        '/* 控制命令专用队列，用于接收 Host 发来的非业务类指令 (如 Shutdown) */',
        '/**\n * @brief Control command queue (Receives non-business commands like Shutdown from Host)\n *        (控制命令专用队列，用于接收 Host 发来的非业务类指令)\n */'
    )
    content = content.replace(
        '/* DSP 核心全局上下文结构体 */',
        '/**\n * @brief DSP core global context structure\n *        (DSP 核心全局上下文结构体)\n */'
    )
    
    # Init/Exit
    content = content.replace(
        '/* ========================================================================== */\n/* 模块生命周期：供主机引导程序 (main_dsp.c) 在启动和退出时调用               */\n/* ========================================================================== */',
        '/**\n * @brief Module lifecycle: Called by bootloader (main_dsp.c) during init/exit\n *        (模块生命周期：供主机引导程序在启动和退出时调用)\n */'
    )

    # Server_create
    content = content.replace(
        '/* ========================================================================== */\n/* DSP 资源创建与初始化                                                       */\n/* ========================================================================== */',
        '/**\n * @brief DSP resource creation and initialization\n *        (DSP 资源创建与初始化)\n * @param remoteProcId Host processor ID\n * @return 0 on success\n */'
    )

    # Server_exec
    content = content.replace(
        '/* ========================================================================== */\n/* DSP 音频处理主循环 (跑在 SYS/BIOS 的 Task 中)                              */\n/* ========================================================================== */',
        '/**\n * @brief DSP audio processing main loop (Runs in SYS/BIOS Task)\n *        (DSP 音频处理主循环，跑在 SYS/BIOS 的 Task 中)\n * @return 0 on exit\n */'
    )

    # notify CB
    content = content.replace(
        '/* ========================================================================== */\n/* 底层硬件中断回调函数 (极其严苛的执行环境，绝不能包含任何阻塞逻辑) */\n/* ========================================================================== */',
        '/**\n * @brief Low-level hardware interrupt callback (Strict execution context, no blocking allowed!)\n *        (底层硬件中断回调函数，极其严苛的执行环境，绝不能包含任何阻塞逻辑)\n * @param procId Remote processor ID\n * @param lineId Interrupt line ID\n * @param eventId Interrupt event ID\n * @param arg User argument (Server_Module instance)\n * @param payload 32-bit IPC payload\n */'
    )

    # Server_delete
    content = content.replace(
        '/* ========================================================================== */\n/* DSP 资源销毁与回收 (主任务退出循环后才会执行到这里)                        */\n/* ========================================================================== */',
        '/**\n * @brief DSP resource destruction and reclamation (Executed after main loop exits)\n *        (DSP 资源销毁与回收，主任务退出循环后才会执行到这里)\n * @return 0 on success\n */'
    )

    with open('dsp/Server.c', 'w', encoding='utf-8') as f:
        f.write(content)

def process_main_host():
    with open('host/main_host.c', 'r', encoding='utf-8') as f:
        content = f.read()
        
    content = content.replace(
        '/*\n *  ======== main ========\n */',
        '/**\n * @brief Host application entry point\n * @param argc Argument count\n * @param argv Argument vector\n * @return Process exit status\n */'
    )
    content = content.replace(
        '/*\n *  ======== Main_main ========\n */',
        '/**\n * @brief Main application execution phase\n * @return 0 on success\n */'
    )
    content = content.replace(
        '/*\n *  ======== Main_parseArgs ========\n */',
        '/**\n * @brief Command line argument parser\n * @param argc Argument count\n * @param argv Argument vector\n * @return 0 on success, -1 on failure\n */'
    )
    
    with open('host/main_host.c', 'w', encoding='utf-8') as f:
        f.write(content)

def process_main_dsp():
    with open('dsp/main_dsp.c', 'r', encoding='utf-8') as f:
        content = f.read()

    content = content.replace(
        '/*\n*  ======== main ========\n*/',
        '/**\n * @brief DSP application entry point (BIOS boot)\n * @param argc Argument count\n * @param argv Argument vector\n * @return 0\n */'
    )
    content = content.replace(
        '/*\n*  ======== smain ========\n*/',
        '/**\n * @brief DSP main task thread\n * @param arg0 Unused\n * @param arg1 Unused\n */'
    )

    with open('dsp/main_dsp.c', 'w', encoding='utf-8') as f:
        f.write(content)

if __name__ == "__main__":
    process_app_c()
    process_server_c()
    process_main_host()
    process_main_dsp()
    print("Replacements complete.")
