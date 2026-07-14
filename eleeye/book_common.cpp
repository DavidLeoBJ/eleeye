#define MODULE_TAG "BookCommon"
#include "book_endgame_normalizer.h"
#include "book_common.h"
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>

// Java坐标转象眼SQ（之前Common命名空间里的逻辑，挪到全局）
int JavaCoordToSq(int javaRank, int javaFile) {
    // 你之前的转换逻辑，比如：
    int x = javaFile - 1;  // Java列1-9 → 0-8
    int y = 9 - javaRank;  // Java行1-9 → 0-9（1=红底线）
    return SQ(x, y);
}
/** 解析FEN字符串并填充PositionStruct对象 

** 1. 首先判断开局库是否已加载，如果没有加载则调用internalOpenBook()加载。
** 2. 然后对输入的FEN进行归一化处理，得到标准化的FEN字符串。
** 3. 检查标准化的FEN是否是残局FEN，如果是残局FEN则解析标准化的FEN得到棋盘状态。
** 4. 如果标准化的FEN不是残局FEN，则直接将输入的FEN解析得到棋盘状态。
** 5. 获取标准化FEN对应的Zobrist锁（哈希值）。  
** 8. 加锁并查找Zobrist锁对应的BookEntry。
** 9. 如果找到BookEntry，则返回权重最高的着法（wmv）。  
** 10. 如果没有找到BookEntry，则返回0。
*/
int queryEndgameMoveInternal(const std::string& rawFen, uint32_t& outHash) {
    if (!g_loaded && !internalOpenBook()) {// 如果开局库未打开过，并且开局库打不开
        LOGE("queryEndgameMoveInternal: Failed to load book!");
        return 0;
    }

    std::string stdFen = EndgameNormalizer::normalizeFenForEndgame(rawFen);
    // ✅ 加这行日志，直接对比两个rawFen的归一化结果
    LOGE("EndgameNormalize: rawFen=%s => stdFen=%s", rawFen.c_str(), stdFen.c_str());
    PositionStruct pos;

    if (isEndgameFen(rawFen)) { // 必须用原始FEN判断残局
        parseFenForEndgame(stdFen.c_str(), pos);
    } else {
        pos.FromFen(rawFen.c_str());
    }

    outHash = pos.zobr.dwLock1;
    std::lock_guard<std::mutex> lock(g_mutex);

    // ✅ 第一步：找第一个>=outHash的位置（lower_bound，参数顺序：元素在前，值在后）
    auto lower_it = std::lower_bound(
        g_entries.begin(),
        g_entries.end(),
        outHash,
        [](const BookEntry& entry, uint32_t hash) { // 明确：元素是BookEntry，值是uint32_t
            return entry.dwZobristLock < hash;
        }
    );

    // ✅ 第二步：找第一个>outHash的位置（upper_bound，参数顺序：值在前，元素在后）
    auto upper_it = std::upper_bound(
        g_entries.begin(),
        g_entries.end(),
        outHash,
        [](uint32_t hash, const BookEntry& entry) { // 明确：值是uint32_t，元素是BookEntry
            return hash < entry.dwZobristLock;
        }
    );

    // ✅ 第三步：组合成equal_range的效果
    auto range = std::make_pair(lower_it, upper_it);
    if (range.first == range.second) return 0;

    // 后续选权重最高的着法逻辑不变
    int bestWvl = 0, bestWmv = 0;
    for (auto it = range.first; it != range.second; ++it) {
        if (it->wvl > bestWvl) {
            bestWvl = it->wvl;
            bestWmv = it->wmv;
        }
    }
    return bestWmv;
}

// 定义全局段落缓存
std::vector<std::string> g_stdFenSegments;
// 原来的void版本实现，把段落存到全局缓存里
void splitFenToSegments(const std::string& fen) {
    g_stdFenSegments.clear();
    std::istringstream iss(fen);
    std::string seg;
    // 按空格拆分FEN为6个标准段落（局面/轮走方/王车易位/过路兵/半回合数/回合数）
    for (int i = 0; i < 6 && iss >> seg; ++i) {
        g_stdFenSegments.push_back(seg);
    }
    // 补全不足6段的场景
    while (g_stdFenSegments.size() < 6) {
        g_stdFenSegments.push_back("");
    }
}