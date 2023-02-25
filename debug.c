#include"rvcc.h"

extern char * CurrentInput;

__attribute__((unused))
void print_one(Token *tok) {
    char * s = tok->Loc;
    while (tok -> Len --)
        putchar(*s++);
    println("");
}

// arg 'tok' is the head of linked list. debug use
__attribute__((unused))
void print_tokens(Token * tok) {
    while (tok)
    {
        print_one(tok);
        tok = tok -> Next;
    }
}

// 输出错误出现的位置，并退出
void verrorAt(char *Loc, char *Fmt, va_list VA) {
    // 先输出源信息
    fprintf(stderr, "%s\n", CurrentInput);

    // 输出出错信息
    // 计算出错的位置，Loc是出错位置的指针，CurrentInput是当前输入的首地址
    int Pos = Loc - CurrentInput;
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
