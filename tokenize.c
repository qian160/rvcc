#include"rvcc.h"

// 判断Tok的值是否等于指定值
bool equal(Token *Tok, char *Str) {
    return memcmp(Tok->Loc, Str, Tok->Len) == 0 && Str[Tok->Len] == '\0';
}

// 当要比较的关键字较多时可以用这个。第二个参数可以在外面通过(sizeof(Kw) / sizeof(*Kw))获得.(pointer size, 8)
// usage of arg3: (char *[]){"...", "...", ".."}
bool equal2(Token *Tok, int n, char*kw[]){
    for(int i = 0; i < n; i++)
        if(equal(Tok, kw[i]))
            return true;
    return false;
}


File * CurrentFile;         // 输入文件
static File **InputFiles;   // 输入文件列表
static bool AtBOL;          // current token is "at begin of line"
static bool HasSpace;       // 终结符前是有空格时为真

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

// 生成新的Token
Token *newToken(TokenKind Kind, char *Start, char *End) {
    Token *Tok = calloc(1, sizeof(Token));
    Tok->Kind = Kind;
    Tok->Loc = Start;
    Tok->Len = End - Start;
    Tok->AtBOL = AtBOL;
    AtBOL = false;
    Tok->HasSpace = HasSpace;
    HasSpace = false;
    Tok->File = CurrentFile;
    return Tok;
}

// 判断Str是否以SubStr开头
static bool startsWith(char *Str, char *SubStr) {
    // 比较LHS和RHS的N个字符是否相等
    return strncmp(Str, SubStr, strlen(SubStr)) == 0;
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
static Token *readCharLiteral(char *Start, char *Quote) {
    char *P = Quote + 1;
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
    Tok->Ty = TyInt;
    // note: char literal is stored by an 'int'
    // sizeof('\0') == sizeof(0) == sizeof(int) == 4
    // Tok->Ty = TyChar;
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
    int64_t Val = strtoul(P, &P, Base);

    // 读取U L LL后缀
    bool L = false;
    bool U = false;

    // LLU / ULL
    // just note that the 2 "L"s must be same 
    if (startsWith(P, "LLU") || startsWith(P, "LLu") || startsWith(P, "llU") ||
        startsWith(P, "llu") || startsWith(P, "ULL") || startsWith(P, "Ull") ||
        startsWith(P, "uLL") || startsWith(P, "ull")) {
        P += 3;
        L = U = true;
    } else if (!strncasecmp(P, "lu", 2) || !strncasecmp(P, "ul", 2)) {
        // LU
        P += 2;
        L = U = true;
    } else if (startsWith(P, "LL") || startsWith(P, "ll")) {
        // LL
        P += 2;
        L = true;
    } else if (*P == 'L' || *P == 'l') {
        // L
        P++;
        L = true;
    } else if (*P == 'U' || *P == 'u') {
        // U
        P++;
        U = true;
    }

    // 推断出类型，采用能存下当前数值的类型
    Type *Ty;
    if (Base == 10) {
        if (L && U)
            Ty = TyULong;
        else if (L)
            Ty = TyLong;
        else if (U)
            Ty = (Val >> 32) ? TyULong : TyUInt;
        else
            Ty = (Val >> 31) ? TyLong : TyInt;
    } else {
        if (L && U)
            Ty = TyULong;
        else if (L)
            Ty = (Val >> 63) ? TyULong : TyLong;
        else if (U)
            Ty = (Val >> 32) ? TyULong : TyUInt;
        else if (Val >> 63)
        Ty = TyULong;
        else if (Val >> 32)
            Ty = TyLong;
        else if (Val >> 31)
            Ty = TyUInt;
        else
            Ty = TyInt;
    }

    // 构造NUM的终结符
    Token *Tok = newToken(TK_NUM, Start, P);
    Tok->Val = Val;
    Tok->Ty = Ty;
    return Tok;
}

// 读取数字
static Token *readNumber(char *Start) {
    // 尝试解析整型常量
    Token *Tok = readIntLiteral(Start);
    // 不带e或者f后缀，则为整型
    if (!strchr(".eEfF", Start[Tok->Len]))
        return Tok;
    // 如果不是整型，那么一定是浮点数
    char *End;
    double Val = strtod(Start, &End);
    // 处理浮点数后缀
    Type *Ty;
    if (*End == 'f' || *End == 'F') {
        Ty = TyFloat;
        End++;
    } else if (*End == 'l' || *End == 'L') {
        Ty = TyDouble;
        End++;
    } else {
        Ty = TyDouble;
    }

    // 构建浮点数终结符
    Tok = newToken(TK_NUM, Start, End);
    Tok->FVal = Val;
    Tok->Ty = Ty;
    return Tok;
}

// 读取到字符串字面量结尾
static char *stringLiteralEnd(char *P) {
    char *Start = P;
    for (; *P != '"'; P++) {
        if (*P == '\n' || *P == '\0')
        errorAt(Start, "unclosed string literal");
        if (*P == '\\')
        P++;
    }
    return P;
}

// 读取字符串字面量. *Start = "
static Token *readStringLiteral(char *Start) {
    // 读取到字符串字面量的右引号
    char *End = stringLiteralEnd(Start + 1);
    // 定义一个与字符串字面量内字符数+1的Buf
    // 用来存储最大位数的字符串字面量
    char *Buf = calloc(1, End - Start);
    // 实际的字符位数，一个转义字符为1位
    int Len = 0;

    // 将读取后的结果写入Buf
    for (char *P = Start + 1; P < End;) {
        if (*P == '\\') {
            Buf[Len++] = readEscapedChar(&P, P + 1);
        } else {
            Buf[Len++] = *P++;
        }
    }

    // Token这里需要包含带双引号的字符串字面量
    Token *Tok = newToken(TK_STR, Start, End + 1);
    Tok->Ty = arrayOf(TyChar, Len + 1);
    Tok->Str = Buf;
    return Tok;
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
        "<<=", ">>=", "...",    // ... is not true punct in fact. just let it to be read
        "==", "!=", "<=", ">=", "->", 
        "+=", "-=", "*=", "/=", "++", "--", 
        "%=", "^=", "|=", "&=", "&&", "||", "<<", ">>",
        "##"
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

// 为所有Token添加行号
static void addLineNumbers(Token *Tok) {
    char *P = CurrentFile->Contents;
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
Token *tokenize(File *FP) {
    // 设定当前文件
    CurrentFile = FP;
    // 读取相应的内容
    char *P = FP->Contents;

    Token Head = {};
    Token *Cur = &Head;

    // 文件开始设置为行首
    AtBOL = true;
    HasSpace = false;

    while (*P) {
        // 跳过行注释
        if (startsWith(P, "//")) {
            P += 2;
            while (*P != '\n')
                P++;
            HasSpace = true;
            continue;
        }

        // 跳过块注释
        if (startsWith(P, "/*")) {
            // 查找第一个"*/"的位置
            char *Q = strstr(P + 2, "*/");
            if (!Q)
                errorAt(P, "unclosed block comment");
            P = Q + 2;
            HasSpace = true;
            continue;
        }

        // 匹配换行符，设置为行首
        if (*P == '\n') {
            P++;
            AtBOL = true;
            HasSpace = true;
            continue;
        }

        // 跳过所有空白符如：空格、回车
        if (isspace(*P)) {
            ++P;
            HasSpace = true;
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
            Cur->Next = readCharLiteral(P, P);
            Cur = Cur->Next;
            P += Cur->Len;
            continue;
        }

        // 宽字符字面量，占两个字节
        if (startsWith(P, "L'")) {
            Cur = Cur->Next = readCharLiteral(P, P + 1);
            P += Cur->Len;
            continue;
        }


        // 解析整型和浮点数
        if (isdigit(*P) || (*P == '.' && isdigit(P[1]))) {
            Cur->Next = readNumber(P);
            // 我们不使用Head来存储信息，仅用来表示链表入口，这样每次都是存储在Cur->Next
            // 否则下述操作将使第一个Token的地址不在Head中。
            // 读取数字字面量
            // 指针前进
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

// 获取输入文件
File **getInputFiles(void) { return InputFiles; }

// 新建一个File
File *newFile(char *Name, int FileNo, char *Contents) {
    File *FP = calloc(1, sizeof(File));
    FP->Name = Name;
    FP->FileNo = FileNo;
    FP->Contents = Contents;
    return FP;
}

// 移除续行，即反斜杠+换行符的形式
static void removeBackslashNewline(char *P) {
    // 旧字符串的索引I（从0开始）
    // 新字符串的索引J（从0开始）
    // 因为J始终<=I，所以二者共用空间，不会有问题
    int I = 0, J = 0;

    // 为了维持行号不变，这里记录了删除的行数
    int N = 0;

    // 如果指向的字符存在
    while (P[I]) {
        // 如果是 '\\'和'\n'
        if (P[I] == '\\' && P[I + 1] == '\n') {
            // I跳过这两个字符
            I += 2;
            // 删除的行数+1
            N++;
        }
        // 如果是换行符
        else if (P[I] == '\n') {
            // P[J]='\n'
            // I、J都+1
            P[J++] = P[I++];
            // 如果删除过N个续行，那么在这里增加N个换行
            // 以保证行号不变
            for (; N > 0; N--)
                P[J++] = '\n';
            }
            // 其他情况，P[J]=P[I]
            // I、J都+1
        else {
            P[J++] = P[I++];
        }
    }

    // 如果最后还删除过N个续行，那么在这里增加N个换行
    for (; N > 0; N--)
        P[J++] = '\n';
    // 以'\0'结束
    P[J] = '\0';
}


// 词法分析文件
Token *tokenizeFile(char *Path) {
    // 读取文件内容
    char *P = readFile(Path);
    if (!P)
        return NULL;

    removeBackslashNewline(P);
    // 文件编号
    static int FileNo;
    // 文件路径，文件编号从1开始，文件内容
    File *FP = newFile(Path, FileNo + 1, P);

    // 为汇编的.file指示保存文件名
    // 最后字符串为空，作为结尾。
    // realloc根据(FileNo + 2)重新分配给定的内存区域
    InputFiles = realloc(InputFiles, sizeof(char *) * (FileNo + 2));
    // 当前文件存入字符串对应编号-1位置
    InputFiles[FileNo] = FP;
    // 最后字符串为空，作为结尾。
    InputFiles[FileNo + 1] = NULL;
    // 文件编号加1
    FileNo++;

    return tokenize(FP);
}
