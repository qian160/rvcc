#include "rvcc.h"


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
    // 将所有关键字的终结符，都标记为KEYWORD
    convertKeywords(Tok);
    return Tok;
}
