#define MODULE_TAG "Xiangqi_JNI"
#include <jni.h>
#include <android/log.h>
#include <string.h>
#include <stdlib.h>
#include "pregen.h"
#include "position.h"
#include "search.h"
#include "hash.h" // ← 加上这个，NewHash 就有了
#include "book_common.h"
#include "book_endgame_normalizer.h"

// 外部函数声明 book_manager.cpp中定义专门为引擎残局命中用的
extern "C" bool InternalQueryBookHit(const char *);

bool g_useBook = true;             // 默认开局库开启
static char g_bookPath[1024] = ""; // 全局开局库保存路径

// 不要使用全局变量，避免多线程冲突
struct SearchOutput
{
    char bestmove[256] = "";
    char result[2048] = {0};
    int score;
    int mv;
    int ponderMv;
    char bestmoveStr[256];
    char ponderText[256];
    char scoreStr[128] = "";
    char mateStr[128];
    int bookHit = 0;
    char searchmodeStr[128];
    char nextmoveStr[1024] = {0};
};

static void InitEngine()
{
    PreGenInit();
    NewHash(24); // 分配哈希表，16MB
    Search.pos.FromFen("rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1");
    Search.pos.nDistance = 0;
    Search.pos.PreEvaluate();
    Search.nBanMoves = 0;
    Search.bQuit = Search.bBatch = Search.bDebug = false;
    Search.bUseHash = true;
    // Search.bUseBook = Search.bNullMove = Search.bKnowledge = g_useBook;// bUseBook 不在 initEngine 里设，由搜索函数里统一管
    Search.bNullMove = Search.bKnowledge = true;
    Search.bIdle = false;
    Search.nCountMask = 4095;
    Search.nRandomMask = 0;
}

static void EnsureInited()
{
    static bool inited = false;
    if (!inited)
    {
        InitEngine();
        inited = true;
    }
}

static bool ResetSearch(const char *fenStr)
{
    if (!fenStr || strlen(fenStr) < 10)
    {
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
    Search.mvResult = 0; // ← 加这行！清零
    Search.nScore = 0;   // ← 加这行！清零
    Search.bUseHash = true;
    // 这行必须加！否则开局库路径不对，导致开局库不起作用.
    strncpy(Search.szBookFile, g_bookPath, sizeof(Search.szBookFile) - 1); // 这行必须加！
    Search.szBookFile[sizeof(Search.szBookFile) - 1] = '\0';               // 防止字符串操作会越界读内存，可能崩溃或读取垃圾数据
    Search.bUseBook = g_useBook;                                           // 开局库开关由 JNI 接口控制（唯一开关）
    Search.mvPonder = 0;
    Search.bookHit = 0;
    ClearHash();
    return true;
}
// 拼装结果
static void OutPut(SearchOutput *out)
{
    out->score = Search.nScore;
    out->mv = Search.mvResult;
    out->ponderMv = Search.mvPonder;
    out->bookHit = Search.bookHit;

    snprintf(out->scoreStr, sizeof(out->scoreStr), "score:%d", out->score);

    if (out->mv > 0)
    {
        uint32_t coord = MOVE_COORD(out->mv);
        // FIXME: 依赖小端序，严格别名违规。以后可改用位移拆字节。
        char *s = (char *)&coord;
        int fromFile = s[0] - 'a';       // 0~8, a=0, b=1, ..., i=8
        int fromRank = 9 - (s[1] - '0'); // 坐标转换
        int toFile = s[2] - 'a';
        int toRank = 9 - (s[3] - '0'); // 坐标转换

        snprintf(out->bestmoveStr, sizeof(out->bestmoveStr),
                 "bestmove:%d%d%d%d",
                 fromRank, fromFile, toRank, toFile);
    }
    // 删除search.cpp:line729 Search.nScore=100; 改用 bookHit 来判断是否开局库着法,更合理，避免误判。
    if (out->bookHit == 1 && out->ponderMv > 0)
    {
        uint32_t pcoord = MOVE_COORD(out->ponderMv);
        // FIXME: 依赖小端序，严格别名违规。以后可改用位移拆字节。
        char *ps = (char *)&pcoord;
        int pfromFile = ps[0] - 'a';
        int pfromRank = 9 - (ps[1] - '0');
        int ptoFile = ps[2] - 'a';
        int ptoRank = 9 - (ps[3] - '0');
        snprintf(out->ponderText, sizeof(out->ponderText),
                 "ponder:%d%d%d%d", pfromRank, pfromFile, ptoRank, ptoFile);
    }

    switch (out->bookHit)
    {
    case 0: // 搜索
        snprintf(out->searchmodeStr, sizeof(out->searchmodeStr), "MODE:SEARCH");
        break;
    case 1: // 命中
        snprintf(out->searchmodeStr, sizeof(out->searchmodeStr), "MODE:BOOKHIT");
        break;
    case 2: // 递进搜索
        snprintf(out->searchmodeStr, sizeof(out->searchmodeStr), "MODE:NEXTSTEP");
        break;
    default:
        snprintf(out->searchmodeStr, sizeof(out->searchmodeStr), "MODE:UNKNOWN");
    }
    // nextmoveStr拼装
    if (Search.nScore >= 9960 || Search.nScore <= -9960 || out->score >= 9960 || out->score <= -9960)
    {
        int len = NextStep_GetPvCount(), mvResult = 0;
        if (len > 0)
        {
            Search.mvResult = NextStep_GetPv(0); // 从缓存中获取最佳走法
            for (int i = 1; i < len && NextStep_GetPv(i) > 0; i++)
            {
                mvResult = NextStep_GetPv(i); // 从缓存中获取最佳走法
                char tmp[5];
                mv4(mvResult, tmp);
                if (out->nextmoveStr[0] == '\0')
                {
                    strcat(out->nextmoveStr, "nextmove:");
                    strcat(out->nextmoveStr, tmp);
                }
                else
                {
                    strcat(out->nextmoveStr, "|");
                    strcat(out->nextmoveStr, tmp);
                }
            }
        }
    }
    // 杀棋判断
    if (out->score >= 9960)
    {
        int moves = (10000 - out->score - g_cnt) / 2;
        if (moves <= 0)
            moves = 0;
        // 0:红方，1:黑方:如果是红方走棋0正分杀则为红杀黑:R/d,如果是黑方走棋1正分杀则为黑杀红:B/d
        snprintf(out->mateStr, sizeof(out->mateStr), "mate:%c/%d", Search.pos.sdPlayer ? 'B' : 'R', moves);
    }
    else if (out->score <= -9960)
    {
        int moves = (10000 + out->score - g_cnt) / 2;
        if (moves <= 0)
            moves = 0;
        // 0:红方，1:黑方:如果是红方走棋0负分杀则为黑杀红B/d,如果是黑方走棋1负分杀则为红杀黑R/d
        snprintf(out->mateStr, sizeof(out->mateStr), "mate:%c/%d", Search.pos.sdPlayer ? 'R' : 'B', moves);
    }
    // 无着法判断：该谁走谁输，如Search.pos.sdPlayer=0，则黑杀红,返回：B/0；如Search.pos.sdPlayer=1，则红杀黑,返回：R/0
    if (out->mv <= 0)
    {
        snprintf(out->mateStr, sizeof(out->mateStr), "mate:%s/0", Search.pos.sdPlayer ? "B" : "R"); // 0:已经绝杀
    }
}
static void DoSearch(const char *fenStr, int depth, SearchOutput *out)
{

    // 清零/清空输出
    memset(out, 0, sizeof(SearchOutput));
    NextStep_Init(depth); // 原始搜索中进行递进搜索初始化
    if (!ResetSearch(fenStr))
    {
        snprintf(out->result, sizeof(out->result), "Error: empty FEN");
        return;
    }
    if (InternalQueryBookHit(fenStr))
    {
        LOGE("残局命中!");
        Search.bookHit = 1;
    }
    else
    {
        SearchMain(depth);
    }
    OutPut(out);
}

static void NextStep(SearchOutput *out)
{
    Search.nGoMode = GO_MODE_INFINITY;
    Search.mvResult = 0;
    Search.mvPonder = 0;
    Search.bUseHash = true;
    Search.bookHit = 2; // 表示NextStep模式
    memset(out, 0, sizeof(SearchOutput));
    char curFen[256];
    Search.pos.ToFen(curFen);         // 生成当前局面 fen
    if (InternalQueryBookHit(curFen)) // 残局书
    {
        LOGE("残局命中!");
        Search.bookHit = 1;
    }
    else
    {
        int moves = (10000 - abs(Search.nScore) - g_cnt) / 2;
        if (moves > 0) // 如果有绝杀就不要再搜索，不再维护局面
        {
            //  原子深度+用户修改正深度重搜，确保搜索质量。速度：用户已经在精度与速度之前权衡，加之随着局面递进一定会趋于简化，速度自动递增。
            NextStep_Refresh();

            // 从缓存中获取着法，直到取完再搜索
            // NextStep_GetnextPv();
        }
    }
    OutPut(out);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ElephantEye_nativeVersion(JNIEnv *env, jclass clazz)
{
    return env->NewStringUTF("ElephantEye v3.32 \n "
                             "Original: github.com/xqbase/eleeye \n "
                             "Android Port: 乐意傲 (github.com/DavidLeoBJ) \n "
                             "License: LGPL v2.1 \n "
                             "Built: " __DATE__ " " __TIME__);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ElephantEye_nativeSearch(
    JNIEnv *env, jclass clazz, jstring fen, jint depth)
{

    const char *fenStr = env->GetStringUTFChars(fen, NULL);

    SearchOutput out;
    DoSearch(fenStr, depth, &out);

    if (out.bestmoveStr[0] == '\0')
    {
        snprintf(out.result, sizeof(out.result), "mate:%s/0", Search.pos.sdPlayer ? "B" : "R");
        env->ReleaseStringUTFChars(fen, fenStr);
        return env->NewStringUTF(out.result);
    }
    else
    {
        strncat(out.result, out.bestmoveStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.ponderText[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.ponderText, sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.mateStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.mateStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.nextmoveStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.nextmoveStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.searchmodeStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.searchmodeStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(out.result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ElephantEye_nativeEvaluate(
    JNIEnv *env, jclass clazz, jstring fen, jint depth)
{

    const char *fenStr = env->GetStringUTFChars(fen, NULL);

    SearchOutput out;

    DoSearch(fenStr, depth, &out);

    if (out.mateStr[0] != '\0')
    {
        strncat(out.scoreStr, out.mateStr, sizeof(out.scoreStr) - strlen(out.scoreStr) - 1);
    }

    if (out.searchmodeStr[0] != '\0')
    {
        strncat(out.scoreStr, ",", sizeof(out.scoreStr) - strlen(out.scoreStr) - 1);
        strncat(out.scoreStr, out.searchmodeStr, sizeof(out.scoreStr) - strlen(out.scoreStr) - 1);
    }
    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(out.scoreStr);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ElephantEye_nativeSearchAndEvaluate(
    JNIEnv *env, jclass clazz, jstring fen, jint depth)
{

    const char *fenStr = env->GetStringUTFChars(fen, NULL);
    SearchOutput out;

    DoSearch(fenStr, depth, &out);

    if (out.bestmoveStr[0] == '\0')
    {
        snprintf(out.result, sizeof(out.result), "mate:%s/0", Search.pos.sdPlayer ? "B" : "R");
        env->ReleaseStringUTFChars(fen, fenStr);
        return env->NewStringUTF(out.result);
    }
    else
    {
        strncat(out.result, out.bestmoveStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.ponderText[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.ponderText, sizeof(out.result) - strlen(out.result) - 1);
    }
    strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
    strncat(out.result, out.scoreStr, sizeof(out.result) - strlen(out.result) - 1);

    if (out.mateStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.mateStr, sizeof(out.result) - strlen(out.result) - 1);
    }
    if (out.nextmoveStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.nextmoveStr, sizeof(out.result) - strlen(out.result) - 1);
    }
    if (out.searchmodeStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.searchmodeStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(out.result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ElephantEye_nativeSetBookPath(
    JNIEnv *env, jobject obj, jstring path)
{

    const char *pathStr = env->GetStringUTFChars(path, NULL);
    strncpy(g_bookPath, pathStr, sizeof(g_bookPath) - 1);
    g_bookPath[sizeof(g_bookPath) - 1] = '\0'; // 防止字符串操作会越界读内存，可能崩溃或读取垃圾数据
    env->ReleaseStringUTFChars(path, pathStr);
}

extern "C" JNIEXPORT bool JNICALL
Java_com_example_chinesechessspectator_engine_ElephantEye_nativeSetBookEnabled(
    JNIEnv *env, jobject obj, jboolean enabled)
{
    g_useBook = (enabled == JNI_TRUE);
    return g_useBook;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ElephantEye_nativeNextStep(
    JNIEnv *env, jclass, jint revise)
{

    if (g_base == 0)
    {
        return env->NewStringUTF("mode:need_init");
    }

    g_revise = revise;

    SearchOutput out;

    NextStep(&out);

    if (out.bestmoveStr[0] == '\0')
    {
        snprintf(out.result, sizeof(out.result), "mate:%s/0", Search.pos.sdPlayer ? "B" : "R");
        return env->NewStringUTF(out.result);
    }
    else
    {
        strncat(out.result, out.bestmoveStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.ponderText[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.ponderText, sizeof(out.result) - strlen(out.result) - 1);
    }
    strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
    strncat(out.result, out.scoreStr, sizeof(out.result) - strlen(out.result) - 1);

    if (out.mateStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.mateStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    if (out.nextmoveStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.nextmoveStr, sizeof(out.result) - strlen(out.result) - 1);
    }
    if (out.searchmodeStr[0] != '\0')
    {
        strncat(out.result, ",", sizeof(out.result) - strlen(out.result) - 1);
        strncat(out.result, out.searchmodeStr, sizeof(out.result) - strlen(out.result) - 1);
    }

    return env->NewStringUTF(out.result);
}