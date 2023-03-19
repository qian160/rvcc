#include "rvcc.h"

// 是否行首是#号
static bool isHash(Token *Tok) { return Tok->AtBOL && equal(Tok, "#"); }

static Token *copyToken(Token *Tok) {
    Token *T = calloc(1, sizeof(Token));
    *T = *Tok;
    T->Next = NULL;
    return T;
}

// 将Tok2放入Tok1的尾部
static Token *append(Token *Tok1, Token *Tok2) {
    // Tok1为空时，直接返回Tok2
    if (!Tok1 || Tok1->Kind == TK_EOF)
        return Tok2;

    Token Head = {};
    Token *Cur = &Head;

    // 遍历Tok1，存入链表
    for (; Tok1 && Tok1->Kind != TK_EOF; Tok1 = Tok1->Next)
        Cur = Cur->Next = copyToken(Tok1);

    // 链表后接Tok2
    Cur->Next = Tok2;
    // 返回下一个
    return Head.Next;
}

// 遍历终结符，处理宏和指示
static Token *preprocess2(Token *Tok) {
    Token Head = {};
    Token *Cur = &Head;

    // 遍历终结符
    while (Tok->Kind != TK_EOF) {
        // 如果不是#号开头则前进
        if (!isHash(Tok)) {
            Cur->Next = Tok;
            Cur = Cur->Next;
            Tok = Tok->Next;
            continue;
        }

        Tok = Tok->Next;

        // 匹配#include
        if (equal(Tok, "include")) {
            // 跳过"
            Tok = Tok->Next;

            // 需要后面跟文件名
            if (Tok->Kind != TK_STR)
                errorTok(Tok, "expected a filename");

            // 以当前文件所在目录为起点
            // 路径为：终结符文件名所在的文件夹路径/当前终结符名
            char *Path = format("%s/%s", dirname(strdup(Tok->File->Name)), Tok->Str);
            // 词法解析文件
            Token *Tok2 = tokenizeFile(Path);
            if (!Tok2)
                errorTok(Tok, "%s", strerror(errno));
            // 将Tok2接续到Tok->Next的位置
            Tok = append(Tok2, Tok->Next);
            continue;
        }

        // 支持空指示
        if (Tok->AtBOL)
            continue;

        errorTok(Tok, "invalid preprocessor directive");
    }

    Cur->Next = Tok;
    return Head.Next;
}

// 判断是否为关键字
static bool isKeyword(Token *Tok) {
    // 关键字列表
    static char *Kw[] = 
        {   "return", "goto","if", "else", "for", "do","while", 
            "break", "continue", "switch", "case", "default",
            "int", "long", "short, void", "char", "_Bool", "float", "double",
            "struct", "union",  "typedef", "enum", 
            "extern", "sizeof", "static", "signed", "unsigned",
            "_Alignof", "_Alignas", "const", "volatile", "auto", "register", 
            "restrict", "__restrict", "__restrict__", "_Noreturn",
        };

    return equal2(Tok, sizeof(Kw) / sizeof(*Kw), Kw);
}

// 将名为xxx的终结符转为KEYWORD
static void convertKeywords(Token *Tok) {
    for (Token *T = Tok; T->Kind != TK_EOF; T = T->Next) {
        if (isKeyword(T))
            T->Kind = TK_KEYWORD;
    }
}

// 预处理器入口函数
Token *preprocess(Token *Tok) {
    // 处理宏和指示
    Tok = preprocess2(Tok);
    // 将所有关键字的终结符，都标记为KEYWORD
    convertKeywords(Tok);
    return Tok;
}
