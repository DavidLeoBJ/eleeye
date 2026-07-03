#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include "pregen.h"
#include "position.h"
#include "search.h"
#include "hash.h"       // ← 加上这个，NewHash 就有了

bool g_useBook = true;   // 默认开局库开启
static char g_bookPath[1024] = "";   // ★ 全局存路径

// 不要使用全局变量，避免多线程冲突
struct SearchOutput {
    char bestmove[256] = "";
    char result[1024] = {0};
    int score;
    int mv;
    int ponderMv;
    char bestmoveStr[256];
    char ponderText[256];
    char scoreStr[128] = "";
    char mateStr[128];
};

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
    // ★ 这行必须加！否则开局库路径不对，导致开局库不起作用.
    strncpy(Search.szBookFile, g_bookPath, sizeof(Search.szBookFile) - 1);  // ★ 这行必须加！
    Search.szBookFile[sizeof(Search.szBookFile) - 1] = '\0';  // 防止字符串操作会越界读内存，可能崩溃或读取垃圾数据
    Search.bUseBook = g_useBook;   // 开局库开关由 JNI 接口控制（唯一开关）
    Search.mvPonder = 0;      // bug: 如果不清零，会导致 ponder 数据残留混乱，出现错误的着法
    ClearHash();
    return true;
}

static void DoSearch(const char *fenStr, int depth, SearchOutput *out) {    
    // 清零/清空输出
    memset(out, 0, sizeof(SearchOutput));

    if (!ResetSearch(fenStr)) {
        snprintf(out->result, sizeof(out->result), "Error: empty FEN");
        return;
    }

    SearchMain(depth);

    out->score = Search.nScore;
    out->mv = Search.mvResult;
    out->ponderMv = Search.mvPonder;    

    snprintf(out->scoreStr, sizeof(out->scoreStr), "score=%d", out->score);

    if (out->mv > 0) {
        uint32_t coord = MOVE_COORD(out->mv);
        // FIXME: 依赖小端序，严格别名违规。以后可改用位移拆字节。
        char* s = (char*)&coord;
        int fromFile = s[0] - 'a';  // 0~8, a=0, b=1, ..., i=8
        int fromRank = 9 - (s[1] - '0');  // 坐标转换
        int toFile = s[2] - 'a';
        int toRank = 9 - (s[3] - '0');  // 坐标转换

        snprintf(out->bestmoveStr, sizeof(out->bestmoveStr),
            "bestmove(%d,%d,%d,%d)",
            fromRank, fromFile, toRank, toFile);
    }  
    // 删除search.cpp:line729 Search.nScore=100; 这行，改用 nNodes 来判断是否开局库着法,更合理，避免误判。
    bool isBookMove = (Search.nNodes == 0);
    if (isBookMove && out->ponderMv > 0) {
        uint32_t pcoord = MOVE_COORD(out->ponderMv);
        // FIXME: 依赖小端序，严格别名违规。以后可改用位移拆字节。
        char* ps = (char*)&pcoord;
        int pfromFile = ps[0] - 'a';
        int pfromRank = 9 - (ps[1] - '0');
        int ptoFile = ps[2] - 'a';
        int ptoRank = 9 - (ps[3] - '0');
        snprintf(out->ponderText, sizeof(out->ponderText),
            "\nponder(%d,%d,%d,%d)", pfromRank, pfromFile, ptoRank, ptoFile);
    }

    // 杀棋判断
    if (out->score >= 9960) {
        int moves = (10000 - out->score) / 2;        
        if (moves <= 0) moves = 1;
        snprintf(out->mateStr, sizeof(out->mateStr), "红方 %d 步杀！", moves);
    } else if (out->score <= -9960) {
        int moves = (10000 + out->score) / -2;
        if (moves <= 0) moves = 1;
        snprintf(out->mateStr, sizeof(out->mateStr),"黑方 %d 步杀！", moves);
    } 
    
    if (out->mv <= 0) {
        snprintf(out->mateStr, sizeof(out->mateStr), "无合法着法，被绝杀！");
    }
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeVersion(JNIEnv* env, jclass clazz) {
    return env->NewStringUTF("ElephantEye v3.32 (象眼引擎) \n "
    "Original: 象棋百科全书网 (xqbase.com) \n "
    "Android Port: 廖延贤 (github.com/DavidLeoBJ) \n "
    "License: LGPL v2.1 \n "
    "Built: " __DATE__ " " __TIME__);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSearch(
    JNIEnv* env, jclass clazz, jstring fen, jint depth) {
    
    const char* fenStr = env->GetStringUTFChars(fen, NULL);
    
    SearchOutput out;   

    DoSearch(fenStr, depth, &out);

    if (out.bestmoveStr[0] == '\0') {
        snprintf(out.result, sizeof(out.result), "无合法着法，被绝杀!");
        env->ReleaseStringUTFChars(fen, fenStr);
        return env->NewStringUTF(out.result);
    }else{
        strncat(out.result,out.bestmoveStr,sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.ponderText[0] != '\0') {
        strncat(out.result, out.ponderText, sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.mateStr[0] != '\0') {
        strncat(out.result, out.mateStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(out.result);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeEvaluate(
    JNIEnv* env, jclass clazz, jstring fen, jint depth) {
    
    const char* fenStr = env->GetStringUTFChars(fen, NULL);   
    
    
    SearchOutput out;   

    DoSearch(fenStr, depth, &out); 

    if (out.mateStr[0] != '\0') {
        strncat(out.scoreStr, out.mateStr, sizeof(out.scoreStr) - strlen(out.scoreStr) - 1);
    }

    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(out.scoreStr);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSearchAndEvaluate(
    JNIEnv* env, jclass clazz, jstring fen, jint depth) {
    
    const char* fenStr = env->GetStringUTFChars(fen, NULL);

    
    SearchOutput out;   

    DoSearch(fenStr, depth, &out);

    if (out.bestmoveStr[0] == '\0') {
        snprintf(out.result, sizeof(out.result), "无合法着法，被绝杀!");
        env->ReleaseStringUTFChars(fen, fenStr);
        return env->NewStringUTF(out.result);
    }else{
        strncat(out.result,out.bestmoveStr,sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.ponderText[0] != '\0') {
        strncat(out.result, out.ponderText, sizeof(out.result) - strlen(out.result) - 1);
    }

    strncat(out.result, out.scoreStr, sizeof(out.result) - strlen(out.result) - 1);

    if (out.mateStr[0] != '\0') {
        strncat(out.result, out.mateStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(out.result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSetBookPath(
    JNIEnv *env, jobject obj, jstring path) {
    
    const char *pathStr = env->GetStringUTFChars(path, NULL);
    strncpy(g_bookPath, pathStr, sizeof(g_bookPath) - 1);
    g_bookPath[sizeof(g_bookPath) - 1] = '\0';  // 防止字符串操作会越界读内存，可能崩溃或读取垃圾数据
    env->ReleaseStringUTFChars(path, pathStr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSetBookEnabled(
    JNIEnv *env, jobject obj, jboolean enabled) {
    g_useBook = (enabled == JNI_TRUE);
}
