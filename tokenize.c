#include"rvcc.h"

// 判断Tok的值是否等于指定值
bool equal(Token *Tok, char *Str) {
    return memcmp(Tok->Loc, Str, Tok->Len) == 0 && Str[Tok->Len] == '\0';
}

// 当要比较的关键字较多时可以用这个。第二个参数可以在外面通过(sizeof(Kw) / sizeof(*Kw))获得.(pointer size, 8)
bool equal2(Token *Tok, int n, char*kw[]){
    for(int i = 0; i < n; i++)
        if(equal(Tok, kw[i]))
            return true;
    return false;
}

char * CurrentInput;
char * CurrentFilename;

// 跳过指定的Str
Token *skip(Token *Tok, char *Str) {
    if(!equal(Tok, Str))
        errorTok(Tok, "expect '%s'", Str);
    return Tok->Next;
}

char* tokenName(Token *Tok) {
    return strndup(Tok->Loc, Tok->Len);
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
    static char *Kw[] = 
        {   "return", "if", "else", "for", "do",
            "while", "int", "sizeof", "char", "struct", "union", 
            "long", "short, void", "typedef", "_Bool", "enum", "static",
            "goto", "break", "continue", "switch", "case", "default", "extern",
            "_Alignof", "_Alignas",
        };

    return equal2(Tok, sizeof(Kw) / sizeof(*Kw), Kw);
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

// 返回一位十六进制转十进制
// hexDigit = [0-9a-fA-F]
// 16: 0 1 2 3 4 5 6 7 8 9  A  B  C  D  E  F
// 10: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
static int fromHex(char C) {
    if ('0' <= C && C <= '9')
        return C - '0';
    if ('a' <= C && C <= 'f')
        return C - 'a' + 10;
    return C - 'A' + 10;
}

// 读取转义字符
static int readEscapedChar(char **NewPos, char * P) {
    // 八进制转义字符
    if ('0' <= *P && *P <= '7') {
        // 读取一个八进制数字，不能长于三位
        // \abc = (a*8+b)*8+c
        int C = 0, n = 0;
        for(; '0' <= *P && *P <= '7' && n++ < 3; P++)
            C = (C << 3) + *P - '0';
        *NewPos = P;
        return C;
    }
    // 十六进制转义字符
    if (*P == 'x') {
        P++;
        // 判断是否为十六进制数字
        if (!isxdigit(*P))
            errorAt(P, "invalid hex escape sequence");

        int C = 0;
        // 读取一位或多位十六进制数字
        // \xWXYZ = ((W*16+X)*16+Y)*16+Z
        for (; isxdigit(*P); P++)
            C = (C << 4) + fromHex(*P);
        *NewPos = P;
        return C;
    }

    *NewPos = P + 1;

    switch (*P) {
        case 'a': // 响铃（警报）
            return '\a';
        case 'b': // 退格
            return '\b';
        case 't': // 水平制表符，tab
            return '\t';
        case 'n': // 换行
            return '\n';
        case 'v': // 垂直制表符
            return '\v';
        case 'f': // 换页
            return '\f';
        case 'r': // 回车
            return '\r';
        // 属于GNU C拓展
        case 'e': // 转义符
            return 27;
        default: // 默认将原字符返回
            return *P;
    }
}

// 读取字符字面量
static Token *readCharLiteral(char *Start) {
    char *P = Start + 1;
    // 解析字符为 \0 的情况
    if (*P == '\0')
        errorAt(Start, "unclosed char literal");

    // 解析字符
    char C;
    // 转义
    if (*P == '\\')
        C = readEscapedChar(&P, P + 1);
    else
        C = *P++;

    // strchr返回以 ' 开头的字符串，若无则为NULL
    char *End = strchr(P, '\'');
    if (!End)
        errorAt(P, "unclosed char literal");

    // 构造一个NUM的终结符，值为C的数值
    Token *Tok = newToken(TK_NUM, Start, End + 1);
    Tok->Val = C;
    return Tok;
}

// 读取数字字面量
static Token *readIntLiteral(char *Start) {
    char *P = Start;

    // 读取二、八、十、十六进制
    // 默认为十进制
    int Base = 10;
    // 比较两个字符串前2个字符，忽略大小写，并判断是否为数字
    if (!strncasecmp(P, "0x", 2) && isxdigit(P[2])) {
        // 十六进制
        P += 2;
        Base = 16;
    } else if (!strncasecmp(P, "0b", 2) && (P[2] == '0' || P[2] == '1')) {
        // 二进制
        P += 2;
        Base = 2;
    } else if (*P == '0') {
        // 八进制
        Base = 8;
    }

    // 将字符串转换为Base进制的数字
    long Val = strtoul(P, &P, Base);
    if (isalnum(*P))
        errorAt(P, "invalid digit");
    
    // 构造NUM的终结符
    Token *Tok = newToken(TK_NUM, Start, P);
    Tok->Val = Val;
    return Tok;
}


// 读取字符串字面量. *Start = "
static Token *readStringLiteral(char *Start) {
    // check legality and compute length
    char *P = Start + 1;
    int len = 0;
    for(; *P != '"'; P++){
        if(*P == '\n' || *P == '\0')
            error("unclosed string literal: %s", Start);
        if(*P == '\\') 
            P++;
        len++;
    }
    len++;      // '\0'
    char * Buf = calloc(1, len);

    int i = 0;
    // 将读取后的结果写入Buf
    for (char *P = Start + 1; *P != '"';) {
        if (*P == '\\') {
            Buf[i++] = readEscapedChar(&P, P + 1);
        } else {
            Buf[i++] = *P++;
        }
    }

    // Token这里需要包含带双引号的字符串字面量
    Token *Tok = newToken(TK_STR, Start, P + 1);
    // 为\0增加一位
    Tok->strLen = len;
    Tok->Str = Buf;
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
    // 判断多字节的操作符. note: "<<=" need to be checked before <<
    static char *Kw[] = {
        "<<=", ">>=",
        "==", "!=", "<=", ">=", "->", 
        "+=", "-=", "*=", "/=", "++", "--", 
        "%=", "^=", "|=", "&=", "&&", "||", "<<", ">>"
    };

    // 遍历列表匹配Ptr字符串
    for (int I = 0; I < sizeof(Kw) / sizeof(*Kw); ++I) {
        if (startsWith(Ptr, Kw[I]))
            return strlen(Kw[I]);
    }

    // 判断1字节的操作符.
    // + - * / | & % ^ ! ~ = ; , . ( ) [ ] ? : " ' ...
    return ispunct(*Ptr) ? 1 : 0;
}

// 将名为xxx的终结符转为KEYWORD
static void convertKeywords(Token *Tok) {
    for (Token *T = Tok; T->Kind != TK_EOF; T = T->Next) {
        if (isKeyword(T))
            T->Kind = TK_KEYWORD;
    }
}

// 为所有Token添加行号
static void addLineNumbers(Token *Tok) {
    char *P = CurrentInput;
    int N = 1;

    do {
        if (P == Tok->Loc) {
            Tok->LineNo = N;
            Tok = Tok->Next;
        }
        if (*P == '\n')
            N++;
    } while (*P++);
}


// 终结符解析，文件名，文件内容
static Token *tokenize(char *Filename, char *P) {
    CurrentFilename = Filename;
    CurrentInput = P;
    Token Head = {};
    Token *Cur = &Head;

    while (*P) {
        // 跳过行注释
        if (startsWith(P, "//")) {
            P += 2;
            while (*P != '\n')
                P++;
            continue;
        }

        // 跳过块注释
        if (startsWith(P, "/*")) {
            // 查找第一个"*/"的位置
            char *Q = strstr(P + 2, "*/");
            if (!Q)
                errorAt(P, "unclosed block comment");
            P = Q + 2;
            continue;
        }
        // 跳过所有空白符如：空格、回车
        if (isspace(*P)) {
            ++P;
            continue;
        }

        // 解析数字
        if (isdigit(*P)) {
            // 我们不使用Head来存储信息，仅用来表示链表入口，这样每次都是存储在Cur->Next
            // 否则下述操作将使第一个Token的地址不在Head中。
            // 读取数字字面量
            Cur->Next = readIntLiteral(P);
            // 指针前进
            Cur = Cur->Next;
            P += Cur->Len;
            continue;
        }

        // 解析字符串字面量
        if (*P == '"') {
            Cur->Next = readStringLiteral(P);
            Cur = Cur->Next;
            P += Cur->Len;
            continue;
        }

        // 解析字符字面量
        if (*P == '\'') {
            Cur->Next = readCharLiteral(P);
            Cur = Cur->Next;
            P += Cur->Len;
            continue;
        }

        // 解析标记符或关键字
        // [a-zA-Z_][a-zA-Z0-9_]*
        if (isIdent1(*P)) {
            char *Start = P;
            while (isIdent2(*++P));
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
        errorAt(P, "invalid token");
    }

    // 解析结束，增加一个EOF，表示终止符。
    Cur->Next = newToken(TK_EOF, P, P);
    // 为所有Token添加行号
    addLineNumbers(Head.Next);

    // 将所有关键字的终结符，都标记为KEYWORD
    convertKeywords(Head.Next);
    // Head无内容，所以直接返回Next
    return Head.Next;
}

// 返回指定文件的内容
static char *readFile(char *Path) {
    FILE *FP;
    // at first I try to simply use fseek + ftell + fread to read
    // all the data at one time, but fseek fails for stdin...
    if (strcmp(Path, "-") == 0) {
        // 如果文件名是"-"，那么就从输入中读取
        FP = stdin;
    } else {
        FP = fopen(Path, "r");
        if (!FP)
            error("cannot open %s: %s", Path, strerror(errno));
    }

    // 要返回的字符串
    char *Buf;
    size_t BufLen;
    FILE *Out = open_memstream(&Buf, &BufLen);

    // 读取整个文件
    while(true) {
        char temp[4096];
        int N = fread(temp, 1, sizeof(temp), FP);
        if (N == 0)
            break;
        // 数组指针temp，数组元素大小1，实际元素个数N，文件流指针
        fwrite(temp, 1, N, Out);
    }

    // 对文件完成了读取
    if (FP != stdin)
        fclose(FP);

    // 刷新流的输出缓冲区，确保内容都被输出到流中
    fflush(Out);
    // 确保最后一行以'\n'结尾
    if (BufLen == 0 || Buf[BufLen - 1] != '\n')
        // 将字符输出到流中
        fputc('\n', Out);
    fputc('\0', Out);
    fclose(Out);
    return Buf;
}

// 对文件进行词法分析
Token *tokenizeFile(char *Path) {
    return tokenize(Path, readFile(Path));
}