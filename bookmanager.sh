#!/bin/bash
# bookmanager.sh — 编译带开局库管理功能的 eleeye engine (arm64-v8a + x86_64)
# 包含了xiangqi_jni.cpp的所有功能
# 删除后来加的残局归一化等功能，保留book_manager.cpp的所有功能
# 用法: bash bookmanager.sh

set -e

NDK="/d/AndroidNDK/30.0.14904198"
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64"
API=34

OUT="jniLibs"
rm -rf "$OUT"
rm -f eleeye/*.o
mkdir -p "$OUT/arm64-v8a"
mkdir -p "$OUT/x86_64"

SRCS="eleeye/xiangqi_jni.cpp eleeye/book.cpp eleeye/evaluate.cpp \
eleeye/genmoves.cpp eleeye/hash.cpp eleeye/movesort.cpp eleeye/position.cpp \
eleeye/preeval.cpp eleeye/pregen.cpp eleeye/search.cpp eleeye/book_manager.cpp \
eleeye/book_common.cpp eleeye/book_endgame_normalizer.cpp"

INCS="-Ieleeye -Ieleeye/Position -Ieleeye/Search"

# -shared 动态so | -fPIC | C++17 | -O2 | 宏定义 | soname(LGPL合规)
FLAGS="-shared -fPIC -std=c++17 -O2 -DCCHESS_A3800 -Wl,--soname,libeleeye_engine.so"

echo "=== Building arm64-v8a ==="
$TOOLCHAIN/bin/aarch64-linux-android$API-clang++ $FLAGS $SRCS $INCS -llog \
  -o "$OUT/arm64-v8a/libeleeye_engine.so"
cp "$TOOLCHAIN/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$OUT/arm64-v8a/"

echo "=== Building x86_64 ==="
$TOOLCHAIN/bin/x86_64-linux-android$API-clang++ $FLAGS $SRCS $INCS -llog \
  -o "$OUT/x86_64/libeleeye_engine.so"
cp "$TOOLCHAIN/sysroot/usr/lib/x86_64-linux-android/libc++_shared.so" "$OUT/x86_64/"

echo ""
echo "=== Build done ==="
ls -lhR "$OUT"