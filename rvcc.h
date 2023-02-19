#include<stdarg.h>
#include<stdio.h>
// put some data structures and useful macros here

// 为每个终结符(token)都设置种类来表示
typedef enum {
    TK_PUNCT, // 操作符如： + -
    TK_NUM,   // 数字
    TK_EOF,   // 文件终止符，即文件的最后
} TokenKind;

// 终结符结构体
typedef struct Token Token;
struct Token {
    TokenKind Kind; // 种类
    Token *Next;    // 指向下一终结符
    int Val;        // 值
    char *Loc;      // 在解析的字符串内的位置
    int Len;        // 长度
};

// AST的节点种类
typedef enum {
    ND_ADD, // +
    ND_SUB, // -
    ND_MUL, // *
    ND_DIV, // /
    ND_NUM, // 整形
} NodeKind;

// AST中二叉树节点
typedef struct Node Node;
struct Node {
    NodeKind Kind; // 节点种类
    Node *LHS;     // 左部，left-hand side
    Node *RHS;     // 右部，right-hand side
    int Val;       // 存储ND_NUM种类的值
};

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
