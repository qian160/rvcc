#include"rvcc.h"

extern char * CurrentInput;
extern char * CurrentFilename;

// 输出错误出现的位置，并退出
void verrorAt(char *Loc, char *Fmt, va_list VA) {
    // 查找包含loc的行
    char *Line = Loc;
    // Line递减到当前行的最开始的位置
    // Line<CurrentInput, 判断是否读取到文件最开始的位置
    // Line[-1] != '\n'，Line字符串前一个字符是否为换行符（上一行末尾）
    while (CurrentInput < Line && Line[-1] != '\n')
        Line--;

    // End递增到行尾的换行符
    char *End = Loc;
    while (*End != '\n')
        End++;

    // 获取行号
    int LineNo = 1;
    for (char *P = CurrentInput; P < Line; P++)
        // 遇到换行符则行号+1
        if (*P == '\n')
            LineNo++;

    // 输出 文件名:错误行
    // Indent记录输出了多少个字符
    int Indent = fprintf(stderr, "%s:%d: ", CurrentFilename, LineNo);
    // 输出Line的行内所有字符（不含换行符）
    fprintf(stderr, "%.*s\n", (int)(End - Line), Line);

    // 计算错误信息位置，在当前行内的偏移量+前面输出了多少个字符
    int Pos = Loc - Line + Indent;

    // 将字符串补齐为Pos位，因为是空字符串，所以填充Pos个空格。
    fprintf(stderr, "%*s", Pos, "");
    fprintf(stderr, "^ ");
    vfprintf(stderr, Fmt, VA);
    fprintf(stderr, "\n");
    va_end(VA);
}

// 字符解析出错
void errorAt(char *Loc, char *Fmt, ...) {
    va_list VA;
    va_start(VA, Fmt);
    verrorAt(Loc, Fmt, VA);
    exit(1);
}

// Tok解析出错
void errorTok(Token *Tok, char *Fmt, ...) {
    va_list VA;
    va_start(VA, Fmt);
    verrorAt(Tok->Loc, Fmt, VA);
    exit(1);
}