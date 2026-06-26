#!/bin/sh
# ==============================================================================
# ALSA 底层旁路抓包配置脚本 (ALSA Snoop Setup)
# 作用：无需修改 C 代码，在 ALSA 驱动层将发送给外放扬声器的数据拦截并保存到本地。
# ==============================================================================

ASOUNDRC="$HOME/.asoundrc"
OUTPUT_FILE="/tmp/dsp_playback.raw"

if [ "$1" = "on" ]; then
    echo "开启 ALSA 旁路抓包模式..."
    
    # 写入 ALSA 配置文件，采用 asym 非对称路由
    # 输入保持真实硬件 (确保时钟中断不乱)，输出分为真实硬件和本地文件。
    cat > "$ASOUNDRC" << 'EOF'
pcm.!default {
    type asym
    playback.pcm {
        type file
        slave.pcm "plughw:0,0"
        file "/tmp/dsp_playback.raw"
        format "raw"
    }
    capture.pcm "plughw:0,0"
}
EOF
    
    # 删除旧的抓包文件
    rm -f "$OUTPUT_FILE"
    
    echo "配置成功！"
    echo "请运行您的音频测试（例如 ./run.sh）。"
    echo "测试结束后，音频输出波形将会自动无损保存至: $OUTPUT_FILE"
    echo "您可使用 Audacity 导入该 Raw Data 文件进行验证 (16-bit PCM, Little-endian, 48000Hz, 2 Channels)。"

elif [ "$1" = "off" ]; then
    echo "关闭 ALSA 旁路抓包模式..."
    rm -f "$ASOUNDRC"
    echo "配置已恢复为系统默认。"

else
    echo "用法: $0 [on|off]"
    echo "  on  - 开启抓包，生成旁路录音配置"
    echo "  off - 关闭抓包，恢复原状"
    exit 1
fi
