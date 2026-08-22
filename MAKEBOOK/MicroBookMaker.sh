#!/bin/bash
# ============================================================
# ElephantEye Book Maker - MSYS2 MinGW64 编译脚本
# 只编译必要文件，生成 MicroBookMaker.exe
# ============================================================

set -e  # 出错立即退出

PROJECT_ROOT="/d/AndroidStudioProjects/eleeye"
SRC_DIR="/d/AndroidStudioProjects/eleeye"
OUTPUT="/d/AndroidStudioProjects/eleeye/MAKEBOOK/MicroBookMaker.exe"

echo "============================================"
echo "  ElephantEye Book Maker - 编译开始"
echo "============================================"

# 清理旧产物
rm -f "${OUTPUT}"

# 编译（只给必要的cpp，不多给）
g++ -O2 -std=c++11 -D_WIN32 \
    -I"${SRC_DIR}" \
    "${SRC_DIR}/book/makebook.cpp" \
    "${SRC_DIR}/eleeye/position.cpp" \
    "${SRC_DIR}/eleeye/genmoves.cpp" \
    "${SRC_DIR}/eleeye/pregen.cpp" \
    "${SRC_DIR}/eleeye/book.cpp" \
    "${SRC_DIR}/cchess/cchess.cpp" \
    "${SRC_DIR}/cchess/pgnfile.cpp" \
    -o "${OUTPUT}" \
    -static-libgcc -static-libstdc++

echo ""
echo "✅ 编译成功！"
echo "   输出: ${OUTPUT}"
echo "   大小: $(stat -c %s "${OUTPUT}") 字节"
echo ""

# 生成 micro.book
echo "============================================"
echo "         生成开局库 micro.book"
echo "============================================"

# cd "${PROJECT_ROOT}"

if [ -f "MicroBookMaker.exe" ]; then
    ./MicroBookMaker.exe WXF-41743games.pgns micro.book 3 1 -1 4
    echo ""
    echo "✅ micro.book 大小: $(stat -c %s micro.book) 字节"
    if [ "$(stat -c %s micro.book)" -gt 0 ]; then
        echo "🎉 开局库生成成功！"
    else
        echo "WARNING: micro.book is empty; check the input PGN and parameters."
    fi
else
    echo "ERROR: MicroBookMaker.exe was not found; compilation failed."
    exit 1
fi

echo ""
echo "============================================"
echo "  Build and book generation completed"
echo "============================================"