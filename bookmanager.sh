#!/bin/bash
# bookmanager.sh — 编译带开局库管理功能的 eleeye engine (arm64-v8a + x86_64)
# 包含了xiangqi_jni.cpp的所有功能并引入book_manager.cpp的所有功能
# 已经修改为静态so,总尺寸减小。
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

# -shared 动态so-->静态so所以增加-static-libstdc++选项 | -fPIC | C++17 | -O2 | 宏定义 | soname(LGPL合规)
# FLAGS="-shared -fPIC -std=c++17 -O2 \
# -DCCHESS_A3800 -Wl,-s,--soname,libeleeye_engine.so"
FLAGS="-shared -static-libstdc++ -fno-exceptions -fno-rtti -fPIC -std=c++17 -O2 \
-DCCHESS_A3800 -Wl,-s -Wl,--soname,libeleeye_engine.so"
# -DCCHESS_A3800：如果代码中定义了这个宏ifdefined(#ifdef)，那么在编译时会自动包含一些代码，比如一些常量定义等。
# -DCCHESS_A3800：如果代码中不定义这个宏if not defined(#ifndef)，那么在编译时不会包含一些代码。
# -fno-exceptions:关异常可减几百k
# -fno-rtti:关运行时类型信息可进一步减小尺寸
# Wl,xxx→ 告诉 clang：把 xxx原样丢给 linker (ld)
# -s→ ld 的参数，意思是 strip all symbols,简写等同于：strip libeleeye_engine.so

# 注：很多so的项目要使用静态编译，共享c++_shared,总体积会变小。另外release版本中只保留arm64-v8a体积变小。
# 一个静态so几十K,但c++_shared要9M;动态so800k,10个动态so才8M,所以10so以内要采用动态比较合理，
# 只有很多so才有必要编译静态so

echo "=== Building arm64-v8a ==="
$TOOLCHAIN/bin/aarch64-linux-android$API-clang++ $FLAGS $SRCS $INCS -llog \
  -o "$OUT/arm64-v8a/libeleeye_engine.so"
# 改为静态可有效减小so的尺寸
# cp "$TOOLCHAIN/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$OUT/arm64-v8a/"

echo "=== Building x86_64 ==="
$TOOLCHAIN/bin/x86_64-linux-android$API-clang++ $FLAGS $SRCS $INCS -llog \
  -o "$OUT/x86_64/libeleeye_engine.so"
# 改为静态可有效减小so的尺寸
# cp "$TOOLCHAIN/sysroot/usr/lib/x86_64-linux-android/libc++_shared.so" "$OUT/x86_64/"

echo ""
echo "=== Build done ==="
ls -lhR "$OUT"