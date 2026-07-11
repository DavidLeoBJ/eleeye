#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <android/log.h>
#include "position.h"// ✅ 关键修正：先包含position.h！它定义了PositionStruct类型
#include "book.h"// ✅ 再包含book.h！它定义了BookEntry类型（源码里BookEntry是全局的，不在任何命名空间）

// 强制要求包含本头文件的模块必须先定义MODULE_TAG，否则编译报错（防漏写）
#ifndef MODULE_TAG
#error "Must define MODULE_TAG before including book_common.h (e.g. #define MODULE_TAG \"XiangqiJNI\")"
#endif

// 统一日志宏，自动带上当前模块的TAG
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MODULE_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, MODULE_TAG, __VA_ARGS__)

#pragma pack(push, 1)
struct BookEntry {
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
bool parseFenForEndgame(const char* fenStr, PositionStruct& pos);
std::string normalizeFenForEndgame(const std::string& rawFen);
bool isEndgameFen(const std::string& rawFen);
int queryEndgameMoveInternal(const std::string& rawFen, uint32_t& outHash);