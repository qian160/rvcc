#include<stdarg.h>
#include<stdio.h>
#include<stdlib.h>
#include<ctype.h>
#include<stdarg.h>
#include<stdbool.h>
#include<string.h>
/*
// 使用POSIX.1标准
// 使用了strndup函数:
    char *c = "0123456789";
    char *p = strndup(c, 6);
    // p = "012345";

*/
#define _POSIX_C_SOURCE 200809L

typedef struct Token Token;
typedef struct Node Node;
typedef struct Obj Obj;
typedef struct Function Function;
typedef struct Node Node;

// put some data structures and useful macros here

//
// 共用头文件，定义了多个文件间共同使用的函数和数据
//

//
// 终结符分析，词法分析
//

// 为每个终结符(token)都设置种类来表示
typedef enum {
    TK_IDENT,   // 标记符，可以为变量名、函数名等
    TK_PUNCT,   // 操作符如： + -
    TK_KEYWORD, // 关键字
    TK_NUM,     // 数字
    TK_EOF,     // 文件终止符，即文件的最后
} TokenKind;

// 终结符结构体
struct Token {
    TokenKind Kind; // 种类
    Token *Next;    // 指向下一终结符
    int Val;        // 值
    char *Loc;      // 在解析的字符串内的位置
    int Len;        // 长度
};

//
// 生成AST（抽象语法树），语法解析
//

// 本地变量
// connected by a linked list
struct Obj {
    Obj *Next;  // 指向下一对象
    char *Name; // 变量名
    int Offset; // fp的偏移量
};

// 函数. currently the only function is "main"
struct Function {
    Node *Body;    // 函数体, made up by statements.
    Obj *Locals;   // 本地变量,
    int StackSize; // 栈大小
};

// AST的节点种类
typedef enum {
    ND_ADD,         // +
    ND_SUB,         // -
    ND_MUL,         // *
    ND_DIV,         // /
    ND_NEG,         // 负号-
    ND_EQ,          // ==
    ND_NE,          // !=
    ND_LT,          // <
    ND_LE,          // <=
    ND_EXPR_STMT,   // 表达式语句
    ND_VAR,         // 变量
    ND_ASSIGN,      // 赋值
    ND_NUM,         // 整形
    ND_RETURN,      // 返回
    ND_BLOCK,       // { ... }，代码块
    ND_IF           // "if"，条件判断
} NodeKind;

// AST中二叉树节点
struct Node {
    NodeKind Kind; // 节点种类
    Node *Next;    // 下一节点，指代下一语句
    Node *LHS;     // 左部，left-hand side
    Node *RHS;     // 右部，right-hand side
    Node *Body;    // 代码块;存储了{}内解析的语句
    Obj * Var;     // 存储ND_VAR的字符串
    // "if"语句
    Node *Cond;    // 条件内的表达式
    Node *Then;    // 符合条件后的语句
    Node *Els;     // 不符合条件后的语句

    int Val;       // 存储ND_NUM种类的值
};



// functions

/* ---------- tokenize.c ---------- */
// 词法分析
Token* tokenize(char* P);
bool equal(Token *Tok, char *Str);
Token *skip(Token *Tok, char *Str);

/* ---------- parse.c ---------- */
// 语法解析入口函数
Function *parse(Token *Tok);

/* ---------- codegen.c ---------- */
// 代码生成入口函数
void codegen(Function *Prog);

#define error(format, ...) \
    do{ \
        printf("\33[1;31m" "[%s:%d %s] " format "\33[0m" "\n", \
            __FILE__, __LINE__, __func__, ## __VA_ARGS__);\
        exit(1);\
    }\
    while(0);

#define Assert(cond, fmt, ...) \
    do{\
        if(!(cond)) error(fmt, ## __VA_ARGS__);\
    }\
    while(0);

#define println(format, ...) printf(format "\n", ## __VA_ARGS__)
