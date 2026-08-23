#!/bin/bash
# 注意：请在 Git Bash / WSL 中运行，不要在 PowerShell 直接执行
# 必须在模拟器（或真机）“系统完全启动 + adb 授权完成”之后才能跑
# 7件事：①cp，②clean,③assemble,
# ④uninstall,⑤install,⑥start,⑦adb logcat

# 只要发现一步错就立即停止防止错误扩散
# -e：出错退出
# -u：未定义变量直接炸
# -o pipefail：管道里任何一个失败就整个失败（关键）

set -e
# set -euo pipefail
# 模拟器+真机情况下脚本不知道该用哪个：选择后一个
# 设备号去脏
# D=$(adb devices | awk '/device$/ && !/emulator/ {print $1; exit}' | tr -d '\r')
# echo "Using device: $D"

#########################
# 基础配置
#########################
# 强制使用 Android SDK 的 adb（MSYS / Git Bash 通用）
export PATH="/c/Users/13012/AppData/Local/Android/Sdk/platform-tools:$PATH"
ELEEYE_JNILIBS="/d/androidstudioprojects/eleeye/jniLibs"
AS_JNILIBS="D:/AndroidStudioProjects/ChineseChessSpectator/appb/src/main/jniLibs"

PKG="com.example.appb"
GRADLEW_DIR="/d/androidstudioprojects/ChineseChessSpectator"
MODULE=":appb"
APK="$GRADLEW_DIR/appb/build/outputs/apk/debug/appb-debug.apk"

echo ">>> Working dir: $(pwd)"
echo ">>> Source jniLibs: $ELEEYE_JNILIBS"
echo ">>> Target jniLibs: $AS_JNILIBS"
echo ">>> APK: $APK"

#########################
# 1. 同步 jniLibs
#########################
echo ""
echo "=== [1/7] Sync jniLibs (overwrite) ==="
rm -rf "$AS_JNILIBS"
mkdir -p "$AS_JNILIBS"

for abi in arm64-v8a x86_64; do
    echo "Copying $abi..."
    mkdir -p "$AS_JNILIBS/$abi"
    cp "$ELEEYE_JNILIBS/$abi/libeleeye_engine.so" "$AS_JNILIBS/$abi/"    
    # cp "$ELEEYE_JNILIBS/$abi/libc++_shared.so" "$AS_JNILIBS/$abi/" # 静态libc++_shared.so不需要了。
done

#########################
# 2. Gradle clean
#########################
echo ""
echo "=== [2/7] Gradle clean ==="
cd "$GRADLEW_DIR"
#./gradlew clean # clean全工程
# 只清appb
./gradlew $MODULE:assembleDebug

#########################
# 3. Assemble module
#########################
echo ""
echo "=== [3/7] Assemble module $MODULE ==="
./gradlew $MODULE:assembleDebug

#########################
# 4. 彻底清场
#########################
echo ""
echo "=== [4/7] Uninstall & clean device ==="
adb uninstall $PKG || true
# 不要手工删，卸载就够了
# adb shell rm -rf "/data/data/$PKG"
# adb shell rm -rf "/data/app/${PKG}-*"

#########################
# 5. 安装
#########################
echo ""
echo "=== [5/7] Install APK ==="
adb install -r -t "$APK"

#########################
# 6. 启动并触发搜索
#########################
echo ""
echo "=== [6/7] Launch & trigger search ==="
adb shell am start -n $PKG/.MainActivity #\
  #-e "fen" "9/3Nak3/9/9/9/9/9/9/9/4K4 w - - 0 0" \
  #-e "depth" "4"

#########################
# 7. 抓取关键日志
#########################
echo ""
echo "=== [7/7] Logcat (engine signals) ==="
sleep 0.5
#一直捕捉日志：
#adb logcat -c && adb logcat | grep -E "Xiangqi_JNI|BookManager|BookTest|=== RAW"
# 可以改为adb -s $D logcat -d -s BookTest:V | awk '/=== RAW/'

echo "=== [7/7] Logcat (one-shot) ==="
# -c 清空日志 前面的-s是指定设置
# 多模拟器时-s指定设备号
#adb -s $D logcat -c
adb logcat -c # 单机版本清日志
sleep 1
# -d 只抓取最近的日志
# -s 指定设置，如果放在后面是指定按指定的tag抓取日志如：adb logcat -s BookTest:V Xiangqi_JNI:V
# -e 指定过滤条件，如：adb logcat -e BookTest -e Xiangqi_JNI
# -v 指定日志级别，如：adb logcat -v brief -e BookTest -e Xiangqi_JNI
# -t 指定时间格式，如：adb logcat -t %d-%m-%y %H:%M:%S -e BookTest -e Xiangqi_JNI
# -b 指定缓冲区，如：adb logcat -b events -e BookTest -e Xiangqi_JNI
# -f 指定日志文件，如：adb logcat -f /sdcard/logcat.txt -e BookTest -e Xiangqi_JNI
# -n 指定日志条数，如：adb logcat -n 10 -e BookTest -e Xiangqi_JNI
# 如果logcat -s Tag:V只能按 Tag 过滤，不能按内容过滤，所以需要使用 -e 过滤内容。
adb -s logcat -d | grep -E "BookTest|=== RAW"
echo "=== DONE ==="