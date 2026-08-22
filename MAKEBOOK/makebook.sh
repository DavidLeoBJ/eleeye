#!/bin/bash
# ============================================================
# ElephantEye Book Maker - MSYS2 MinGW64 编译脚本
# 位置: eleeye/MAKEBOOK/build_makebook.sh
# ============================================================

set -e

# 项目根目录（脚本在 MAKEBOOK/ 里，所以上级目录是根）
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${PROJECT_ROOT}"
PGN_DIR="/d/Eleeye_PT_Makebook"

# 输出目录就是当前目录（MAKEBOOK/）
OUTPUT_DIR="$(pwd)"
OUTPUT="${OUTPUT_DIR}/MicroBookMaker.exe"

echo "============================================"
echo "  ElephantEye Book Maker - 编译开始"
echo "============================================"

mkdir -p "${OUTPUT_DIR}"
rm -f "${OUTPUT}"

# 编译
g++ -O2 -std=c++11 -D_WIN32 \
    -I"${SRC_DIR}" \
    "${SRC_DIR}/book/makebook.cpp" \
    "${SRC_DIR}/eleeye/pregen.cpp" \
    "${SRC_DIR}/eleeye/position.cpp" \
    "${SRC_DIR}/eleeye/genmoves.cpp" \
    "${SRC_DIR}/eleeye/book.cpp" \
    "${SRC_DIR}/cchess/cchess.cpp" \
    "${SRC_DIR}/cchess/pgnfile.cpp" \
    -o "${OUTPUT}" \
    -static-libgcc -static-libstdc++

echo ""
echo "✅ 编译成功！"
echo "   输出: ${OUTPUT}"
echo "   大小: $(stat -c %s "${OUTPUT}") 字节"

# 生成开局库
echo ""
echo "============================================"
echo "         生成开局库 micro.book"
echo "============================================"

if [ -f "${OUTPUT}" ]; then
    "${OUTPUT}" "${PGN_DIR}/WXF-41743games.pgns" micro.book 3 1 -1 4
    echo ""
    echo "✅ micro.book 大小: $(stat -c %s micro.book) 字节"
    if [ "$(stat -c %s micro.book)" -gt 0 ]; then
        echo "🎉 开局库生成成功！"
    else
        echo "WARNING: micro.book 为空"
    fi
else
    echo "ERROR: 编译产物不存在"
    exit 1
fi