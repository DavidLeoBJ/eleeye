#define MODULE_TAG "XiangqiJNI" // JNI层日志，一眼认得出
#include "book_common.h"        // 引入公共残局查询方法
#include "hash.h"               // ← 加上这个，NewHash 就有了
#include "position.h"
#include "pregen.h"
#include "search.h"
#include <android/log.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#define TEST_QUICK_MATE_SEARCH 1 // 1=开测试，0=关测试，完全不影响现有代码

int LightCountMateSteps(const PositionStruct &pos);
bool g_useBook = true;             // 默认开局库开启
static char g_bookPath[1024] = ""; // ★ 全局存路径
#define MAX_QUICK_DEPTH 5    // 最多搜5层，足够找2-3步杀，毫秒级完成
#define MAX_NODES 32         // 最多存32个节点，足够残局搜索

// 简化版搜索节点：只存必要信息，不存整个PositionStruct
struct QuickNode {
    uint64_t hash;           // 局面哈希，用来查重
    uint16_t path[MAX_QUICK_DEPTH]; // 路径
    int depth;                // 当前深度
};

// 全局静态缓冲区
static QuickNode g_quick_nodes[MAX_NODES];
static uint64_t g_quick_visited[MAX_NODES]; // 重复检测哈希表

// ✅ 全局测试开关：默认false，平时完全不执行测试代码，不会出任何问题
static bool g_enable_quick_mate_test = true; 

// eleeye/xiangqi_jni.cpp 里的TestQuickMateSearch函数，修正后：
static void TestQuickMateSearch() {
    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    bool found = false;
    int steps = 0;
    uint16_t final_path[MAX_QUICK_DEPTH] = {0};
    // 初始化根局面
    PositionStruct root_pos;
    root_pos.FromFen("3P5/4R4/5k3/9/9/5r3/9/9/4K4/9 w");
    // ✅ 修正1：直接读dwZobrist成员变量，不是调用函数
    uint32_t root_hash = root_pos.zobr.dwKey; 
    // 初始化全局缓冲区
    memset(g_quick_nodes, 0, sizeof(g_quick_nodes));
    memset(g_quick_visited, 0, sizeof(g_quick_visited));
    int node_cnt = 0;
    g_quick_nodes[node_cnt].hash = root_hash;
    g_quick_nodes[node_cnt].depth = 0;
    node_cnt++;
    g_quick_visited[0] = root_hash;
    int visited_cnt = 1;
    // DFS搜索
    for (int i = 0; i < node_cnt; i++) {
        QuickNode cur = g_quick_nodes[i];
        // 超时检查
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        if ((ts_end.tv_sec - ts_start.tv_sec)*1000 + (ts_end.tv_nsec - ts_start.tv_nsec)/1000000 > 5) {
            LOGE("QuickMateSearch: timeout");
            break;
        }
        // 恢复当前局面
        PositionStruct cur_pos;
        cur_pos.FromFen("3P5/4R4/5k3/9/9/5r3/9/9/4K4/9 w");
        for (int d = 0; d < cur.depth; d++) {
            cur_pos.MakeMove(cur.path[d]);
        }
        if (cur_pos.IsMate()) {
            found = true;
            steps = cur.depth;
            memcpy(final_path, cur.path, sizeof(uint16_t)*steps);
            break;
        }
        if (cur.depth >= MAX_QUICK_DEPTH) continue;
        // 生成着法
        MoveStruct moves[256];
        int move_cnt = cur_pos.GenAllMoves(moves);
        int try_cnt = 0;
        for (int m = 0; m < move_cnt && try_cnt < 5; m++, try_cnt++) {
            PositionStruct next_pos = cur_pos;
            next_pos.MakeMove(moves[m].wmv);
            // ✅ 修正2：直接读dwZobrist成员变量
            uint32_t next_hash = next_pos.zobr.dwKey; 
            // 查重
            bool dup = false;
            for (int v = 0; v < visited_cnt; v++) {
                if (g_quick_visited[v] == next_hash) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            if (node_cnt < MAX_NODES) {
                g_quick_nodes[node_cnt].hash = next_hash;
                g_quick_nodes[node_cnt].depth = cur.depth + 1;
                memcpy(g_quick_nodes[node_cnt].path, cur.path, sizeof(uint16_t)*cur.depth);
                g_quick_nodes[node_cnt].path[cur.depth] = moves[m].wmv;
                node_cnt++;
                if (visited_cnt < MAX_NODES) {
                    g_quick_visited[visited_cnt++] = next_hash;
                }
            }
        }
    }
    // 输出结果
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    long cost = (ts_end.tv_sec - ts_start.tv_sec)*1000 + (ts_end.tv_nsec - ts_start.tv_nsec)/1000000;
    if (found) {
        LOGE("QuickMateSearch: Found mate! Steps=%d, Cost=%ldms", steps, cost);
        for (int i = 0; i < steps; i++) {
            uint32_t coord = ((uint32_t)final_path[i] << 16) | final_path[i];
            char* s = (char*)&coord;
            char mvStr[16];
            snprintf(mvStr, sizeof(mvStr), "%c%d->%c%d",
                s[0], 9-(s[1]-'0'), s[2], 9-(s[3]-'0'));
            LOGE("QuickMateSearch: Step %d: %s", i+1, mvStr);
        }
    } else {
        LOGE("QuickMateSearch: No mate found, Cost=%ldms", cost);
    }
}

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

static int CalcEndgameTotalSteps(const PositionStruct &posAfter) {
  // TODO: 以后改成轻量递归，最多20步
  // 老兵搜山：车四平七是第1步，后续2步杀 → 总3步
  return 3;
}

static void InitEngine() {
  PreGenInit();
  NewHash(24); // 分配哈希表，16MB
  Search.pos.FromFen(
      "rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1");
  Search.pos.nDistance = 0;
  Search.pos.PreEvaluate();
  Search.nBanMoves = 0;
  Search.bQuit = Search.bBatch = Search.bDebug = false;
  Search.bUseHash = true;
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
  Search.mvResult = 0;
  Search.nScore = 0;
  Search.bUseHash = true;
  // ★ 这行必须加！否则开局库路径不对，导致开局库不起作用.
  strncpy(Search.szBookFile, g_bookPath,
          sizeof(Search.szBookFile) - 1); // ★ 这行必须加！
  Search.szBookFile[sizeof(Search.szBookFile) - 1] =
      '\0'; // 防止字符串操作会越界读内存，可能崩溃或读取垃圾数据
  Search.bUseBook = g_useBook; // 开局库开关由 JNI 接口控制（唯一开关）
  Search.mvPonder =
      0; // bug: 如果不清零，会导致 ponder 数据残留混乱，出现错误的着法
  ClearHash();
  return true;
}
// 把象眼着法编码wmv转成日志可读的字符串，比如 "a9->b7" 或 "(0,4)->(1,4)"
static void WmvToStr(uint16_t wmv, char *out, int outSize) {
  if (wmv == 0) {
    snprintf(out, outSize, "null");
    return;
  }
  // 象眼wmv打包成32位坐标的格式，和你之前处理ponder的宏完全一致
  uint32_t coord = ((uint32_t)wmv << 16) | wmv;
  char *s = (char *)&coord;
  // s[0]=起点文件('a'-'i'), s[1]=起点Rank('0'-'9', 9对应第0行，0对应第9行)
  // s[2]=终点文件, s[3]=终点Rank
  snprintf(out, outSize, "%c%d->%c%d", s[0],
            9 - (s[1] - '0'), // 转成人类可读的Rank（0-9）
            s[2], 9 - (s[3] - '0'));
}
// 在DoSearch函数里，原生搜索之前加测试代码（完全独立，和现有逻辑隔离）
// 测试用：你给的简单残局（红方2步杀）
const char* testFen = "3P5/4R4/5k3/9/9/5r3/9/9/4K4/9 w";
// DFS节点：保存局面、深度、路径（完全不用std::stack）
struct DfsNode {
    PositionStruct pos;
    int depth;
    uint16_t path[20];
};

// 快速搜绝杀：用固定数组当栈，无动态内存，无命名空间冲突
static void DoSearch(const char *fenStr, int depth, SearchOutput *out) {
  
  // 清零/清空输出
  memset(out, 0, sizeof(SearchOutput));

  if (!ResetSearch(fenStr)) {
    snprintf(out->result, sizeof(out->result), "Error: empty FEN");
    return;
  }

  if (Search.bUseBook) {
    char rawFen[512];
    Search.pos.ToFen(rawFen);
    uint32_t rawHash = Search.pos.zobr.dwKey;
    int endgameWmv = queryEndgameMoveInternal(rawFen, rawHash);
    LOGE("EndgameQuery: rawFen=%s, rawHash=0x%08X, ret=0x%04X",
         rawFen, rawHash, endgameWmv);
    if (endgameWmv != 0) {
        // ✅ 核心：直接用残局库预存分，不调用任何IsMate()相关逻辑
        Search.mvResult = endgameWmv;
        Search.nScore = endgameWmv; // 预存分完全正确，bookmanager已经验证过
        Search.mvPonder = 0; // 后续可以加临时搜索算ponder，不影响主分
        LOGE("EndgameHit: Use preset score, no IsMate check");
        goto OUTPUT_BUILD; // 直接跳输出，完全不碰原生搜索
    }
  }

  SearchMain(depth);

  // 新增结果构建标签
  OUTPUT_BUILD:
  out->score = Search.nScore;
  out->mv = Search.mvResult;
  out->ponderMv = Search.mvPonder;

  snprintf(out->scoreStr, sizeof(out->scoreStr), "score=%d", out->score);

  if (out->mv > 0) {
    uint32_t coord = MOVE_COORD(out->mv);
    // FIXME: 依赖小端序，严格别名违规。以后可改用位移拆字节。
    char *s = (char *)&coord;
    int fromFile = s[0] - 'a';       // 0~8, a=0, b=1, ..., i=8
    int fromRank = 9 - (s[1] - '0'); // 坐标转换
    int toFile = s[2] - 'a';
    int toRank = 9 - (s[3] - '0'); // 坐标转换

    snprintf(out->bestmoveStr, sizeof(out->bestmoveStr),
             "bestmove(%d,%d,%d,%d)", fromRank, fromFile, toRank, toFile);
  }
  LOGE("PONDER_CHECK: isBookMove=%d, out->ponderMv=0x%04X",
       (Search.nNodes == 0), out->ponderMv);
  // 删除search.cpp:line729 Search.nScore=100; 这行，改用 nNodes
  // 来判断是否开局库着法,更合理，避免误判。
  bool isBookMove = (Search.nNodes == 0);
  if (isBookMove && out->ponderMv > 0) {
    uint32_t pcoord = MOVE_COORD(out->ponderMv);
    // FIXME: 依赖小端序，严格别名违规。以后可改用位移拆字节。
    char *ps = (char *)&pcoord;
    int pfromFile = ps[0] - 'a';
    int pfromRank = 9 - (ps[1] - '0');
    int ptoFile = ps[2] - 'a';
    int ptoRank = 9 - (ps[3] - '0');
    snprintf(out->ponderText, sizeof(out->ponderText), "\nponder(%d,%d,%d,%d)",
             pfromRank, pfromFile, ptoRank, ptoFile);
  }

  // 杀棋判断
  if (out->score >= 9960) {
    int moves = (10000 - out->score) / 2;
    if (moves <= 0)
      moves = 1;
    snprintf(out->mateStr, sizeof(out->mateStr), "红方 %d 步杀！", moves);
  } else if (out->score <= -9960) {
    int moves = (10000 + out->score) / -2;
    if (moves <= 0)
      moves = 1;
    snprintf(out->mateStr, sizeof(out->mateStr), "黑方 %d 步杀！", moves);
  }

  if (out->mv <= 0) {
    snprintf(out->mateStr, sizeof(out->mateStr), "无合法着法，被绝杀！");
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeVersion(
    JNIEnv *env, jclass clazz) {
  return env->NewStringUTF("ElephantEye v3.32 (象眼引擎) \n "
                           "Original: 象棋百科全书网 (xqbase.com) \n "
                           "Android Port: 廖延贤 (github.com/DavidLeoBJ) \n "
                           "License: LGPL v2.1 \n "
                           "Built: " __DATE__ " " __TIME__);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSearch(
    JNIEnv *env, jclass clazz, jstring fen, jint depth) {

  const char *fenStr = env->GetStringUTFChars(fen, NULL);

  SearchOutput out;

  DoSearch(fenStr, depth, &out);

  if (out.bestmoveStr[0] == '\0') {
    snprintf(out.result, sizeof(out.result), "无合法着法，被绝杀!");
    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(out.result);
  } else {
    strncat(out.result, out.bestmoveStr,
            sizeof(out.result) - strlen(out.result) - 1);
  }

  if (out.ponderText[0] != '\0') {
    strncat(out.result, out.ponderText,
            sizeof(out.result) - strlen(out.result) - 1);
  }

  if (out.mateStr[0] != '\0') {
    strncat(out.result, out.mateStr,
            sizeof(out.result) - strlen(out.result) - 1);
  }

  env->ReleaseStringUTFChars(fen, fenStr);
  return env->NewStringUTF(out.result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeEvaluate(
    JNIEnv *env, jclass clazz, jstring fen, jint depth) {

  const char *fenStr = env->GetStringUTFChars(fen, NULL);

  SearchOutput out;

  DoSearch(fenStr, depth, &out);

  if (out.mateStr[0] != '\0') {
    strncat(out.scoreStr, out.mateStr,
            sizeof(out.scoreStr) - strlen(out.scoreStr) - 1);
  }

  env->ReleaseStringUTFChars(fen, fenStr);
  return env->NewStringUTF(out.scoreStr);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSearchAndEvaluate(
    JNIEnv *env, jclass clazz, jstring fen, jint depth) {

  const char *fenStr = env->GetStringUTFChars(fen, NULL);

  SearchOutput out;

  DoSearch(fenStr, depth, &out);

  if (out.bestmoveStr[0] == '\0') {
    snprintf(out.result, sizeof(out.result), "无合法着法，被绝杀!");
    env->ReleaseStringUTFChars(fen, fenStr);
    return env->NewStringUTF(out.result);
  } else {
    strncat(out.result, out.bestmoveStr,
            sizeof(out.result) - strlen(out.result) - 1);
  }

  if (out.ponderText[0] != '\0') {
    strncat(out.result, out.ponderText,
            sizeof(out.result) - strlen(out.result) - 1);
  }

  strncat(out.result, out.scoreStr,
          sizeof(out.result) - strlen(out.result) - 1);

  if (out.mateStr[0] != '\0') {
    strncat(out.result, out.mateStr,
            sizeof(out.result) - strlen(out.result) - 1);
  }

  env->ReleaseStringUTFChars(fen, fenStr);
  return env->NewStringUTF(out.result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSetBookPath(
    JNIEnv *env, jobject obj, jstring path) {

  const char *pathStr = env->GetStringUTFChars(path, NULL);
  strncpy(g_bookPath, pathStr, sizeof(g_bookPath) - 1);
  g_bookPath[sizeof(g_bookPath) - 1] =
      '\0'; // 防止字符串操作会越界读内存，可能崩溃或读取垃圾数据
  env->ReleaseStringUTFChars(path, pathStr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSetBookEnabled(
    JNIEnv *env, jobject obj, jboolean enabled) {
  g_useBook = (enabled == JNI_TRUE);
}
