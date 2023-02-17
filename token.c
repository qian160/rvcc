#include<stdbool.h>
#include<string.h>
#include<stdlib.h>
#include<ctype.h>
#include"rvcc.h"

// 判断Tok的值是否等于指定值，没有用char，是为了后续拓展
bool equal(Token *Tok, char *Str) {
    // 比较字符串LHS（左部），RHS（右部）的前N位，S2的长度应大于等于N.
    // 比较按照字典序，LHS<RHS回负值，LHS=RHS返回0，LHS>RHS返回正值
    // 同时确保，此处的Op位数=N
    return memcmp(Tok->Loc, Str, Tok->Len) == 0 && Str[Tok->Len] == '\0';
}

// 跳过指定的Str
Token *skip(Token *Tok, char *Str) {
    if (!equal(Tok, Str))
        error("expect '%s'", Str);
    return Tok->Next;
}

// 返回TK_NUM的值
int getNumber(Token *Tok) {
    if (Tok->Kind != TK_NUM)
        error("expect a number");
    return Tok->Val;
}

// 生成新的Token
Token *newToken(TokenKind Kind, char *Start, char *End) {
    // 分配1个Token的内存空间
    Token *Tok = calloc(1, sizeof(Token));
    Tok->Kind = Kind;
    Tok->Loc = Start;
    Tok->Len = End - Start;
    return Tok;
}

// 终结符解析. try to generate a list of tokens from P
// (head) -> '1' -> '+' -> '2' -> '-' -> '10'
Token *tokenize(char *P) {
    Token Head = {};
    Token *Cur = &Head;

    while (*P) {
        // 跳过所有空白符如：空格、回车
        if (isspace(*P)) {
            ++P;
            continue;
        }
        // 解析数字
        if (isdigit(*P)) {
            // 初始化，类似于C++的构造函数
            // 我们不使用Head来存储信息，仅用来表示链表入口，这样每次都是存储在Cur->Next
            // 否则下述操作将使第一个Token的地址不在Head中。
            Cur->Next = newToken(TK_NUM, P, P);
            // 指针前进
            Cur = Cur->Next;
            const char *OldPtr = P;
            Cur->Val = strtoul(P, &P, 10);
            Cur->Len = P - OldPtr;
            continue;
        }
        // 解析操作符
        if (*P == '+' || *P == '-') {
            // 操作符长度都为1
            Cur->Next = newToken(TK_PUNCT, P, P + 1);
            Cur = Cur->Next;
            ++P;
            continue;
        }

        // 处理无法识别的字符
        error("invalid token: %c", *P);
    }
    // 解析结束，增加一个EOF，表示终止符。
    Cur->Next = newToken(TK_EOF, P, P);
    // Head无内容，所以直接返回Next
    return Head.Next;
}