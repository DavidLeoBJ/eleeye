#!/bin/bash
rm -f libeleeye_engine.so
NDK="/d/AndroidNDK/30.0.14904198"
CLANG="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android34-clang++"

# Disable UCCI/pipe (not needed for JNI), keep opening book enabled manually
$CLANG -shared -static-libstdc++ -fPIC -DCCHESS_A3800 \
  -o libeleeye_engine.so \
  eleeye/xiangqi_jni.cpp \
  eleeye/book.cpp \
  eleeye/evaluate.cpp \
  eleeye/genmoves.cpp \
  eleeye/hash.cpp \
  eleeye/movesort.cpp \
  eleeye/position.cpp \
  eleeye/preeval.cpp \
  eleeye/pregen.cpp \
  eleeye/search.cpp \
  -Ieleeye -Ieleeye/Position -Ieleeye/Search

echo "Build done."