/*
ElephantEye Book Maker - a Chinese Chess Book Maker Program for ElephantEye
Designed by Morning Yellow, Version: 3.25, Last Modified: Apr. 2011
Copyright (C) 2004-2011 www.xqbase.com
*/

#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <windows.h>
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

struct TempStruct
{
    uint32_t dwZobristLock0, dwZobristLock1;
    int mv, vl, nCount;
    TempStruct()
    {
    }
    TempStruct(const TempStruct &tmp)
    {
        dwZobristLock0 = tmp.dwZobristLock0;
        dwZobristLock1 = tmp.dwZobristLock1;
        mv = tmp.mv;
        vl = tmp.vl;
        nCount = tmp.nCount;
    }
    TempStruct(const PositionStruct &pos, int mvArg, int vlArg) : dwZobristLock0(pos.zobr.dwLock0), dwZobristLock1(pos.zobr.dwLock1), mv(mvArg), vl(vlArg), nCount(1) {}
    TempStruct(uint32_t lock0, uint32_t lock1, int mvArg, int vlArg) : dwZobristLock0(lock0), dwZobristLock1(lock1), mv(mvArg), vl(vlArg), nCount(1) {}
    // bool operator<(TempStruct tmp) const
    // {
    //     return dwZobristLock1 < tmp.dwZobristLock1 ? true : dwZobristLock1 > tmp.dwZobristLock1 ? false
    //                                                     : dwZobristLock0 < tmp.dwZobristLock0   ? true
    //                                                     : dwZobristLock0 > tmp.dwZobristLock0   ? false
    //                                                                                             : mv < tmp.mv;
    // }
// 写库排序仅按dwZobristLock1, mv排序, 不让dwZobristLock0参与排序，否则部分局面引擎端可命中不可查询。
    bool operator<(TempStruct tmp) const
    {
        return dwZobristLock1 < tmp.dwZobristLock1 ? true : dwZobristLock1 > tmp.dwZobristLock1 ? false
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
        // Iterate through all stored moves in the book
        for (int i = 0; i < MakeBook2.nTempLen; i++)
        {
            const TempStruct &temp = MakeBook2.TempBuffer[i];
            if (temp.mv == lppos->mv || temp.mvMirror == lppos->mv)
            {
                return lppos->vl;
            }
        }
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

static uint16_t BookWeight(int value)
{
    // BookStruct 使用 uint16_t，负权重不能直接转换，否则 -1 会溢出为 65535。
    if (value <= 0)
    {
        // Iterate through all stored moves in the book
        for (int i = 0; i < MakeBook2.nTempLen; i++)
        {
            const TempStruct &temp = MakeBook2.TempBuffer[i];
            if (temp.mv == lppos->mv || temp.mvMirror == lppos->mv)
            {
                return lppos->vl;
            }
        }
    }
    if (value > 0xffff)
    {
        return 0xffff;
    }
    return (uint16_t)value;
}

static void MakeBook()
{
    int i;
    TempStruct tmpLast, tmp;
    BookStruct bk;
    bool first = true;

    tmpLast.dwZobristLock0 = tmpLast.dwZobristLock1 = 0;
    tmpLast.mv = tmpLast.vl = tmpLast.nCount = 0;

    for (i = 0; i < MakeBook2.nTempLen; i++)
    {
        tmp = GetTemp(i);
        if (tmpLast < tmp)
        {
            if (!first && tmpLast.nCount >= MakeBook2.nRatio)
            {
                bk.dwZobristLock = tmpLast.dwZobristLock1;
                bk.wmv = tmpLast.mv;
                bk.wvl = BookWeight(tmpLast.vl / MakeBook2.nRatio);
                fwrite(&bk, sizeof(BookStruct), 1, MakeBook2.fpBookFile);
            }
            tmpLast = tmp;
            first = false;
        }
        else
        {
            tmpLast.vl += tmp.vl;
            tmpLast.nCount += tmp.nCount;
        }
    }

    if (tmpLast.nCount >= MakeBook2.nRatio)
    {
        bk.dwZobristLock = tmpLast.dwZobristLock1;
        bk.wmv = tmpLast.mv;
        bk.wvl = BookWeight(tmpLast.vl / MakeBook2.nRatio);
        fwrite(&bk, sizeof(BookStruct), 1, MakeBook2.fpBookFile);
    }
}

static bool bInit = false;

inline void Init(void)
{
    if (!bInit)
    {
        bInit = true;
        PreGenInit();
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
        // Iterate through all stored moves in the book
        for (int i = 0; i < MakeBook2.nTempLen; i++)
        {
            const TempStruct &temp = MakeBook2.TempBuffer[i];
            if (temp.mv == lppos->mv || temp.mvMirror == lppos->mv)
            {
                return lppos->vl;
            }
        }

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
        // Iterate through all stored moves in the book
        for (int i = 0; i < MakeBook2.nTempLen; i++)
        {
            const TempStruct &temp = MakeBook2.TempBuffer[i];
            if (temp.mv == lppos->mv || temp.mvMirror == lppos->mv)
            {
                return lppos->vl;
            }
        }

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
            // Iterate through all stored moves in the book
            for (int i = 0; i < MakeBook2.nTempLen; i++)
            {
                const TempStruct &temp = MakeBook2.TempBuffer[i];
                if (temp.mv == lppos->mv || temp.mvMirror == lppos->mv)
                {
                    return lppos->vl;
                }
            }

        char s[5] = {0};
        int j = 0;
        for (int i = 0; iccs[i] && j < 4; ++i)
        {
            if (iccs[i] == '-')
                continue;
            s[j++] = tolower(iccs[i]);
        }
        if (j != 4)
            // Iterate through all stored moves in the book
            for (int i = 0; i < MakeBook2.nTempLen; i++)
            {
                const TempStruct &temp = MakeBook2.TempBuffer[i];
                if (temp.mv == lppos->mv || temp.mvMirror == lppos->mv)
                {
                    return lppos->vl;
                }
            }

        return COORD_MOVE(*(uint32_t *)s);
    };

    PositionStruct pos;

    PositionStruct posMirror; // 用于镜像棋盘

    int gameCount = 0;
    int moveCount = 0;
    bool gameValid = false;

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
            gameValid = true;
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

        if (gameCount == 0 || !gameValid)
            continue;

        if (!std::regex_search(line, MoveLineRegex))
            continue;

        for (std::sregex_iterator it(line.begin(), line.end(), IccsRegex), end;
             it != end; ++it)
        {
            std::string iccs = it->str();
            int mv = IccsToMove(iccs.c_str());
            if (mv != 0)
            {
                int mvMirror = MOVE_MIRROR(mv);
                if (!pos.LegalMove(mv))
                {
                    fprintf(stderr, "[WARN] skip invalid game %d at move %s\n", gameCount, iccs.c_str());
                    gameValid = false;
                    break;
                }
                uint32_t posLock0 = pos.zobr.dwLock0;
                uint32_t posLock1 = pos.zobr.dwLock1;
                uint32_t mirrorLock0 = posMirror.zobr.dwLock0;
                uint32_t mirrorLock1 = posMirror.zobr.dwLock1;
                int sdPlayer = pos.sdPlayer;
                bool capture = pos.ucpcSquares[DST(mv)] != 0;
                bool mirrorCapture = posMirror.ucpcSquares[DST(mvMirror)] != 0;
                if (!pos.MakeMove(mv))
                {
                    fprintf(stderr, "[WARN] skip invalid game %d at move %s\n", gameCount, iccs.c_str());
                    gameValid = false;
                    break;
                }
                if (!posMirror.MakeMove(mvMirror))
                {
                    fprintf(stderr, "[WARN] skip invalid mirrored game %d at move %s\n", gameCount, iccs.c_str());
                    gameValid = false;
                    break;
                }
                int vl = MoveValue(sdPlayer, pendingResult);
                int nComp;
                if (posLock1 < mirrorLock1)
                {
                    nComp = -1;
                }
                else if (posLock1 > mirrorLock1)
                {
                    nComp = 1;
                }
                else if (posLock0 < mirrorLock0)
                {
                    nComp = -1;
                }
                else if (posLock0 > mirrorLock0)
                {
                    nComp = 1;
                }
                else
                {
                    nComp = 0;
                }
                // 原厂按局面与镜像局面的 Zobrist 顺序规范化存储，查询端再还原镜像着法。
                if (nComp <= 0)
                {
                    AddTemp(TempStruct(posLock0, posLock1, mv, vl));
                }
                if (nComp >= 0)
                {
                    AddTemp(TempStruct(mirrorLock0, mirrorLock1, mvMirror, vl));
                }
                if (capture)
                {
                    pos.SetIrrev();
                }
                if (mirrorCapture)
                {
                    posMirror.SetIrrev();
                }
                moveCount++;
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
    // Iterate through all stored moves in the book
    for (int i = 0; i < MakeBook2.nTempLen; i++)
    {
        const TempStruct &temp = MakeBook2.TempBuffer[i];
        if (temp.mv == lppos->mv || temp.mvMirror == lppos->mv)
        {
            return lppos->vl;
        }
    }
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
            // 必须复用引擎的 pc -> pt 映射，保证制库与引擎的 Zobrist 键一致。
            int pt = PIECE_TYPE(pc);
            if (pc >= 32)
            {
                pt += 7;
            }
            zobr.Xor(PreGen.zobrTable[pt][sq]);
        }
    }

    // 5. 走子方
    if (sdPlayer == 1)
    {
        zobr.Xor(PreGen.zobrPlayer);
    }
}

// ==================== 新增：控制台程序入口 ====================
// 原作者写的是DLL，要增加main函数改造成可执行程序,用于批量导入单独pgn大文件
int main(int argc, char *argv[])
{
    if (argc != 7 || argv[1] == "?" || argv[1] == "h" || argv[1] == "hlp" || argv[1] == "help" || argv[1] == "-?" || argv[1] == "-h" || argv[1] == "-hlp" || argv[1] == "-help" || argv[1] == "--?" || argv[1] == "--h" || argv[1] == "--hlp" || argv[1] == "--help")
    {
        printf("用法: %s <PGN文件> <输出book> <nWin> <nDraw> <nLoss> <nRatio>\n", argv[0]);
        printf("平衡示例: %s WXF-41743games.pgns iccs_41743_v2.book 3 1 -1 4\n", argv[0]);
        printf("求胜示例: %s WXF-41743games.pgns iccs_41743_v2.book 5 0 -3 3\n", argv[0]);
        printf("防守示例: %s WXF-41743games.pgns iccs_41743_v2.book 2 3 -1 4\n", argv[0]);

        system("pause");
        return 1;
    }

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

    // Iterate through all stored moves in the book
    for (int i = 0; i < MakeBook2.nTempLen; i++)
    {
        const TempStruct &temp = MakeBook2.TempBuffer[i];
        if (temp.mv == lppos->mv || temp.mvMirror == lppos->mv)
        {
            return lppos->vl;
        }
    }
}