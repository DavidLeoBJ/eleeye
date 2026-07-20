/*
ElephantEye Book Maker - a Chinese Chess Book Maker Program for ElephantEye
Designed by Morning Yellow, Version: 3.25, Last Modified: Apr. 2011
Copyright (C) 2004-2011 www.xqbase.com

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
/*
 * MakeBook - ElephantEye 原作者开局库生成接口
 *
 * 参数含义（原作者定义）：
 * nWin:   该着法对应的胜局数下限（>=）
 * nDraw:  该着法对应的和局数下限（>=）
 * nLoss:  该着法对应的负局数上限（<=）
 * nRatio: 该着法在棋谱中出现的最少次数（>=）
 *
 * 注意：
 * - 黑方胜局少、和局多，容易被 nWin / nDraw 过滤
 * - 原作者是按“单局、单次调用”设计的
 * - 改为 EXE + 死循环批量调用，需注意全局状态重置
 */
// BOOL WINAPI MakeBook(LPCSTR szPath, LPCSTR szFile,
//                      LONG nWin, LONG nDraw, LONG nLoss, LONG nRatio);

#include <stdio.h>
#include <string.h>
#include <conio.h>   // 必须加！用于_kbhit/_getch，死循环的核心
#include <windows.h> // 原来就有！用于Sleep和SetConsoleOutputCP
#include <cstdio>
#include <cstring>
#include <cctype>
#include <regex>
#include <string>
#include "../base/base.h"
#include "../eleeye/position.h"
#include "../eleeye/book.h"
#include "../cchess/cchess.h"
#include "../cchess/pgnfile.h"
#include "../eleeye/pregen.h"

// extern "C" __declspec(dllexport) BOOL WINAPI MakeBook(LPCSTR szPath, LPCSTR szFile,
//     LONG nWin, LONG nDraw, LONG nLoss, LONG nRatio);
// extern "C" __declspec(dllexport) LONG WINAPI GetBookMoves(const PositionStruct *lppos,
//     LPCSTR szFile, BookStruct *lpbks);
// extern "C" __declspec(dllexport) BOOL WINAPI PutBookMove(LPCSTR szFile, const BookStruct *lpcbk);

struct TempStruct
{
    uint32_t dwZobristLock0, dwZobristLock1;
    int mv, vl;
    TempStruct()
    {
    }
    TempStruct(const TempStruct &tmp)
    {
        dwZobristLock0 = tmp.dwZobristLock0;
        dwZobristLock1 = tmp.dwZobristLock1;
        mv = tmp.mv;
        vl = tmp.vl;
    }
    TempStruct(const PositionStruct &pos, int mvArg, int vlArg)
    {
        dwZobristLock0 = pos.zobr.dwLock0;
        dwZobristLock1 = pos.zobr.dwLock1;
        mv = mvArg;
        vl = vlArg;
    }
    bool operator<(TempStruct tmp) const
    {
        return dwZobristLock1 < tmp.dwZobristLock1 ? true : dwZobristLock1 > tmp.dwZobristLock1 ? false
                                                        : dwZobristLock0 < tmp.dwZobristLock0   ? true
                                                        : dwZobristLock0 > tmp.dwZobristLock0   ? false
                                                                                                : mv < tmp.mv;
    }
};

static struct
{
    FILE *fpBookFile, *fpTempFile;
    int nTempLen;
    int nWin, nDraw, nLoss, nRatio;
    TempStruct *TempBuffer;
} MakeBook2;

inline void ReadTemp(int nPtr, TempStruct *lptmp, int nLen)
{
    fseek(MakeBook2.fpTempFile, nPtr * sizeof(TempStruct), SEEK_SET);
    fread(lptmp, sizeof(TempStruct), nLen, MakeBook2.fpTempFile);
}

inline void WriteTemp(int nPtr, const TempStruct *lptmp, int nLen)
{
    fseek(MakeBook2.fpTempFile, nPtr * sizeof(TempStruct), SEEK_SET);
    fwrite(lptmp, sizeof(TempStruct), nLen, MakeBook2.fpTempFile);
}

inline TempStruct GetTemp(int nPtr)
{
    TempStruct tmp;
    ReadTemp(nPtr, &tmp, 1);
    return tmp;
}

inline void SetTemp(int nPtr, const TempStruct &tmp)
{
    WriteTemp(nPtr, &tmp, 1);
}

inline void AddTemp(const TempStruct &tmp)
{
    SetTemp(MakeBook2.nTempLen, tmp);
    MakeBook2.nTempLen++;
}

inline int MoveValue(int sd, int nResult)
{
    switch (nResult)
    {
    case 1:
        return sd == 0 ? MakeBook2.nWin : MakeBook2.nLoss;
    case 2:
        return MakeBook2.nDraw;
    case 3:
        return sd == 0 ? MakeBook2.nLoss : MakeBook2.nWin;
    default:
        return 0;
    }
}

static void ParseFile(const char *szFilePath)
{
    int i, mv, mvMirror, nComp;
    PositionStruct pos, posMirror;
    PgnFileStruct pgn;

    if (pgn.Read(szFilePath))
    {
        pos = posMirror = pgn.posStart;
        posMirror.Mirror();
        for (i = 0; i < pgn.nMaxMove; i++)
        {
            mv = pgn.wmvMoveTable[i + 1];
            mvMirror = MOVE_MIRROR(mv);
            if (pos.zobr.dwLock1 < posMirror.zobr.dwLock1)
            {
                nComp = -1;
            }
            else if (pos.zobr.dwLock1 > posMirror.zobr.dwLock1)
            {
                nComp = 1;
            }
            else
            {
                if (pos.zobr.dwLock0 < posMirror.zobr.dwLock0)
                {
                    nComp = -1;
                }
                else if (pos.zobr.dwLock0 > posMirror.zobr.dwLock0)
                {
                    nComp = 1;
                }
                else
                {
                    nComp = 0;
                }
            }
            if (nComp <= 0)
            {
                AddTemp(TempStruct(pos, mv, MoveValue(pos.sdPlayer, pgn.nResult)));
            }
            if (nComp >= 0)
            {
                AddTemp(TempStruct(posMirror, mvMirror, MoveValue(pos.sdPlayer, pgn.nResult)));
            }
            if (pos.ucpcSquares[DST(mv)] == 0)
            {
                pos.MakeMove(mv);
            }
            else
            {
                pos.MakeMove(mv);
                pos.SetIrrev();
            }
            if (posMirror.ucpcSquares[DST(mvMirror)] == 0)
            {
                posMirror.MakeMove(mvMirror);
            }
            else
            {
                posMirror.MakeMove(mvMirror);
                posMirror.SetIrrev();
            }
        }
    }
}

static void SearchFolder(const char *szFolderPath);

static void SearchFile(const char *szFilePath, const WIN32_FIND_DATA &wfd)
{
    if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        if (strlen(szFilePath) > 4)
        {
            if (strnicmp(szFilePath + strlen(szFilePath) - 4, ".PGN", 4) == 0)
            {
                ParseFile(szFilePath);
            }
        }
    }
    else
    {
        if (strcmp(wfd.cFileName, ".") != 0 && strcmp(wfd.cFileName, "..") != 0)
        {
            SearchFolder(szFilePath);
        }
    }
}

static void SearchFolder(const char *szFolderPath)
{
    char szFilePath[1024];
    WIN32_FIND_DATA wfd;
    HANDLE hFind;
    char *lpFilePath;

    strcpy(szFilePath, szFolderPath);
    lpFilePath = szFilePath + strlen(szFolderPath);
    if (*(lpFilePath - 1) == '\\')
    {
        lpFilePath--;
    }
    strcpy(lpFilePath, "\\*");
    lpFilePath++;
    hFind = FindFirstFile(szFilePath, &wfd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        strcpy(lpFilePath, wfd.cFileName);
        SearchFile(szFilePath, wfd);
        while (FindNextFile(hFind, &wfd))
        {
            strcpy(lpFilePath, wfd.cFileName);
            SearchFile(szFilePath, wfd);
        }
    }
    FindClose(hFind);
}

static const int TEMP_BUFFER_SIZE = 1048576;

static void SortMem(int low, int high)
{
    TempStruct tmpMid, tmpSwap;
    int lowMid, highMid;

    lowMid = low;
    highMid = high;
    tmpMid = MakeBook2.TempBuffer[(low + high) / 2];
    while (lowMid <= highMid)
    {
        while (lowMid < high && MakeBook2.TempBuffer[lowMid] < tmpMid)
        {
            lowMid++;
        }
        while (low < highMid && tmpMid < MakeBook2.TempBuffer[highMid])
        {
            highMid--;
        }
        if (lowMid <= highMid)
        {
            SWAP(MakeBook2.TempBuffer[lowMid], MakeBook2.TempBuffer[highMid]);
            lowMid++;
            highMid--;
        }
    }

    if (low < highMid)
    {
        SortMem(low, highMid);
    }
    if (lowMid < high)
    {
        SortMem(lowMid, high);
    }
}

static void SortTemp(int low, int high)
{
    TempStruct tmpMid, tmpSwap;
    int lowMid, highMid, nLen;

    nLen = high - low + 1;
    if (nLen > TEMP_BUFFER_SIZE)
    {
        lowMid = low;
        highMid = high;
        tmpMid = GetTemp((low + high) / 2);
        while (lowMid <= highMid)
        {
            while (lowMid < high && GetTemp(lowMid) < tmpMid)
            {
                lowMid++;
            }
            while (low < highMid && tmpMid < GetTemp(highMid))
            {
                highMid--;
            }
            if (lowMid <= highMid)
            {
                tmpSwap = GetTemp(lowMid);
                SetTemp(lowMid, GetTemp(highMid));
                SetTemp(highMid, tmpSwap);
                lowMid++;
                highMid--;
            }
        }

        if (low < highMid)
        {
            SortTemp(low, highMid);
        }
        if (lowMid < high)
        {
            SortTemp(lowMid, high);
        }
    }
    else
    {
        ReadTemp(low, MakeBook2.TempBuffer, nLen);
        if (nLen > 1)
        {
            SortMem(0, nLen - 1);
        }
        WriteTemp(low, MakeBook2.TempBuffer, nLen);
    }
}

static void MakeBook()
{
    int i;
    TempStruct tmpLast, tmp;
    BookStruct bk;

    bool first = true; // 新增：标记是否是第一次

    tmpLast.dwZobristLock0 = tmpLast.dwZobristLock1 = 0;
    tmpLast.mv = tmpLast.vl = 0;

    for (i = 0; i < MakeBook2.nTempLen; i++)
    {
        tmp = GetTemp(i);
        if (tmpLast < tmp)
        {
            if (!first)
            { // 不是第一次，才写“前一组”
                if (tmpLast.vl >= MakeBook2.nRatio)
                {
                    bk.wvl = tmpLast.vl / MakeBook2.nRatio;
                    fwrite(&bk, sizeof(BookStruct), 1, MakeBook2.fpBookFile);
                }
            }
            tmpLast = tmp;
            first = false;
        }
        else
        {
            tmpLast.vl += tmp.vl;
        }
    }

    // 循环外：写最后一组
    if (tmpLast.vl >= MakeBook2.nRatio)
    {
        bk.dwZobristLock = tmpLast.dwZobristLock1;
        bk.wmv = tmpLast.mv;
        bk.wvl = tmpLast.vl / MakeBook2.nRatio;
        fwrite(&bk, sizeof(BookStruct), 1, MakeBook2.fpBookFile);
    }
}

static bool bInit = false;

inline void Init(void)
{
    if (!bInit)
    {
        bInit = true;
        // PreGenInit();
        ChineseInit();
    }
}

// 新增：判断棋局结束
// 1. 去掉首尾空白（含全角空格）
bool IsGameResult(const char *s)
{
    // 1. 去掉首尾空白（含全角空格）
    while (*s && isspace((unsigned char)*s))
        s++;
    const char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1)))
        end--;

    int len = (int)(end - s);
    if (len < 3)
        return false;

    //  2  任何非半角字符 → 整局棋谱直接作废
    char buf[16];
    int j = 0;

    for (int i = 0; i < len && j < 15; i++)
    {
        unsigned char c = (unsigned char)s[i];

        // 只允许半角可打印 ASCII + 基本空白符
        // 空格(0x20) ~ 波浪号(0x7E)
        if (!(c == ' ' || c == '\t' ||
              (c >= 0x21 && c <= 0x7E) ||
              c == '\r' || c == '\n'))
        {
            // 发现全角 / 非法字符 → 这局棋谱作废
            return false;
        }

        // 忽略空格 / tab
        if (c == ' ' || c == '\t')
        {
            continue;
        }

        buf[j++] = c;
    }
    buf[j] = '\0';
    // 3. 归一化全角 -> 半角（最小改动版）
    for (int i = 0; buf[i]; i++)
    {
        // 全角空格
        if ((unsigned char)buf[i] == 0xE3 &&
            (unsigned char)buf[i + 1] == 0x80 &&
            (unsigned char)buf[i + 2] == 0x80)
        {
            buf[i] = ' ';
            // 删除后面两个字节
            memmove(buf + i + 1, buf + i + 3, strlen(buf + i + 3) + 1);
            continue;
        }

        // 全角减号
        if ((unsigned char)buf[i] == 0xE2 &&
            (unsigned char)buf[i + 1] == 0x80 &&
            (unsigned char)buf[i + 2] == 0x93)
        {
            buf[i] = '-';
            memmove(buf + i + 1, buf + i + 3, strlen(buf + i + 3) + 1);
            continue;
        }

        // 全角斜杠
        if ((unsigned char)buf[i] == 0xE2 &&
            (unsigned char)buf[i + 1] == 0x80 &&
            (unsigned char)buf[i + 2] == 0x95)
        {
            buf[i] = '/';
            memmove(buf + i + 1, buf + i + 3, strlen(buf + i + 3) + 1);
            continue;
        }
    }

    // 4. 语义判断
    return (strcmp(buf, "1-0") == 0 ||
            strcmp(buf, "0-1") == 0 ||
            strcmp(buf, "1/2-1/2") == 0 ||
            strcmp(buf, "1/2") == 0);
}

int IccsToMove(const char *iccs)
{
    if (!iccs)
        return 0;

    char s[5];
    int j = 0;

    // 去掉 '-'，统一小写
    for (int i = 0; iccs[i] && j < 4; ++i)
    {
        if (iccs[i] == '-')
            continue;
        s[j++] = tolower(iccs[i]);
    }
    if (j != 4)
        return 0;

    return COORD_MOVE(*(uint32_t *)s);
}

static void ParseCollection(LPCSTR szFile,
                            LONG nWin, LONG nDraw, LONG nLoss)
{
    FILE *fp = fopen(szFile, "r");
    if (!fp)
    {
        fprintf(stderr, "[PARSECOLLECTION] open failed\n");
        return;
    }

    /* ========= 正则：识别着法行 ========= */
    static const std::regex MoveLineRegex(
        R"(^\s*[0-9]+\.\s+[A-Ia-i][0-9]-[A-Ia-i][0-9])");

    static const std::regex IccsRegex(
        R"(\b([A-Ia-i][0-9])-([A-Ia-i][0-9])\b)");

    /* ========= ICCS → ElephantEye mv（唯一实现） ========= */
    auto IccsToMove = [](const char *iccs) -> int
    {
        if (!iccs)
            return 0;

        char s[5] = {0};
        int j = 0;
        for (int i = 0; iccs[i] && j < 4; ++i)
        {
            if (iccs[i] == '-')
                continue;
            s[j++] = tolower(iccs[i]);
        }
        if (j != 4)
            return 0;

        return COORD_MOVE(*(uint32_t *)s);
    };

    auto ExtractIccs = [](const std::string &line, char iccs[8]) -> bool
    {
        std::smatch m;
        if (std::regex_search(line, m, IccsRegex))
        {
            iccs[0] = m[1].str()[0];
            iccs[1] = m[1].str()[1];
            iccs[2] = m[2].str()[0];
            iccs[3] = m[2].str()[1];
            iccs[4] = '\0';
            return true;
        }
        return false;
    };

    PositionStruct pos;

    PositionStruct posMirror; // 用于镜像棋盘

    int gameCount = 0;
    int moveCount = 0;

    char buf[4096];
    std::string line;
    int pendingResult = 0;

    while (fgets(buf, sizeof(buf), fp))
    {

        line = buf;
        line.erase(line.find_last_not_of("\r\n") + 1);

        /* 1. 局开始 */
        if (line.find("[Game") != std::string::npos)
        {
            // pos.FromFen("rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1");

            pos.FromFenPrefix("rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w");
            posMirror = pos;
            posMirror.Mirror();
            gameCount++;
            continue;
        }

        // 1. 解析元数据：[Result "..."]（权威结果）
        if (line.find("[Result") != std::string::npos)
        {
            if (line.find("\"1-0\"") != std::string::npos)
                pendingResult = 1;
            else if (line.find("\"0-1\"") != std::string::npos)
                pendingResult = 3;
            else if (line.find("\"1/2-1/2\"") != std::string::npos)
                pendingResult = 2;
            else
                pendingResult = 0; // "*"

            continue; // ✅ 这一行处理完了，直接下一行
        }

        if (gameCount > 0 && line == "1-0" || line == "0-1" || line == "1/2-1/2")
        {
            int tailResult = 0;
            if (line == "1-0")
                tailResult = 1;
            else if (line == "0-1")
                tailResult = 3;
            else if (line == "1/2-1/2")
                tailResult = 2;

            if (tailResult != pendingResult)
            {
                fprintf(stderr,
                        "[WARN] Result mismatch: meta=%d tail=%d\n",
                        pendingResult, tailResult);
            }

            pendingResult = 0; // 准备下一局
            continue;
        }

        if (gameCount == 0)
            continue;

        if (!std::regex_search(line, MoveLineRegex))
            continue;

        char iccs[8];
        if (ExtractIccs(line, iccs))
        {
            int mv = IccsToMove(iccs);
            if (mv != 0)
            {
                int vl = MoveValue(pos.sdPlayer, pendingResult);
                AddTemp(TempStruct(pos, mv, vl));
                moveCount++;
                pos.MakeMove(mv);
            }
        }
    }

    fclose(fp);
}

BOOL WINAPI MakeBook(LPCSTR szPath, LPCSTR szFile,
                     LONG nWin, LONG nDraw, LONG nLoss, LONG nRatio)
{
    fprintf(stderr,
            "[MAKEBOOK] start: nWin=%d nDraw=%d nLoss=%d nRatio=%d\n",
            nWin, nDraw, nLoss, nRatio);
    BOOL bSuccess;
    Init();
    bSuccess = FALSE;
    MakeBook2.nWin = nWin;
    MakeBook2.nDraw = nDraw;
    MakeBook2.nLoss = nLoss;
    MakeBook2.nRatio = nRatio;
    MakeBook2.fpTempFile = tmpfile();
    if (MakeBook2.fpTempFile != NULL)
    {
        MakeBook2.nTempLen = 0;
        MakeBook2.fpBookFile = fopen(szFile, "wb");
        if (MakeBook2.fpBookFile != NULL)
        {
            // SearchFolder(szPath);
            // 为了适应单文件棋谱合集，这里直接读取单个文件
            DWORD attr = GetFileAttributesA(szPath);
            if (attr != INVALID_FILE_ATTRIBUTES &&
                (attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                SearchFolder(szPath);
            }
            else
            {
                ParseCollection(szPath, nWin, nDraw, nLoss);
            }
            if (MakeBook2.nTempLen > 1)
            {
                MakeBook2.TempBuffer = new TempStruct[TEMP_BUFFER_SIZE];
                SortTemp(0, MakeBook2.nTempLen - 1);
                delete[] MakeBook2.TempBuffer;
            }
            MakeBook();
            fclose(MakeBook2.fpBookFile);
            bSuccess = TRUE;
        }
        fclose(MakeBook2.fpTempFile);
    }
    return bSuccess;
}

LONG WINAPI GetBookMoves(const PositionStruct *lppos,
                         LPCSTR szFile, BookStruct *lpbks)
{
    Init();
    return GetBookMoves(*lppos, szFile, lpbks);
}

BOOL WINAPI PutBookMove(LPCSTR szFile, const BookStruct *lpcbk)
{
    BookFileStruct BookFile;
    BookStruct bk;
    int nPtr;

    Init();
    nPtr = lpcbk->nPtr;
    if (!BookFile.Open(szFile, true))
    {
        return FALSE;
    }
    if (nPtr >= BookFile.nLen)
    {
        BookFile.Close();
        return FALSE;
    }
    BookFile.Read(bk, nPtr);
    bk.wvl = lpcbk->wvl;
    BookFile.Write(bk, nPtr);
    BookFile.Close();
    return TRUE;
}

void PositionStruct::FromFenPrefix(const char *szFen)
{
    // 1. 用完整 FEN 解析棋盘 + 走子方
    FromFen(szFen);

    // 2. 清零“后半段状态”
    nMoveNum = 0;
    nDistance = 0;

    // 3. 重新算 Zobrist，只基于棋盘 + 走子方
    zobr.InitZero();

    // 4. 棋盘棋子
    for (int sq = 0; sq < 256; ++sq)
    {
        int pc = ucpcSquares[sq];
        if (pc != 0)
        {
            zobr.Xor(PreGen.zobrTable[pc][sq]);
        }
    }

    // 5. 走子方
    if (sdPlayer == 1)
    {
        zobr.Xor(PreGen.zobrPlayer);
    }
}

// ==================== 新增：控制台程序入口 ====================
// 原作者写的是DLL，要增加main函数改造成可执行程序
int main(int argc, char *argv[])
{
    if (argc != 7)
    {
        printf("用法: %s <PGN文件> <输出book> <nWin> <nDraw> <nLoss> <nRatio>\n", argv[0]);
        printf("示例: %s WXF-41743games.pgns iccs_41743_v2.book 3 1 -1 4\n", argv[0]);
        system("pause");
        return 1;
    }

    PreGenInit(); // 重复调用，导致严重错误
    ChineseInit(false);
    SetConsoleOutputCP(65001);

    BOOL success = MakeBook(
        argv[1],       // 棋谱
        argv[2],       // book
        atol(argv[3]), // nWin=3
        atol(argv[4]), // nDraw=1
        atol(argv[5]), // nLoss=-1
        atol(argv[6])  // nRatio=4 权重<4丢弃
    );

    if (success)
        printf("\n开局库生成成功！\n");
    else
        printf("\n开局库生成失败！\n");

    return 0;
}