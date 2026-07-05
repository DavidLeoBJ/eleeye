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
static inline int javaToSq(int y, int x) {
    return COORD_XY(x + FILE_LEFT, y + RANK_TOP);
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

// ---- 打开开局库 ----
JNIEXPORT jboolean JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeOpenBook(
    JNIEnv* env, jclass, jstring path) {
    // ===== 确诊 =====
    LOGD("PreGen.zobrTable[1][51].dwLock1 = %u", PreGen.zobrTable[1][51].dwLock1);
    LOGD("PreGen.zobrTable[0][0].dwLock1 = %u", PreGen.zobrTable[0][0].dwLock1);
    LOGD("PreGen.zobrPlayer.dwLock1 = %u", PreGen.zobrPlayer.dwLock1);
    // ================

    const char* cp = env->GetStringUTFChars(path, nullptr);
    if (!cp) return JNI_FALSE;
    LOGD("nativeOpenBook: %s", cp);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
    g_loaded = false;

    FILE* fp = fopen(cp, "rb");
    if (!fp) {
        LOGE("nativeOpenBook: fopen failed");
        env->ReleaseStringUTFChars(path, cp);
        return JNI_FALSE;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz % sizeof(BookEntry) != 0) {
        LOGE("nativeOpenBook: invalid size %ld", sz);
        fclose(fp);
        env->ReleaseStringUTFChars(path, cp);
        return JNI_FALSE;
    }

    size_t count = (size_t)(sz / sizeof(BookEntry));
    g_entries.resize(count);
    size_t readn = fread(g_entries.data(), sizeof(BookEntry), count, fp);
    fclose(fp);
    env->ReleaseStringUTFChars(path, cp);

    if (readn != count) {
        LOGE("nativeOpenBook: read %zu expected %zu", readn, count);
        g_entries.clear();
        return JNI_FALSE;
    }

    // 确保有序
    std::sort(g_entries.begin(), g_entries.end(),
        [](const BookEntry& a, const BookEntry& b) {
            if (a.dwZobristLock != b.dwZobristLock) return a.dwZobristLock < b.dwZobristLock;
            return a.wmv < b.wmv;
        });

    g_loaded = true;
    LOGD("nativeOpenBook: loaded %zu entries, first hash=0x%08X",
         g_entries.size(), g_entries.empty() ? 0 : g_entries[0].dwZobristLock);
    return JNI_TRUE;
}

// extern "C" JNIEXPORT jstring JNICALL
// Java_com_example_chinesechessspectator_engine_BookManager_nativeQueryBook(
//     JNIEnv* env, jclass, jstring fen) {
//     return env->NewStringUTF("hello");
// }

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeQueryBook(
    JNIEnv* env, jclass, jstring fen) {

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
JNIEXPORT jboolean JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeSetBookWeight(
    JNIEnv* env, jclass, jstring fen, jint fromY, jint fromX, jint toY, jint toX, jint weight) {

    if (!g_loaded) return JNI_FALSE;

    const char* cfen = env->GetStringUTFChars(fen, nullptr);
    if (!cfen) return JNI_FALSE;
    uint32_t hash = fenToLock1(cfen);
    env->ReleaseStringUTFChars(fen, cfen);

    int sqSrc = javaToSq(fromY, fromX);
    int sqDst = javaToSq(toY, toX);
    uint16_t move = (uint16_t)(sqSrc + (sqDst << 8));

    std::lock_guard<std::mutex> lock(g_mutex);

    // 二分查找精确位置
    BookEntry key{hash, move, 0};
    auto it = std::lower_bound(g_entries.begin(), g_entries.end(), key,
        [](const BookEntry& a, const BookEntry& b) {
            if (a.dwZobristLock != b.dwZobristLock) return a.dwZobristLock < b.dwZobristLock;
            return a.wmv < b.wmv;
        });

    // 找到了已有记录
    if (it != g_entries.end() && it->dwZobristLock == hash && it->wmv == move) {
        if (weight > 0) {
            it->wvl = (uint16_t)weight;
        } else {
            g_entries.erase(it); // 权重<=0 删除
        }
        return JNI_TRUE;
    }

    // 没找到，新增（保持有序）
    if (weight > 0) {
        g_entries.insert(it, {hash, move, (uint16_t)weight});
    }
    return JNI_TRUE;
}

// ---- 保存到文件 ----
JNIEXPORT jboolean JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeSaveBook(
    JNIEnv* env, jclass, jstring path) {

    if (!g_loaded) return JNI_FALSE;

    std::lock_guard<std::mutex> lock(g_mutex);

    // 排序 + 去重（保留权重较大的）
    std::sort(g_entries.begin(), g_entries.end(),
        [](const BookEntry& a, const BookEntry& b) {
            if (a.dwZobristLock != b.dwZobristLock) return a.dwZobristLock < b.dwZobristLock;
            if (a.wmv != b.wmv) return a.wmv < b.wmv;
            return a.wvl > b.wvl; // 权重大的排前面
        });
    // 去重：相同 hash+move 只保留第一个（权重最大的）
    g_entries.erase(std::unique(g_entries.begin(), g_entries.end(),
        [](const BookEntry& a, const BookEntry& b) {
            return a.dwZobristLock == b.dwZobristLock && a.wmv == b.wmv;
        }), g_entries.end());

    const char* cp = env->GetStringUTFChars(path, nullptr);
    if (!cp) return JNI_FALSE;
    bool ok = saveToPath(cp);
    env->ReleaseStringUTFChars(path, cp);
    return ok ? JNI_TRUE : JNI_FALSE;
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