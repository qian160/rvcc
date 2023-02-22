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
    Assert(equal(Tok, Str), "expect '%s'", Str);
    return Tok->Next;
}

char* tokenName(Token *Tok) {
    return strndup(Tok->Loc, Tok->Len);
}

// 返回TK_NUM的值
int getNumber(Token *Tok) {
    Assert(Tok -> Kind == TK_NUM, "expect a number");
    return Tok->Val;
}

// 消耗掉指定Token
bool consume(Token **Rest, Token *Tok, char *Str) {
    // 存在
    if (equal(Tok, Str)) {
        *Rest = Tok->Next;
        return true;
    }
    // 不存在
    *Rest = Tok;
    return false;
}

// 判断是否为关键字
static bool isKeyword(Token *Tok) {
    // 关键字列表
    static char *Kw[] = {"return", "if", "else", "for", "while", "int"};

    // 遍历关键字列表匹配
    for (int I = 0; I < sizeof(Kw) / sizeof(*Kw); ++I) {
        if (equal(Tok, Kw[I]))
            return true;
    }
    return false;
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

// 判断Str是否以SubStr开头
static bool startsWith(char *Str, char *SubStr) {
    // 比较LHS和RHS的N个字符是否相等
    return strncmp(Str, SubStr, strlen(SubStr)) == 0;
}

// 判断标记符的首字母规则
// [a-zA-Z_]
static bool isIdent1(char C) {
    // a-z与A-Z在ASCII中不相连，所以需要分别判断
    return ('a' <= C && C <= 'z') || ('A' <= C && C <= 'Z') || C == '_';
}

// 判断标记符的非首字母的规则
// [a-zA-Z0-9_]
static bool isIdent2(char C) { 
    return isIdent1(C) || ('0' <= C && C <= '9');
}

// 读取操作符, return the length
static int readPunct(char *Ptr) {
    // 判断2字节的操作符
    if (startsWith(Ptr, "==") || startsWith(Ptr, "!=") || startsWith(Ptr, "<=") ||
        startsWith(Ptr, ">="))
        return 2;

    // 判断1字节的操作符.
    // + - * / | & % ^ ! ~ = ; , . ( ) [ ] ? :
    return ispunct(*Ptr) ? 1 : 0;
}

__attribute__((unused))
static void print_one(Token *tok) {
    char * s = tok->Loc;
    while (tok -> Len --)
        putchar(*s++);
    println("");
}

// arg 'tok' is the head of linked list. debug use
__attribute__((unused))
static void print_tokens(Token * tok) {
    while (tok)
    {
        print_one(tok);
        tok = tok -> Next;
    }
}

// 将名为xxx的终结符转为KEYWORD
static void convertKeywords(Token *Tok) {
    for (Token *T = Tok; T->Kind != TK_EOF; T = T->Next) {
        if (isKeyword(T))
            T->Kind = TK_KEYWORD;
    }
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

        // 解析标记符
        // [a-zA-Z_][a-zA-Z0-9_]*
        if (isIdent1(*P)) {
            char *Start = P;
            do {
                ++P;
            } while (isIdent2(*P));
            Cur->Next = newToken(TK_IDENT, Start, P);
            Cur = Cur->Next;
            continue;
        }
        // 解析操作符
        int PunctLen = readPunct(P);
        if (PunctLen) {
            Cur->Next = newToken(TK_PUNCT, P, P + PunctLen);
            Cur = Cur->Next;
            // 指针前进Punct的长度位
            P += PunctLen;
            continue;
        }

        // 处理无法识别的字符
        error("invalid token: %c", *P);
    }
    // 解析结束，增加一个EOF，表示终止符。
    Cur->Next = newToken(TK_EOF, P, P);
    // 将所有关键字的终结符，都标记为KEYWORD
    // 这样他们在之后就不会被误解析成其他类型比如ident了
    convertKeywords(Head.Next);
    // Head无内容，所以直接返回Next
    return Head.Next;
}