// book_endgame_normalizer.h
#pragma once
#include "book_common.h"  // 已经包含了TransformList的定义
#include <string>
#include "position.h" // 确保PositionStruct的定义可见
namespace EndgameNormalizer {
    // 残局专用简易FEN解析器（你已有的）
    bool parseFenForEndgame(const char* fenStr, PositionStruct& pos);
    // 残局同质化归一化函数（补上这个声明！之前漏了）
    std::string normalizeFenForEndgame(const std::string& rawFen);
    // 你其他的残局函数声明...
}

namespace EndgameNormalizer {
    std::string NormalizeFen(const std::string& rawFen); // 新函数，替换旧的
    int GetMove(const std::string& rawFen);
    bool SaveMove(const std::string& rawFen, int fromRank, int fromFile, int toRank, int toFile);
}
// book_endgame_normalizer.h 里的声明
namespace EndgameNormalizer {
    int JavaCoordToStdSqForEndgame(int javaRank, int javaFile, const TransformList& transforms);
}