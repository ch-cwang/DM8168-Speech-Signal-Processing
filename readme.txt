DM8168 Speech Signal Processing (Audio Streaming System)

=============================================================================

Notice: 
This project has evolved from the TI "Shared Region Example" into a complete 
real-time audio streaming and processing framework between the ARM (Host) and 
DSP (Server). 

The legacy string conversion logic (lowercase to uppercase) has been entirely 
replaced by a robust, anti-tearing zero-copy audio pipeline using ALSA and 
SysLink IPC.

For full project documentation, architectural diagrams, requirements, and 
testing instructions, please refer to the markdown file:
-> README.md
-> docs/system_design.md

=============================================================================
