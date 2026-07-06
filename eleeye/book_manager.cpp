// eleeye/book_manager.cpp

#include <jni.h>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <android/log.h>

#include "position.h" 

static jclass g_intArrayCls = nullptr;
static JavaVM* g_vm = nullptr;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "BookManager", __VA_ARGS__)

#define LOGE(...) __android_log_print(ANDROID_LOG_DEBUG, "BookManager", __VA_ARGS__)

// 声明全局对象（pregen.cpp 里定义了，这里 extern 引用）
extern PreGenStruct PreGen;
extern void PreGenInit(void);// 必须初始化

// 确保只初始化一次 PreGenInit()，避免重复调用
static std::once_flag g_pregen_flag;
static void EnsurePreGenInited() {
    std::call_once(g_pregen_flag, []() {
        PreGenInit();
        LOGD("After PreGenInit: zobrTable[1][51].dwLock1 = 0x%08X", PreGen.zobrTable[1][51].dwLock1);
    });
}

#pragma pack(push, 1)
struct BookEntry {
    uint32_t dwZobristLock;
    uint16_t wmv;
    uint16_t wvl;
};
#pragma pack(pop)

static std::string g_bookPath;
static std::vector<BookEntry> g_entries;
static bool g_loaded = false;
static std::mutex g_mutex;

#include "pregen.h"  // 提供 extern ZobristStruct zobrTable[14][256]; extern ZobristStruct zobrPlayer;

// 声明全局对象（pregen.cpp 里定义了，这里 extern 引用）
extern PreGenStruct PreGen;

// ---- CalcZobrist ----
static void CalcZobrist(PositionStruct& pos) {
    EnsurePreGenInited();
    pos.zobr = ZobristStruct();
    
    int xorCount = 0;
    for (int sq = 0; sq < 256; sq++) {
        int pc = (int)pos.ucpcSquares[sq];
        if (pc == 0) continue;
        
        int pt = PIECE_TYPE(pc);       // ← 直接调 eleeye 的函数，返回 0~6
        if (pc >= 32) {
            pt += 7;                   // ← 照抄 position.cpp：红方走黑方槽
        }
        // pt 范围是 7~13，完全合法！
        
        pos.zobr.Xor(PreGen.zobrTable[pt][sq]);
        xorCount++;
    }
    
    if (pos.sdPlayer != 0) {
        pos.zobr.Xor(PreGen.zobrPlayer);
    }
    
    LOGD("CalcZobrist: xorCount=%d, hash=0x%08X", xorCount, pos.zobr.dwLock1);
}

// ---------- 坐标转换 ----------
static int javaToSq(int y, int x) {
    int rank = RANK_TOP + (9 - y);   // y=9 → rank=RANK_TOP, y=0 → rank=RANK_TOP+9
    int file = FILE_LEFT + x;         // x=0 → FILE_LEFT, x=8 → FILE_LEFT+8
    return COORD_XY(file, rank);
}

static inline void sqToJava(int sq, int& y, int& x) {
    x = FILE_X(sq) - FILE_LEFT;
    y = RANK_Y(sq) - RANK_TOP;
}

// ---------- FEN → hash ----------
static uint32_t fenToLock1(const char* fen) {
    EnsurePreGenInited();
    
    LOGD("fenToLock1: input fen=%s", fen ? fen : "(null)");
    
    PositionStruct pos;
    pos.FromFen(fen);
    
    // 确认 FromFen 之后棋子还在
    int checkPc = pos.ucpcSquares[51];
    LOGD("fenToLock1: after FromFen, sq51 pc=%d(0x%02X)", checkPc, checkPc);
    
    CalcZobrist(pos);
    LOGD("fenToLock1: final hash=0x%08X", pos.zobr.dwLock1);
    return pos.zobr.dwLock1;
}

// 内部调用打开Book
static bool internalOpenBook() {
    if (g_loaded) return true;
    if (g_bookPath.empty()) {
        LOGD("internalOpenBook: no path set");
        return false;
    }
    
    FILE* f = fopen(g_bookPath.c_str(), "rb");
    if (!f) {
        LOGD("internalOpenBook: fopen failed errno=%d", errno);
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size % 8 != 0) {
        fclose(f);
        return false;
    }
    
    g_entries.resize(size / 8);
    size_t count = fread(g_entries.data(), 8, size / 8, f);
    fclose(f);
    
    if (count != (size_t)(size / 8)) return false;
    
    g_loaded = true;
    LOGD("internalOpenBook: loaded %zu entries", count);
    return true;
}
// ---------- 保存文件（原子写入）----------
static bool saveToPath(const char* path) {
    std::string tmp = std::string(path) + ".tmp";
    FILE* fp = fopen(tmp.c_str(), "wb");
    if (!fp) {
        LOGE("saveToPath: cannot open %s", tmp.c_str());
        return false;
    }
    size_t n = fwrite(g_entries.data(), sizeof(BookEntry), g_entries.size(), fp);
    fclose(fp);
    if (n != g_entries.size()) {
        remove(tmp.c_str());
        LOGE("saveToPath: fwrite incomplete %zu/%zu", n, g_entries.size());
        return false;
    }
    // 覆盖原文件
    if (rename(tmp.c_str(), path) != 0) {
        remove(tmp.c_str());
        LOGE("saveToPath: rename failed");
        return false;
    }
    return true;
}

// ============================================================
// JNI 接口
// ============================================================

extern "C" {

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeQueryBook(
    JNIEnv* env, jclass, jstring fen) {

    if (!internalOpenBook()) return env->NewStringUTF("");

    if (!g_loaded) {
        return env->NewStringUTF("");
    }

    const char* cfen = env->GetStringUTFChars(fen, nullptr);
    if (!cfen) return env->NewStringUTF("");

    // ===== 1. 算原始局面的 hash =====
    PositionStruct pos;
    pos.FromFen(cfen);
    CalcZobrist(pos);
    uint32_t hashOrig = pos.zobr.dwLock1;

    std::lock_guard<std::mutex> lock(g_mutex);

    // ===== 2. 先查原始 hash =====
    BookEntry key{hashOrig, 0, 0};
    auto range = std::equal_range(g_entries.begin(), g_entries.end(), key,
        [](const BookEntry& a, const BookEntry& b) {
            return a.dwZobristLock < b.dwZobristLock;
        });

    std::vector<BookEntry> matches;

    if (range.first != range.second) {
        // ---- 查到了，直接用，不翻转 ----
        for (auto it = range.first; it != range.second; ++it) {
            if (it->wvl > 0) matches.push_back(*it);
        }

    } else {
        // ===== 3. 没查到，尝试镜像 =====
        PositionStruct posMirror = pos;
        posMirror.Mirror();       // 内部 AddPiece 会自动重算 zobrist
        // 保险起见再算一次确保正确
        CalcZobrist(posMirror);
        uint32_t hashMirror = posMirror.zobr.dwLock1;

        BookEntry keyMirror{hashMirror, 0, 0};
        auto rangeMirror = std::equal_range(g_entries.begin(), g_entries.end(), keyMirror,
            [](const BookEntry& a, const BookEntry& b) {
                return a.dwZobristLock < b.dwZobristLock;
            });

        if (rangeMirror.first != rangeMirror.second) {
            // ---- 镜像查到了，翻转着法坐标 ----
            for (auto it = rangeMirror.first; it != rangeMirror.second; ++it) {
                if (it->wvl > 0) {
                    BookEntry e = *it;
                    e.wmv = MOVE_MIRROR(e.wmv);  // 坐标翻回用户视角
                    matches.push_back(e);
                }
            }
        }
        // 都没查到 → matches 为空，返回空字符串
    }

    // ===== 4. 按权重排序 =====
    std::sort(matches.begin(), matches.end(),
        [](const BookEntry& a, const BookEntry& b) { return a.wvl > b.wvl; });

    // ===== 5. 拼成字符串返回 =====
    std::string result;
    for (size_t i = 0; i < matches.size(); i++) {
        int fromY, fromX, toY, toX;
        sqToJava(matches[i].wmv & 0xFF, fromY, fromX);
        sqToJava(matches[i].wmv >> 8, toY, toX);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d;", fromY, fromX, toY, toX, (int)matches[i].wvl);
        result += buf;
    }

    env->ReleaseStringUTFChars(fen, cfen);
    return env->NewStringUTF(result.c_str());
}

// ---- 设置/删除着法权重 ----
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeSetBookWeight(
    JNIEnv* env, jclass, jstring fen, jint fromY, jint fromX, jint toY, jint toX, jint weight) {

    if (!internalOpenBook()) return JNI_FALSE;

    if (!g_loaded) return JNI_FALSE;

    const char* cfen = env->GetStringUTFChars(fen, nullptr);
    if (!cfen) return JNI_FALSE;

    PositionStruct pos;
    pos.FromFen(cfen);
    CalcZobrist(pos);
    uint32_t hashOrig = pos.zobr.dwLock1;

    int sqSrc = javaToSq(fromY, fromX);
    int sqDst = javaToSq(toY, toX);
    uint16_t wmv = (uint16_t)((sqDst << 8) | sqSrc);

    std::lock_guard<std::mutex> lock(g_mutex);

    // 1. 先查 H_orig
    BookEntry key{hashOrig, 0, 0};
    auto range = std::equal_range(g_entries.begin(), g_entries.end(), key,
        [](const BookEntry& a, const BookEntry& b) { return a.dwZobristLock < b.dwZobristLock; });

    for (auto it = range.first; it != range.second; ++it) {
        if (it->wmv == wmv) {
            it->wvl = (uint16_t)weight;
            LOGD("updated H_orig: hash=0x%08X wmv=0x%04X weight=%d", hashOrig, wmv, weight);
            env->ReleaseStringUTFChars(fen, cfen);
            return JNI_TRUE;
        }
    }

    // 2. H_orig 没找到 → 镜像查 H_mirror，改那里的
    PositionStruct posMirror = pos;
    posMirror.Mirror();
    CalcZobrist(posMirror);
    uint32_t hashMirror = posMirror.zobr.dwLock1;
    uint16_t wmvMirrored = MOVE_MIRROR(wmv);

    BookEntry keyMirror{hashMirror, 0, 0};
    auto rangeMirror = std::equal_range(g_entries.begin(), g_entries.end(), keyMirror,
        [](const BookEntry& a, const BookEntry& b) { return a.dwZobristLock < b.dwZobristLock; });

    for (auto it = rangeMirror.first; it != rangeMirror.second; ++it) {
        if (it->wmv == wmvMirrored) {
            it->wvl = (uint16_t)weight;
            LOGD("updated H_mirror: hash=0x%08X wmv=0x%04X weight=%d", hashMirror, wmvMirrored, weight);
            env->ReleaseStringUTFChars(fen, cfen);
            return JNI_TRUE;
        }
    }

    // 3. 都没找到 → 不 insert，返回 false（不允许往 H_orig 里写）
    LOGD("nativeSetBookWeight: not found in either H_orig or H_mirror, doing nothing");
    env->ReleaseStringUTFChars(fen, cfen);
    return JNI_FALSE;
}

// ---- 保存到文件 ----
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeSaveBook(
    JNIEnv* env, jclass) {

    if (!g_loaded) return JNI_FALSE;

    FILE* f = fopen(g_bookPath.c_str(), "wb");
    if (!f) {
        LOGD("nativeSaveBook: fopen failed, path='%s'", g_bookPath.c_str());
        return JNI_FALSE;
    }

    for (const auto& e : g_entries) {
        uint32_t hash_le = e.dwZobristLock;
        fwrite(&hash_le, 4, 1, f);
        uint16_t wmv_le = e.wmv;
        fwrite(&wmv_le, 2, 1, f);
        uint16_t wvl_le = e.wvl;
        fwrite(&wvl_le, 2, 1, f);
    }

    fclose(f);
    LOGD("nativeSaveBook: saved %zu entries to %s", g_entries.size(), g_bookPath.c_str());
    return JNI_TRUE;
}

} // extern "C"
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    
    jclass local = env->FindClass("[I");
    if (local) {
        g_intArrayCls = (jclass)env->NewGlobalRef(local);
        env->DeleteLocalRef(local);
    }
    
    LOGD("JNI_OnLoad: g_intArrayCls cached=%p", g_intArrayCls);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeSetBookPath(
    JNIEnv* env, jclass, jstring path) {
    
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    g_bookPath = cpath;
    env->ReleaseStringUTFChars(path, cpath);
    LOGD("nativeSetBookPath: stored path='%s'", g_bookPath.c_str());
}