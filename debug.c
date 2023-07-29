#include "rvcc.h"
#include <execinfo.h>
extern char * CurrentInput;
extern File * CurrentFile;         // 输入文件

// 输出错误出现的位置，并退出
void verrorAt(char *Filename, char *Input, int LineNo, char *Loc, char *Fmt, va_list VA) {
    // 查找包含loc的行
    char *Line = Loc;
    // Line递减到当前行的最开始的位置
    // Line<CurrentInput, 判断是否读取到文件最开始的位置
    // Line[-1] != '\n'，Line字符串前一个字符是否为换行符（上一行末尾）
    while (Input < Line && Line[-1] != '\n')
        Line--;

    // End递增到行尾的换行符
    char *End = Loc;
    while (*End != '\n')
        End++;

    // 输出 文件名:错误行
    // Indent记录输出了多少个字符
    int Indent = fprintf(stderr, "%s:%d: ", Filename, LineNo);
    // 输出Line的行内所有字符（不含换行符）
    fprintf(stderr, "%.*s\n", (int)(End - Line), Line);

    // 计算错误信息位置，在当前行内的偏移量+前面输出了多少个字符
    int Pos = displayWidth(Line, Loc - Line) + Indent;

    // 将字符串补齐为Pos位，因为是空字符串，所以填充Pos个空格。
    fprintf(stderr, "%*s", Pos, "");
    fprintf(stderr, "^ ");
    vfprintf(stderr, Fmt, VA);
    fprintf(stderr, " 😵\n");
    va_end(VA);
}

// 字符解析出错
noreturn void errorAt(char *Loc, char *Fmt, ...) {
    int LineNo = 1;
    for (char *P = CurrentFile->Contents; P < Loc; P++)
        if (*P == '\n')
        LineNo++;

    va_list VA;
    va_start(VA, Fmt);
    verrorAt(CurrentFile->Name, CurrentFile->Contents, LineNo, Loc, Fmt, VA);
    exit(1);
}

// Tok解析出错
noreturn void errorTok(Token *Tok, char *Fmt, ...) {
    va_list VA;
    va_start(VA, Fmt);
    verrorAt(Tok->File->Name, Tok->File->Contents,  Tok->LineNo, Tok->Loc, Fmt, VA);
    exit(1);
}

// Tok解析警告
void warnTok(Token *Tok, char *Fmt, ...) {
    va_list VA;
    va_start(VA, Fmt);
    verrorAt(Tok->File->Name, Tok->File->Contents, Tok->LineNo, Tok->Loc, Fmt, VA);
    va_end(VA);
}
/*
// not so useful...
void print_call_stack(int depth)
{
    void *call_stack[depth];
    int num_entries = backtrace(call_stack, depth);
    char **symbols = backtrace_symbols(call_stack, num_entries);

    fprintf(stderr, "call stack:\n");
    for (int i = 0; i < num_entries; i++)
        fprintf(stderr, "%s\n", symbols[i]);

    free(symbols);
}

void error(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    exit(1);
}

void Assert(int cond, char *fmt, ...) {
    if(!cond)
        error(fmt, "FAIL");
}
*/
