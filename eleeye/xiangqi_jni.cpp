#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include "pregen.h"
#include "position.h"
#include "search.h"
#include "hash.h"       // ← 加上这个，NewHash 就有了

bool g_useBook = true;   // 默认开局库开启
static char g_bookPath[1024] = "";   // ★ 全局存路径

static void InitEngine() {
    PreGenInit();
    NewHash(24);  // 分配哈希表，16MB
    Search.pos.FromFen("rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1");
    Search.pos.nDistance = 0;
    Search.pos.PreEvaluate();
    Search.nBanMoves = 0;
    Search.bQuit = Search.bBatch = Search.bDebug = false;
    Search.bUseHash = true;
    //Search.bUseBook = Search.bNullMove = Search.bKnowledge = g_useBook;// bUseBook 不在 initEngine 里设，由搜索函数里统一管
    Search.bNullMove = Search.bKnowledge = true;
    Search.bIdle = false;
    Search.nCountMask = 4095;
    Search.nRandomMask = 0;
    // Search.rc4Random.InitRand();  // 如果这行报错就注释掉
}

static void EnsureInited() {
    static bool inited = false;
    if (!inited) {
        InitEngine();
        inited = true;
    }
}

static bool ResetSearch(const char *fenStr) {
    if (!fenStr || strlen(fenStr) < 10) {
        return false;
    }
    EnsureInited();
    Search.pos.ClearBoard();
    Search.pos.FromFen(fenStr);
    Search.pos.nDistance = 0;
    Search.pos.PreEvaluate();
    Search.nBanMoves = 0;
    Search.nGoMode = GO_MODE_INFINITY;
    Search.nNodes = 0;
    Search.mvResult = 0;      // ← 加这行！清零
    Search.nScore = 0;        // ← 加这行！清零
    Search.bUseHash = true;
    Search.bUseBook = false;  // 开局库开关(唯一赋值点)
    ClearHash();
    return true;
}

static void DoSearch(const char *fenStr, int depth, char *result, int resultLen) {
    
    if (!ResetSearch(fenStr)) {
        snprintf(result, resultLen, "Error: empty FEN");
        return;
    }

    // // 确认一下设进去了没有
    // char dbg[64];
    // snprintf(dbg, sizeof(dbg), "\nbefore Main: bUseBook=%d", Search.bUseBook);
    // strncat(result, dbg, sizeof(result) - strlen(result) - 1);

    SearchMain(depth);
    
    int mv = Search.mvResult;
    if (mv > 0) {
        uint32_t coord = MOVE_COORD(mv);
        char* s = (char*)&coord;
        int fromFile = s[0] - 'a';  // 0~8, a=0, b=1, ..., i=8
        int fromRank = 9 - (s[1] - '0');  // 坐标转换
        int toFile = s[2] - 'a';
        int toRank = 9 - (s[3] - '0');  // 坐标转换

        snprintf(result, resultLen,
            "bestmove(%d,%d,%d,%d)",
            fromRank, fromFile, toRank, toFile);
    }
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeVersion(JNIEnv* env, jclass clazz) {
    return env->NewStringUTF("ElephantEye v3.3 | Android JNI | Built: " __DATE__ " " __TIME__ " | Book: enabled");
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSearch(
    JNIEnv* env, jclass clazz, jstring fen, jint depth) {
    
    const char* fenStr = env->GetStringUTFChars(fen, NULL);

    char result[256] = {0};
    DoSearch(fenStr, depth, result, sizeof(result));
    int ponderMv = Search.mvPonder;
    if (ponderMv != 0) {
        uint32_t pcoord = MOVE_COORD(ponderMv);
        char* ps = (char*)&pcoord;
        int pfromFile = ps[0] - 'a';
        int pfromRank = 9 - (ps[1] - '0');
        int ptoFile = ps[2] - 'a';
        int ptoRank = 9 - (ps[3] - '0');
        char ponderText[128];
        snprintf(ponderText, sizeof(ponderText),
            "\nponder(%d,%d,%d,%d)", pfromRank, pfromFile, ptoRank, ptoFile);
        strncat(result, ponderText, sizeof(result) - strlen(result) - 1);
    }

    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(result);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeEvaluate(
    JNIEnv* env, jclass clazz, jstring fen, jint depth) {
    
    const char* fenStr = env->GetStringUTFChars(fen, NULL);
    char result[1024] = {0};   // ← 一定要初始化！

    if (!ResetSearch(fenStr)) {
        snprintf(result, sizeof(result), "Error: empty FEN");
        env->ReleaseStringUTFChars(fen, fenStr);
        return env->NewStringUTF(result);
    }

    // ★ 用 SearchMain(1) 代替直接调 Evaluate
    SearchMain(depth);

    int score = Search.nScore;
    env->ReleaseStringUTFChars(fen, fenStr);

    snprintf(result, sizeof(result), "score=%d", score);
    return env->NewStringUTF(result);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSearchAndEvaluate(
    JNIEnv* env, jclass clazz, jstring fen, jint depth) {
    // 先返回一个简单的字符串，表示JNI调用成功
    //return env->NewStringUTF("SO_UPDATED_OK");

    const char* fenStr = env->GetStringUTFChars(fen, NULL);
    char result[4096] = {0};

    if (!ResetSearch(fenStr)) {
        snprintf(result, sizeof(result), "Error: empty FEN");
        env->ReleaseStringUTFChars(fen, fenStr);
        return env->NewStringUTF(result);
    }
    // ★ 这行必须加！否则开局库路径不对，导致开局库不起作用.
    strncpy(Search.szBookFile, g_bookPath, sizeof(Search.szBookFile) - 1);  // ★ 这行必须加！

    Search.bUseBook = true; //开局库开关

    SearchMain(depth);

    int score = Search.nScore;
    int ponderMv = Search.mvPonder;   // ← 现在能用了！修改源代码后增加了ponder输出
    int mv = Search.mvResult;

    if (mv != 0) {
        // ★ 必须用 MOVE_COORD 转换！
        uint32_t coord = MOVE_COORD(mv);
        char* s = (char*)&coord;
        int bfromFile = s[0] - 'a';
        int bfromRank = 9 - (s[1] - '0');
        int btoFile = s[2] - 'a';
        int btoRank = 9 - (s[3] - '0');

        // ponder：有才解析，然后拼装成字符串ponderText，最后拼接到result中
        char ponderText[256] = "";
        if (ponderMv != 0) {
            uint32_t pcoord = MOVE_COORD(ponderMv);
            char* ps = (char*)&pcoord;
            int pfromFile = ps[0] - 'a';
            int pfromRank = 9 - (ps[1] - '0');
            int ptoFile = ps[2] - 'a';
            int ptoRank = 9 - (ps[3] - '0');
            snprintf(ponderText, sizeof(ponderText),
                "\nponder(%d,%d,%d,%d)", pfromRank, pfromFile, ptoRank, ptoFile);
        }

        //把pvDebug数组的内容也加入到result中，方便调试
        char pvStr[128];
        snprintf(pvStr, sizeof(pvStr),
            "\n[pv before: %d,%d,%d,%d,%d] [pv after: %d,%d,%d,%d,%d]",
            g_pvDebug[0], g_pvDebug[1], g_pvDebug[2], g_pvDebug[3], g_pvDebug[4],
            g_pvDebug[5], g_pvDebug[6], g_pvDebug[7], g_pvDebug[8], g_pvDebug[9]);
        //strncat(result, pvStr, sizeof(result) - strlen(result) - 1);

        //把开局库路径也加入到result中，方便调试
        char bookDebug[512];
        snprintf(bookDebug, sizeof(bookDebug),
            "\n[bookPath: %s]\n[szBookFile: %s]",
            g_bookPath, Search.szBookFile);
        //strncat(result, bookDebug, sizeof(result) - strlen(result) - 1);

        // 打印20步杀
        char mateStr[128];
        if (score >= 9960) {
            int moves = (10000 - score) / 2;
            if (moves <= 0) moves = 1;
            snprintf(mateStr, sizeof(mateStr),
                "score=%d\nbestmove(%d,%d,%d,%d)\n%s\n红方 %d 步杀！",
                score, bfromRank, bfromFile, btoRank, btoFile, ponderText, moves);
            strncat(result, mateStr, sizeof(result) - strlen(result) - 1);
        } else if (score <= -9960) {
            int moves = (10000 + score) / -2;
            if (moves <= 0) moves = 1;
            snprintf(mateStr, sizeof(mateStr),
                "score=%d\nbestmove(%d,%d,%d,%d)%s\n黑方 %d 步杀！",
                score, bfromRank, bfromFile, btoRank, btoFile, ponderText,  moves);
            strncat(result, mateStr, sizeof(result) - strlen(result) - 1);
        } else {
            snprintf(mateStr, sizeof(mateStr),
                "score=%d\nbestmove(%d,%d,%d,%d)%s",
                score, bfromRank, bfromFile, btoRank, btoFile, ponderText);
            strncat(result, mateStr, sizeof(result) - strlen(result) - 1);  
        }
    } else {
        char check_mateStr[128];
        if (fenStr[0] == 'w' || fenStr[0] == 'W') {
            snprintf(check_mateStr, sizeof(check_mateStr), "score=%d\n红方被绝杀！", score);
        } else {
            snprintf(check_mateStr, sizeof(check_mateStr), "score=%d\n黑方被绝杀！", score);
        }
        strncat(result, check_mateStr, sizeof(result) - strlen(result) - 1);
    }

    // char dbg[128];
    // snprintf(dbg, sizeof(dbg), "\nnBookMoves=%d", g_pvDebug[0]);
    // strncat(result, dbg, sizeof(result) - strlen(result) - 1);

    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSetBookPath(
    JNIEnv *env, jobject obj, jstring path) {
    
    const char *pathStr = env->GetStringUTFChars(path, NULL);
    strncpy(g_bookPath, pathStr, sizeof(g_bookPath) - 1);
    env->ReleaseStringUTFChars(path, pathStr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSetBookEnabled(
    JNIEnv *env, jobject obj, jboolean enabled) {
    g_useBook = (enabled == JNI_TRUE);
}
