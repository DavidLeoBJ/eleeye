#include <jni.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ========== 象眼头文件 ==========
#include "position.h"
#include "search.h"
#include "pregen.h"

// ========== 全局声明 ==========
//extern "C" void PreGen(void);

SearchStruct Search;

// ========== 核心搜索逻辑 ==========
static void DoSearch(const char *fenStr, int depth, char *result, int resultLen) {
    
    static bool inited = false;
    if (!inited) {
        PreGenInit();
        inited = true;
    }

    // 清盘 + 完整FEN导入
    Search.pos.ClearBoard();
    Search.pos.FromFen(fenStr);
    Search.pos.PreEvaluate();

    // 搜索参数
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

    // 开搜
    SearchMain(depth);

    int mv = Search.mvResult;
    if (mv > 0) {
        char moveStr[8] = {0};
        MoveToStr(mv, moveStr);   // 得到 "h2e2" 这种 ICCS 串

        // ICCS -> 列(X), 行(Y)
        int fx = moveStr[0] - 'a';
        int fy = moveStr[1] - '0';
        int tx = moveStr[2] - 'a';
        int ty = moveStr[3] - '0';

        // 局面评分（红方视角，±100 = 1兵）
        int score = Search.pos.Evaluate(-MATE_VALUE, MATE_VALUE);

        snprintf(result, resultLen,
            "AI建议: (%d,%d) -> (%d,%d)\n评估分数: %d\n搜索节点数: %lld",
            fy, fx, ty, tx, score, (long long)Search.nNodes);
    } else {
        snprintf(result, resultLen,
            "AI建议: (-1,-1) -> (-1,-1)\n评估分数: 0\n搜索节点数: 0");
    }
}

// ========== JNI: nativeTest ==========
extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_appb_ChessEngine_nativeTest(JNIEnv *env, jobject thiz) {
    return env->NewStringUTF("XiangYu Engine v3.15 (ElephantEye) JNI OK");
}

// ========== JNI: nativeSearch ==========
extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_appb_ChessEngine_nativeSearch(JNIEnv *env, jobject thiz, jstring fen, jint depth) {
    const char *fenStr = env->GetStringUTFChars(fen, NULL);

    char result[512] = {0};
    DoSearch(fenStr, depth, result, sizeof(result));

    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(result);
}
