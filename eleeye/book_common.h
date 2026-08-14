
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <android/log.h>
#include "position.h" //  关键修正：先包含position.h！它定义了PositionStruct类型
#include "book.h"     //  再包含book.h！它定义了BookEntry类型（源码里BookEntry是全局的，不在任何命名空间）

#ifndef MODULE_TAG
#error "MODULE_TAG must be defined before including book_common.h! Example: #define MODULE_TAG \"MyModule\""
#endif

// ====================== 1. 日志宏（补齐LOGW，和现有LOGE/LOGD对齐）======================
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MODULE_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, MODULE_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, MODULE_TAG, __VA_ARGS__)
// ====================== 2. 变换类型枚举（之前定义过的）======================
enum class FenTransformType
{
    SWAP_ROWS, // 交换两行
    SHIFT_COL  // 列平移
};
// ====================== 3. 变换记录结构体（之前定义过的）======================
struct FenTransform
{
    FenTransformType type;
    int param1; // SWAP_ROWS: 行A；SHIFT_COL: 平移量delta
    int param2; // SWAP_ROWS: 行B；SHIFT_COL: 无用
};
// ====================== 4. 变换列表类型别名（缺失的TransformList）======================
using TransformList = std::vector<FenTransform>; // 补这个！
// ====================== 5. 坐标宏（你之前确认过的正确版本）======================
#define SQ(x, y) ((x) << 4 | (y))    // 生成sq：x列，y行
#define COL(sq) (((sq) >> 4) & 0x0F) // 从sq取列x
#define ROW(sq) ((sq) & 0x0F)        // 从sq取行y
// ====================== 6. 公共函数声明（补齐缺失的）======================
// Java坐标转象眼SQ（替代之前的Common::JavaCoordToSq）
int JavaCoordToSq(int javaRank, int javaFile);

#pragma pack(push, 1)
struct BookEntry
{
    uint32_t dwZobristLock;
    uint16_t wmv;
    uint16_t wvl;
};
#pragma pack(pop)

// 全局变量声明（定义在book_manager.cpp中，不可使用static！）
extern std::vector<BookEntry> g_entries;
extern std::mutex g_mutex;
extern bool g_loaded;

// 函数声明（定义在book_manager.cpp中，不何使用static！）
bool internalOpenBook();
bool parseFenForEndgame(const char *fenStr, PositionStruct &pos);
bool isEndgameFen(const std::string &rawFen);
int queryEndgameMoveInternal(const std::string &rawFen, uint32_t &outHash);
// 全局FEN段落缓存（拆分后的FEN段落存在这里）
extern std::vector<std::string> g_stdFenSegments;
// 原来的void版本声明，完全不用改！
void splitFenToSegments(const std::string &fen);
