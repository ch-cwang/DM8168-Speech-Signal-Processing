#!/bin/sh
# ==============================================================================
# 测试用例 2: CPU 极端高负载抗撕裂测试
# ==============================================================================

# 确保在编译输出目录下运行
if [ ! -f "./run.sh" ]; then
    echo "错误：找不到可执行脚本 ./run.sh。请在 install 部署目录下运行本脚本。"
    exit 1
fi

echo "======================================================"
echo " [用例 2] CPU 极端高负载抗撕裂测试 (运行 10 秒) "
echo "======================================================"
echo "请用 PC 持续播放 1kHz 正弦波，并在同一台 PC 用 Audacity 开始录音。"

# 启动 2 个高强度无意义计算后台进程，榨干 ARM 端 CPU
dd if=/dev/urandom of=/dev/null bs=1M 2>/dev/null &
LOAD_PID1=$!
dd if=/dev/urandom of=/dev/null bs=1M 2>/dev/null &
LOAD_PID2=$!

echo "负载已施加。请在此 10 秒内使用 Audacity 录制波形以供后续检视撕裂情况..."
(sleep 10; echo "") | ./run.sh
RET_VAL=$?

# 清理高负载进程
kill -9 $LOAD_PID1 $LOAD_PID2 2>/dev/null
echo "已清理负载进程。"

if [ $RET_VAL -eq 0 ]; then
    echo "[PASSED] 用例 2 通过：高压下音频内核能稳固处理，无崩溃退出。"
else
    echo "[FAILED] 用例 2 失败：在高压环境下系统崩溃，返回值 $RET_VAL。"
    exit 1
fi
