#include <jni.h>
#include <string.h>
#include <stdio.h>
#include "position.h"
#include "search.h"
#include "ucci.h"
#include "pregen.h"

#ifdef __cplusplus
extern "C" {
#endif

// 辅助：着法编号 → 坐标字符串（如 "h2e2"）
static void MoveToStr(int mv, char *buf) {
    if (mv <= 0) {
        strcpy(buf, "nomove");
        return;
    }
    uint32_t dw = MOVE_COORD(mv);
    char *p = (char *)&dw;
    buf[0] = p[0];
    buf[1] = p[1];
    buf[2] = p[2];
    buf[3] = p[3];
    buf[4] = '\0';
}

// 核心搜索逻辑（给 nativeSearch 和 nativeTestSearch 共用）
static void DoSearch(const char *fenStr, int depth, char *result, int resultLen) {
    // ① 拆 FEN：只取棋盘部分，解析走子方
    char fenCopy[256];
    strncpy(fenCopy, fenStr, sizeof(fenCopy) - 1);
    fenCopy[255] = '\0';
    
    char *boardPart = strtok(fenCopy, " ");
    char *sideStr = strtok(NULL, " ");
    
    // ② 清盘 + 只解析棋盘部分
    Search.pos.ClearBoard();
    Search.pos.FromFen(boardPart);
    
    // ③ 设走子方（象眼里 0=红方，1=黑方，w 对应红方）
    if (sideStr && sideStr[0] == 'w') {
        Search.pos.sdPlayer = 0;
    } else {
        Search.pos.sdPlayer = 1;
    }
    
    // ④ 预评估！这步是关键，初始化攻击表等内部结构
    Search.pos.PreEvaluate();
    
    // ---- 以下是你原来的搜索参数设置，不动 ----
    Search.bQuit = false;
    Search.bPonder = false;
    Search.bDraw = false;
    Search.bBatch = true;
    Search.bDebug = false;
    Search.bUseHash = true;
    Search.bUseBook = false;
    Search.bNullMove = true;
    Search.bKnowledge = true;
    Search.bIdle = false;
    Search.nGoMode = GO_MODE_INFINITY;
    Search.nNodes = 0;
    Search.nCountMask = 0;
    Search.nProperTimer = 0;
    Search.nMaxTimer = 0;
    Search.nRandomMask = 0;
    Search.nBanMoves = 0;
    memset(Search.wmvBanList, 0, sizeof(Search.wmvBanList));
    Search.szBookFile[0] = '\0';

    SearchMain(depth);

    int mv = Search.mvResult;
    if (mv > 0) {
        char moveStr[8] = {0};
        MoveToStr(mv, moveStr);
        snprintf(result, resultLen, "bestmove %s", moveStr);
    } else {
        snprintf(result, resultLen, "nomove");
    }
}

// ========== 7 个 JNI 函数，严格对齐你的 Java 签名 ==========

// 1. nativeTest()
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeTest(JNIEnv *env, jobject thiz) {
    return env->NewStringUTF("XiangYu Engine v3.15 (ElephantEye) JNI OK");
}

// 2. nativeAdd(int a, int b)
JNIEXPORT jint JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeAdd(JNIEnv *env, jobject thiz, jint a, jint b) {
    return a + b;
}

// 3. nativeEvaluateStartPos()
JNIEXPORT jint JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeEvaluateStartPos(JNIEnv *env, jobject thiz) {
    PositionStruct pos;
    pos.ClearBoard();
    pos.FromFen(cszStartFen);
    pos.PreEvaluate();
    return pos.Evaluate(-MATE_VALUE, MATE_VALUE);
}

// 4. nativeTestFENParser()
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeTestFENParser(JNIEnv *env, jobject thiz) {
    PositionStruct pos;
    pos.ClearBoard();
    pos.FromFen(cszStartFen);
    char fenBuf[128] = {0};
    pos.ToFen(fenBuf);
    return env->NewStringUTF(fenBuf);
}

// 5. nativeTestMoveGenerator()
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeTestMoveGenerator(JNIEnv *env, jobject thiz) {
    PositionStruct pos;
    pos.ClearBoard();
    pos.FromFen(cszStartFen);
    MoveStruct mvs[MAX_GEN_MOVES];
    int n = pos.GenAllMoves(mvs);
    char buf[64];
    snprintf(buf, sizeof(buf), "Generated %d moves from startpos", n);
    return env->NewStringUTF(buf);
}

// 6. nativeTestSearch()
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeTestSearch(JNIEnv *env, jobject thiz) {
    char result[128] = {0};
    DoSearch(cszStartFen, 2, result, sizeof(result));
    return env->NewStringUTF(result);
}

// 7. nativeSearch(String fen, int depth) —— 核心方法
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSearch(JNIEnv *env, jobject thiz, jstring fen, jint depth) {
    const char *fenStr = env->GetStringUTFChars(fen, NULL);
    if (fenStr == NULL) {
        return env->NewStringUTF("error: null fen");
    }
    char result[128] = {0};
    DoSearch(fenStr, (int)depth, result, sizeof(result));
    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(result);
}

#ifdef __cplusplus
}
#endif
