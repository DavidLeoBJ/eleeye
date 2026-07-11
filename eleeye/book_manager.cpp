// eleeye/book_manager.cpp
#define MODULE_TAG "BookManager"
#include "book_common.h"
#include <jni.h>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <android/log.h>
#include <unistd.h>  // 声明fsync函数
#include "pregen.h"
#include "position.h" 

jclass g_intArrayCls = nullptr;
JavaVM* g_vm = nullptr;

extern void PreGenInit(void);// 必须初始化

std::once_flag g_pregen_flag;
void EnsurePreGenInited() {
    std::call_once(g_pregen_flag, []() {
        PreGenInit();
    });
}

std::string g_bookPath;
std::vector<BookEntry> g_entries;
bool g_loaded = false;
std::mutex g_mutex;// 全局锁定义

#include "pregen.h"  // 提供 extern ZobristStruct zobrTable[14][256]; extern ZobristStruct zobrPlayer;

// 声明全局对象（pregen.cpp 里定义了，这里 extern 引用）
extern PreGenStruct PreGen;

bool isEndgameFen(const std::string& rawFen) {
    int redPieceCount = 0;
    const char* p = rawFen.c_str();
    while (*p && *p != ' ') {
        if (*p >= 'A' && *p <= 'Z') redPieceCount++;
        p++;
    }
    return redPieceCount <=5 || strstr(rawFen.c_str(), "P3/") || strstr(rawFen.c_str(), "P4/") || strstr(rawFen.c_str(), "P5/");
}

void CalcZobrist(PositionStruct& pos) {
    EnsurePreGenInited();
    pos.zobr = ZobristStruct();
    
    int xorCount = 0;
    for (int sq = 0; sq < 256; sq++) {
        int pc = (int)pos.ucpcSquares[sq];
        if (pc == 0) continue;
        
        int pt = PIECE_TYPE(pc);       // ← 直接调 eleeye 的函数，返回 0~6
        if (pc >= 17) {
            pt += 7;                   // ← 照抄 position.cpp：红方走黑方槽
        }
        // pt 范围是 7~13，完全合法！
        
        pos.zobr.Xor(PreGen.zobrTable[pt][sq]);
        xorCount++;
    }
    
    if (pos.sdPlayer != 0) {
        pos.zobr.Xor(PreGen.zobrPlayer);
    }
}

// ---------- 坐标转换 ----------
int javaToSq(int y, int x) {
    int rank = RANK_TOP + (9 - y);   // y=9 → rank=RANK_TOP, y=0 → rank=RANK_TOP+9
    int file = FILE_LEFT + x;         // x=0 → FILE_LEFT, x=8 → FILE_LEFT+8
    return COORD_XY(file, rank);
}

inline void sqToJava(int sq, int& y, int& x) {
    x = FILE_X(sq) - FILE_LEFT;
    y = RANK_Y(sq) - RANK_TOP;
}

// ---------- FEN → hash ----------
uint32_t fenToLock1(const char* fen) {
    EnsurePreGenInited();
    
    PositionStruct pos;
    pos.FromFen(fen);
    
    int checkPc = pos.ucpcSquares[51];
    
    CalcZobrist(pos);
    return pos.zobr.dwLock1;
}

// 内部调用打开Book
bool internalOpenBook() {
    // ✅ 核心保护：已经加载过了，直接返回，绝不再读磁盘
    if (g_loaded) {
        return true;
    }
    
    g_entries.clear();
    FILE* f = fopen(g_bookPath.c_str(), "rb");
    if (!f) {
        LOGE("internalOpenBook: fopen failed! errno=%d", errno);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t entryCount = fileSize / sizeof(BookEntry);
    g_entries.reserve(entryCount);
    BookEntry e;
    for (size_t i = 0; i < entryCount; i++) {
        if (fread(&e, sizeof(BookEntry), 1, f) != 1) break;
        g_entries.push_back(e);
    }
    fclose(f);
    g_loaded = true; // ✅ 标记已加载，后续再调用直接跳过

    std::sort(g_entries.begin(), g_entries.end(),
        [](const BookEntry& a, const BookEntry& b) {
            return a.dwZobristLock < b.dwZobristLock;
        });

    return true;
}

// ---------- 保存文件（原子写入）----------
bool saveToPath(const char* path) {
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
        return false;
    }
    // 覆盖原文件
    if (rename(tmp.c_str(), path) != 0) {
        remove(tmp.c_str());
        return false;
    }
    return true;
}

/**
 * 残局同质化核心函数：将所有等价残局转换为统一的标准化FEN
 * 输入：原始残局FEN
 * 输出：标准化后的FEN（所有等价残局输出一致）
 * 注意：必须是纯函数（输入相同则输出相同），禁止任何随机逻辑
 */
std::string normalizeFenForEndgame(const std::string& rawFen) {
    std::string stdFen = rawFen;
    // ===== 当前逻辑：红车平移同质化（只处理红车锚定）=====
    size_t rPos = stdFen.find('R');
    if (rPos != std::string::npos) {
        // 解析红车当前x坐标
        size_t numStart = rPos;
        while (numStart > 0 && isdigit(stdFen[numStart - 1])) numStart--;
        std::string xStr = stdFen.substr(numStart, rPos - numStart);
        int curX = xStr.empty() ? 0 : std::stoi(xStr);
        const int baseX = 4; // 基准x=4（中路）
        if (curX != baseX) {
            // 替换FEN中的红车位置：左边baseX，右边补成9列
            std::string newLeft = std::to_string(baseX);
            std::string newRight = std::to_string(8 - baseX);
            size_t rightNumStart = rPos + 1;
            while (rightNumStart < stdFen.size() && isdigit(stdFen[rightNumStart])) rightNumStart++;
            stdFen.replace(numStart, rightNumStart - numStart, newLeft + "R" + newRight);
        }
    }
    // ===== TODO: 后续扩展点（直接在这里加，不用动上层代码）=====
    // 1. 镜像变换：if (enableMirror) { ... }
    // 2. 多兵排序：sortRedPawns(stdFen);
    // 3. 红帅锚定：if (noRedCar) { normalizeByRedKing(stdFen); }
    // 4. 九宫等价：normalizeKingInPalace(stdFen);
    LOGD("Normalize: %s → %s", rawFen.c_str(), stdFen.c_str());
    return stdFen;
}

/**
 * 残局专用简易FEN解析器：
 * 1. 只解析棋盘部分和走子方，不校验子力半场、不校验规则合法性
 * 2. 完全适配残局中子力过河、深入敌营的场景
 * 3. 子力编码完全兼容象眼原生格式（红1-7，黑17-23）
 */
bool parseFenForEndgame(const char* fenStr, PositionStruct& pos) {
    memset(&pos, 0, sizeof(PositionStruct));
    const char* p = fenStr;
    int y = 0;  // 当前行：0=黑底线，9=红底线（和FEN顺序一致）
    int x = 0;  // 当前列：0=1路，8=9路（从左到右跟踪）

    // 1. 解析棋盘部分（第一个空格前的内容）
    while (*p && *p != ' ') {
        if (*p == '/') {
            // 换行：y+1，x重置为0
            y++;
            x = 0;
            p++;
        } else if (*p >= '0' && *p <= '9') {
            // 数字：跳过对应数量的空位，x直接加数字值
            x += (*p - '0');
            p++;
        } else {
            // 子力字符：记录到当前(x,y)位置
            int sq = y * 16 + x;  // 象眼sq编码规则：y*16 + x
            if (*p >= 'A' && *p <= 'Z') {
                // 红方子力编码（和象眼原生一致）
                switch (*p) {
                    case 'K': pos.ucpcSquares[sq] = 1; break;  // 红帅
                    case 'R': pos.ucpcSquares[sq] = 2; break;  // 红车
                    case 'C': pos.ucpcSquares[sq] = 3; break;  // 红炮
                    case 'N': pos.ucpcSquares[sq] = 4; break;  // 红马
                    case 'B': pos.ucpcSquares[sq] = 5; break;  // 红相
                    case 'A': pos.ucpcSquares[sq] = 6; break;  // 红仕
                    case 'P': pos.ucpcSquares[sq] = 7; break;  // 红兵
                }
            } else {
                // 黑方子力编码（和象眼原生一致）
                switch (*p) {
                    case 'k': pos.ucpcSquares[sq] = 17; break; // 黑将
                    case 'r': pos.ucpcSquares[sq] = 18; break; // 黑车
                    case 'c': pos.ucpcSquares[sq] = 19; break; // 黑炮
                    case 'n': pos.ucpcSquares[sq] = 20; break; // 黑马
                    case 'b': pos.ucpcSquares[sq] = 21; break; // 黑象
                    case 'a': pos.ucpcSquares[sq] = 22; break; // 黑士
                    case 'p': pos.ucpcSquares[sq] = 23; break; // 黑卒
                }
            }
            x++;  // 处理完一个子力，x向右移动一格
            p++;
        }
    }

    // 2. 解析走子方（第一个空格后的'w'/'b'）
    while (*p && *p == ' ') p++;
    pos.sdPlayer = (*p == 'w') ? 0 : 1;

    // 3. 计算Zobrist哈希（和象眼原生逻辑一致，保证兼容性）
    CalcZobrist(pos);
    return true;
}

// 内部保存函数：只给C++层调用，自动加锁，不暴露给Java
// 修改后的internalSaveBook（删除内部锁）
bool internalSaveBook() {
    if (!g_loaded) return false;
    
    FILE* f = fopen(g_bookPath.c_str(), "wb");
    if (!f) {
        LOGE("internalSaveBook: fopen failed! errno=%d", errno);
        return false;
    }
    // ✅ 禁用文件缓冲，写操作直接落盘
    setvbuf(f, nullptr, _IONBF, 0);
    
    size_t written = 0;
    for (const auto& e : g_entries) {
        if (fwrite(&e, sizeof(BookEntry), 1, f) != 1) {
            fclose(f);
            return false;
        }
        written++;
    }
    
    // ✅ 强制刷到磁盘（Android缓冲的坑就在这）
    fflush(f);
    fsync(fileno(f));
    
    long fileSize = ftell(f);
    fclose(f);
    return true;
}

// ============================================================
// JNI 接口
// ============================================================

extern "C" {

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeQueryBook(
    JNIEnv* env, jclass, jstring fen) {

    if (!g_loaded) {
        internalOpenBook();
    }

    const char* cfen = env->GetStringUTFChars(fen, nullptr);
    std::string stdFen = normalizeFenForEndgame(cfen); // 残局同质化处理
    PositionStruct pos;  // ✅ 在函数开头统一声明pos，所有分支都能访问

    // 1. 判断是否为残局（简单规则，后续可优化）
    bool isEndgame = false;
    int redPieceCount = 0;
    const char* p = cfen;
    while (*p && *p != ' ') {
        if (*p >= 'A' && *p <= 'Z') {
            redPieceCount++;
        }
        p++;
    }
    // 残局特征：红方子力≤5个，或红兵出现在黑方半场（前5行）
    if (redPieceCount <= 5 || strstr(cfen, "P3/") || strstr(cfen, "P4/") || strstr(cfen, "P5/")) {
        isEndgame = true;
    }

    // 2. 残局用无校验解析器，开局用原版解析器（核心改动）
    if (isEndgame) {
        parseFenForEndgame(stdFen.c_str(), pos);  // ✅ 使用无校验简易解析器，彻底解禁子力限制
    } else {
        pos.FromFen(cfen);  // ✅ 使用原版解析器，完全兼容现有开局库
    }

    // 3. 后面的查库逻辑完全不用改（复用原有代码，不丢成果）
    uint32_t hash = pos.zobr.dwLock1;
    BookEntry key{hash, 0, 0};
    auto range = std::equal_range(g_entries.begin(), g_entries.end(), key,
        [](const BookEntry& a, const BookEntry& b) { return a.dwZobristLock < b.dwZobristLock; });
    
    size_t matchCount = std::distance(range.first, range.second);

    // --- 以下是原有的组装返回字符串的逻辑，原封不动保留 ---
    std::string result;
    for (auto it = range.first; it != range.second; ++it) {
        int srcSq = it->wmv & 0xFF;
        int dstSq = (it->wmv >> 8) & 0xFF;
        int fromY, fromX, toY, toX;
        sqToJava(srcSq, fromY, fromX);
        sqToJava(dstSq, toY, toX);
        if (!result.empty()) {
            result += ";";
        }
        result += std::to_string(fromY) + "," + std::to_string(fromX) + "," +
                  std::to_string(toY) + "," + std::to_string(toX) + "," +
                  std::to_string(it->wvl);
    }
    result += ";";

    env->ReleaseStringUTFChars(fen, cfen);
    return env->NewStringUTF(result.c_str());
}

// ---- 设置着法权重 ----
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
            env->ReleaseStringUTFChars(fen, cfen);
            return JNI_TRUE;
        }
    }

    // 3. 都没找到 → 不 insert，返回 false（不允许往 H_orig 里写）
    env->ReleaseStringUTFChars(fen, cfen);
    return JNI_FALSE;
}

// 这个函数是java层加载so时的回调函数，返回给ART，不要试图去获取它的返回值
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    
    jclass local = env->FindClass("[I");
    if (local) {
        g_intArrayCls = (jclass)env->NewGlobalRef(local);
        env->DeleteLocalRef(local);
    }
    
    // ✅ 新增1：预初始化Zobrist表（两个cpp共享，只做一次）
    PreGenInit(); 
    // ✅ 新增2：预加载开局库（只加载一次，后续查询/插入不用再加载）
    if (!g_loaded) {
        g_loaded = internalOpenBook();
    }
    // JNI_VERSION_1_6 is the minimum version required for Android NDK, so we return it to indicate success
    return JNI_VERSION_1_6;
}

// ✅ 新增3：卸载时清理全局资源，无副作用
extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return;
    
    // 清理全局类引用
    if (g_intArrayCls) {
        env->DeleteGlobalRef(g_intArrayCls);
        g_intArrayCls = nullptr;
    }
    // 卸载前保存一次，避免数据丢失
    if (g_loaded) {
        internalSaveBook();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeSetBookPath(
    JNIEnv* env, jclass, jstring path) {
    
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    g_bookPath = cpath;
    env->ReleaseStringUTFChars(path, cpath);
}


extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeInsertBookMove(
    JNIEnv* env, jclass, jstring fen, jint fromY, jint fromX, jint toY, jint toX) {
    
    if (!g_loaded) {
        if (!internalOpenBook()) {
            LOGE("FAIL: internalOpenBook failed!");
            return JNI_FALSE;
        }
    }
    
    const char* cfen = env->GetStringUTFChars(fen, nullptr);
    if (!cfen) {
        return JNI_FALSE;
    }

    std::string stdFen = normalizeFenForEndgame(cfen); // 残局同质化质化处理
    
    // ✅ 用残局专用解析器，替换原来的pos.FromFen(cfen);
    PositionStruct pos;
    if (!parseFenForEndgame(stdFen.c_str(), pos)) {
        LOGE("FAIL: parseFenForEndgame failed!");
        env->ReleaseStringUTFChars(fen, cfen);
        return JNI_FALSE;
    }
    
    // // ✅ 在解析FEN之后，计算hash之前，先找到红帅和黑将的位置
    // int redKingSq = 0, blackKingSq = 0;
    // for (int sq = 0; sq < 256; sq++) {
    //     if (pos.ucpcSquares[sq] == 1) {    // 红帅的编码是1
    //         redKingSq = sq;
    //     } else if (pos.ucpcSquares[sq] == 17) { // 黑将的编码是17
    //         blackKingSq = sq;
    //     }
    // }

    // // 然后再校验
    // if (redKingSq == 0 || blackKingSq == 0) {
    //     LOGE("FAIL: Missing king after parsing!");
    //     env->ReleaseStringUTFChars(fen, cfen);
    //     return JNI_FALSE;
    // }
    
    // 计算hash（parseFenForEndgame已经算过了，这里直接用）
    uint32_t hashOrig = pos.zobr.dwLock1;
    
    // 计算wmv（复用现有的javaToSq，坐标转换逻辑不变）
    int sqSrc = javaToSq(fromY, fromX);
    int sqDst = javaToSq(toY, toX);
    uint16_t wmv = (uint16_t)((sqDst << 8) | sqSrc);
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    // 查找当前局面的记录区间
    BookEntry key{hashOrig, 0, 0};
    auto range = std::equal_range(g_entries.begin(), g_entries.end(), key,
        [](const BookEntry& a, const BookEntry& b) { return a.dwZobristLock < b.dwZobristLock; });
    size_t entryCount = std::distance(range.first, range.second);
    
    // 检查着法是否已存在：存在则叠加权重
    // 1. 先找当前局面下是否有相同的着法
    auto it = std::find_if(range.first, range.second, [wmv](const BookEntry& e) {
        return e.wmv == wmv; // 匹配着法编码
    });

    // 2. 根据查找结果处理
    if (it != range.second) {
        // 着法已存在：叠加权重
        it->wvl += 1;
    } else {
        // 着法不存在：插入新记录
        BookEntry e{hashOrig, wmv, 1};
        // ✅ 新增：计算有序插入位置（按Zobrist哈希排序，和加载时的规则完全一致）
        auto insertIt = std::lower_bound(g_entries.begin(), g_entries.end(), e,
            [](const BookEntry& a, const BookEntry& b) {
                return a.dwZobristLock < b.dwZobristLock;
            });
        // ✅ 修改：用有序位置插入，代替原来的`range.second`
        g_entries.insert(insertIt, e);
    }

    // 3. 自动保存（同步，无死锁）
    internalSaveBook();    

    env->ReleaseStringUTFChars(fen, cfen);
    return JNI_TRUE;
}

// 对外暴露导出，待完善
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_chinesechessspectator_engine_BookManager_nativeExportBook(JNIEnv* env, jclass, jstring exportPath) {
    // 把当前内存里的g_entries写到exportPath，不影响原有的g_bookPath
    return JNI_TRUE;  
}

} // extern "C"
