#define MODULE_TAG "EndgameNormalizer"
#include "book_common.h"
#include <android/log.h>
#include <string>

namespace EndgameNormalizer
{
    // ====================== 坐标转换工具（核心！之前所有坑的根源都在这）
    // ====================== Java坐标→象眼内部SQ编码（y*16 +
    // x，y是象眼行，x是象眼列）
    inline int JavaCoordToEleEyeSq(int javaRank, int javaFile)
    {
        // javaRank: 1-based行（1=红底线，9=黑底线）
        // javaFile: 1-based列（1=a，9=i）
        int eyeY = 9 - javaRank; // 核心转换：Java行转象眼行
        int eyeX = javaFile - 1; // 核心转换：Java列转象眼列
        // 加个合法性校验，提前抓错
        if (eyeY < 0 || eyeY >= 10 || eyeX < 0 || eyeX >= 9)
        {
            LOGE("Invalid JavaCoord: (%d,%d) -> eyeY=%d, eyeX=%d", javaRank, javaFile,
                 eyeY, eyeX);
            return -1;
        }
        return (eyeY << 4) | eyeX;
    }
    // 象眼SQ编码→Java坐标
    inline void EleEyeSqToJavaCoord(int sq, int &javaRank, int &javaFile)
    {
        int eyeY = sq >> 4;
        int eyeX = sq & 0xF;
        javaRank = 9 - eyeY; // 象眼行转Java行
        javaFile = eyeX + 1; // 象眼列转Java列
    }
    // ====================== FEN/归一化工具 ======================
    std::string NormalizeFen(const std::string &rawFen); // 归一化函数
    int InverseNormalizeMove(int stdWmv);                // 逆映射函数
    // ====================== 残局库IO工具 ======================
    uint32_t CalcFenHash(const std::string &stdFen);
    bool SaveBookEntry(uint32_t hash, int stdWmv);
    bool LoadBookEntry(uint32_t hash, int &outWmv);
} // namespace EndgameNormalizer

TransformList g_normalizeTransforms;
namespace EndgameNormalizer
{
    int JavaCoordToStdSqForEndgame(int javaRank, int javaFile,
                                   const TransformList &transforms)
    {
        // 1. 先调用通用转换，得到原始局面的SQ（用book_common里的坐标宏）
        int rawSq = JavaCoordToSq(javaRank, javaFile); // 去掉Common::前缀

        // 2. 逆序遍历变换记录（归一化时按顺序记录，逆映射要从后往前应用）
        for (auto it = transforms.rbegin(); it != transforms.rend(); ++it)
        {
            if (it->type == FenTransformType::SHIFT_COL)
            {
                // 列平移的逆操作：原始SQ减去平移量（归一化时是加了delta，逆映射要减回来）
                rawSq -= it->param1;
            }
            else if (it->type == FenTransformType::SWAP_ROWS)
            {
                // 行交换的逆操作：互换SQ的行号（行交换是对称的，正向反向逻辑一样）
                int y = ROW(rawSq);
                int x = COL(rawSq);
                if (y == it->param1)
                {                   // 当前行是交换行A
                    y = it->param2; // 换成交换行B
                }
                else if (y == it->param2)
                {                   // 当前行是交换行B
                    y = it->param1; // 换成交换行A
                }
                rawSq = SQ(y, x); // 重新生成SQ编码（用book_common里的SQ宏）
            }
        }
        return rawSq; // 返回std局面下的SQ编码
    }
} // namespace EndgameNormalizer

namespace EndgameNormalizer
{
    /**
     * 残局归一化核心函数：
     * 1. 仅处理红方阵营r=5~9，绝不碰黑方阵地
     * 2. 列锚定优先中路c=4，满列按规则移位：c>=4右移，c<4左移
     * 3. 仅生成临时stdFen算哈希，不修改原生引擎的Search.pos，绝对安全
     */
    std::string normalizeFenForEndgame(const std::string &rawFen)
    {
        // 每次归一化前清空旧记录（核心：变换记录和当前归一化强绑定）
        g_normalizeTransforms.clear();

        std::string stdFen = rawFen;
        std::vector<std::string> segs;
        // 1. 拆FEN棋盘段（0=黑底线，9=红底线，完全符合FEN标准）
        size_t start = 0;
        size_t pos = stdFen.find('/');
        while (pos != std::string::npos)
        {
            segs.push_back(stdFen.substr(start, pos - start));
            start = pos + 1;
            pos = stdFen.find('/', start);
        }
        segs.push_back(stdFen.substr(start));
        // 截掉走子方等后缀，只留棋盘
        if (!segs.empty())
        {
            size_t sp = segs.back().find(' ');
            if (sp != std::string::npos)
                segs.back() = segs.back().substr(0, sp);
        }
        if (segs.size() != 10)
        {
            LOGE("Normalize ERROR: Invalid FEN, seg count=%zu != 10", segs.size());
            return rawFen;
        }

        // 2. 找红车原始位置（FEN坐标体系：行0~9，列1~9，左上为黑底线左列）
        int redCarY = -1, redCarX = -1;
        LOGE("========== NORMALIZE: SCAN RAW FEN FOR RED CAR START ==========");
        for (int y = 0; y < 10; y++)
        {
            size_t rPos = segs[y].find('R');
            if (rPos != std::string::npos)
            {
                redCarY = y; // FEN行号（0=黑底线，9=红底线）
                size_t numStart = rPos;
                while (numStart > 0 && isdigit(segs[y][numStart - 1]))
                    numStart--;
                std::string xStr = segs[y].substr(numStart, rPos - numStart);
                redCarX = xStr.empty() ? 1 : std::stoi(xStr); // FEN列号（1-based，左→右）
                // 关键：直接计算对应引擎内部的sq编码（象眼规则：y<<4 |
                // x0，x0是0-based列）
                int engineSq = (redCarY << 4) | (redCarX - 1);
                LOGE("Normalize: Found raw FEN red car -> FEN(row=%d, col=%d) | "
                     "Engine(sq=0x%02X, y=%d, x=%d)",
                     redCarY, redCarX, engineSq, redCarY, redCarX - 1);
                break;
            }
        }
        // 校验：必须找到红车，且列在合法范围
        if (redCarY == -1)
        {
            LOGE("Normalize ERROR: No red car 'R' found in raw FEN");
            return rawFen;
        }
        if (redCarX < 1 || redCarX > 9)
        {
            LOGE("Normalize ERROR: Red car column %d out of range [1,9]", redCarX);
            return rawFen;
        }
        LOGE("========== NORMALIZE: SCAN RAW FEN FOR RED CAR END ==========");

        // 3. 递归找归一化位置：优先锚定中路c=4（1-based），满列按规则移位
        int targetC = redCarX; // 目标列（1-based，初始为红车原始列）
        int targetY = -1;      // 目标行（0-based，红方阵营5~9）
        while (true)
        {
            // 列范围校验（0~8对应1~9）
            if (targetC < 1 || targetC > 9)
            {
                LOGE("Normalize ERROR: All columns full, fallback to raw FEN");
                return rawFen;
            }
            // 只在红方阵营（FEN行5~9，对应红方底线到第5行）找空位
            targetY = -1;
            for (int y = 5; y < 10; y++)
            {
                std::string &seg = segs[y];
                int curC = 0; // 当前列计数器（0-based）
                size_t idx = 0;
                bool hasPiece = false;
                while (idx < seg.size())
                {
                    if (isdigit(seg[idx]))
                    {
                        curC += seg[idx] - '0';
                        idx++;
                    }
                    else
                    {
                        if (curC == targetC - 1)
                        { // curC是0-based，targetC是1-based，减1对齐
                            hasPiece = true;
                            break;
                        }
                        curC++;
                        idx++;
                    }
                }
                if (!hasPiece)
                {
                    targetY = y;
                    break;
                }
            }
            if (targetY != -1)
                break;
            // 满列移位规则（你的核心规则：中路优先，右移/左移）
            LOGD("Normalize: Column %d full, shift (c>=4 right, c<4 left)", targetC);
            targetC += (targetC >= 4) ? 1 : -1;
        }

        // 4. 移红车到目标位置（FEN层面操作，不涉及引擎内部棋盘）
        if (redCarY != targetY)
        {
            std::swap(segs[redCarY], segs[targetY]);
            // 记录行交换变换（逆映射时需要按相反顺序执行）
            g_normalizeTransforms.push_back(
                {FenTransformType::SWAP_ROWS, redCarY, targetY});
            LOGE("NormalizeRecord: Swap rows %d <-> %d (FEN row, black bottom)",
                 redCarY, targetY);
        }

        // 锚定列到targetC（1-based）
        std::string &tSeg = segs[targetY];
        size_t rPos = tSeg.find('R');
        size_t numStart = rPos;
        while (numStart > 0 && isdigit(tSeg[numStart - 1]))
            numStart--;
        std::string newLeft = std::to_string(targetC - 1);  // 0-based左空位数
        std::string newRight = std::to_string(8 - targetC); // 0-based右空位数
        size_t rightNumStart = rPos + 1;
        while (rightNumStart < tSeg.size() && isdigit(tSeg[rightNumStart]))
            rightNumStart++;
        tSeg.replace(numStart, rightNumStart - numStart, newLeft + "R" + newRight);

        // 列替换完成后，计算列平移量并记录变换
        int colDelta = targetC - redCarX; // 1-based列的差值，和引擎0-based列差值一致
        g_normalizeTransforms.push_back({FenTransformType::SHIFT_COL, colDelta, 0});
        LOGE("NormalizeRecord: Shift col by %d (raw col=%d -> std col=%d, 1-based)",
             colDelta, redCarX, targetC);

        // 5. 拼回FEN，并校验stdFen里的红车位置
        stdFen.clear();
        for (size_t i = 0; i < segs.size(); i++)
        {
            if (i > 0)
                stdFen += "/";
            stdFen += segs[i];
        }
        size_t sp = rawFen.find(' ');
        if (sp != std::string::npos)
            stdFen += rawFen.substr(sp);
        else
            stdFen += " w - - 0 1";

        // 校验：打印stdFen里的红车位置，确保归一化正确
        LOGE("========== NORMALIZE: VERIFY STD FEN RED CAR ==========");
        splitFenToSegments(
            stdFen); // 调用void函数，内部会把段落存到g_stdFenSegments里
        auto &stdSegs =
            g_stdFenSegments; // 给全局缓存起个别名，后面三处直接用stdSegs就行！
        for (int y = 0; y < 10; y++)
        {
            size_t rPos = stdSegs[y].find('R');
            if (rPos != std::string::npos)
            {
                size_t numStart = rPos;
                while (numStart > 0 && isdigit(stdSegs[y][numStart - 1]))
                    numStart--;
                std::string xStr = stdSegs[y].substr(numStart, rPos - numStart);
                int stdCol = xStr.empty() ? 1 : std::stoi(xStr);
                int stdSq = (y << 4) | (stdCol - 1);
                LOGE("Normalize: Found std FEN red car -> FEN(row=%d, col=%d) | "
                     "Engine(sq=0x%02X, y=%d, x=%d)",
                     y, stdCol, stdSq, y, stdCol - 1);
                break;
            }
        }
        LOGE("Normalize SUCCESS: %s → %s", rawFen.c_str(), stdFen.c_str());
        return stdFen;
    }

    // 静态函数，仅当前cpp可见，不暴露符号，完全内部使用
    static int inverseNormalizeMove(int stdWmv)
    {
        if (stdWmv == 0 || g_normalizeTransforms.empty())
        {
            LOGD("InverseMove: no transforms or invalid wmv");
            return 0;
        }
        // 1.
        // 解析stdWmv的源/目标sq（象眼格式：高8位源，低8位目标；每个sq高4位行，低4位列）
        int stdSrcSq = (stdWmv >> 8) & 0xFF;
        int stdDstSq = stdWmv & 0xFF;
        int srcY = stdSrcSq >> 4;  // 源行（0-based）
        int srcX = stdSrcSq & 0xF; // 源列（0-based）
        int dstY = stdDstSq >> 4;  // 目标行（0-based）
        int dstX = stdDstSq & 0xF; // 目标列（0-based）
        LOGD("InverseMove: Input stdWmv=0x%04X, src=(%d,%d), dst=(%d,%d)", stdWmv,
             srcY, srcX, dstY, dstX);
        // 2. 逆序遍历变换记录，执行逆操作（关键！和归一化顺序完全相反）
        for (auto it = g_normalizeTransforms.rbegin();
             it != g_normalizeTransforms.rend(); ++it)
        {
            switch (it->type)
            {
            case FenTransformType::SWAP_ROWS:
            {
                // 逆操作：再交换一次同样的行（交换是对称的）
                if (srcY == it->param1)
                    srcY = it->param2;
                else if (srcY == it->param2)
                    srcY = it->param1;
                if (dstY == it->param1)
                    dstY = it->param2;
                else if (dstY == it->param2)
                    dstY = it->param1;
                LOGD("InverseMove: Inverse swap rows %d <-> %d", it->param1, it->param2);
                break;
            }
            case FenTransformType::SHIFT_COL:
            {
                // 逆操作：列平移相反方向（比如归一化右移+3，还原左移-3）
                int invDelta = -it->param1;
                srcX += invDelta;
                dstX += invDelta;
                LOGD("InverseMove: Inverse shift col by %d", invDelta);
                break;
            }
            }
        }
        // 3. 合法性校验：位置必须在棋盘内
        if (srcY < 0 || srcY >= 10 || srcX < 0 || srcX >= 9 || dstY < 0 ||
            dstY >= 10 || dstX < 0 || dstX >= 9)
        {
            LOGW("InverseMove: Mapped position out of board");
            return 0;
        }
        // 4. 组装还原后的着法（象眼sq格式：行<<4 | 列）
        int rawSrcSq = (srcY << 4) | srcX;
        int rawDstSq = (dstY << 4) | dstX;
        int rawWmv = (rawSrcSq << 8) | rawDstSq;
        LOGD("InverseMove: Output rawWmv=0x%04X, src=(%d,%d), dst=(%d,%d)", rawWmv,
             srcY, srcX, dstY, dstX);
        return rawWmv;
    }
} // namespace EndgameNormalizer