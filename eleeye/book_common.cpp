#include "book_common.h"
#include <algorithm>

int queryEndgameMoveInternal(const std::string& rawFen, uint32_t& outHash) {
    if (!g_loaded && !internalOpenBook()) {
        LOGE("queryEndgameMoveInternal: Failed to load book!");
        return 0;
    }

    std::string stdFen = normalizeFenForEndgame(rawFen);
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