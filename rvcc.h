#ifndef __RVCC__H__

#define __RVCC__H__ 

#include<stdarg.h>
#include<stdio.h>
#include<stdlib.h>
#include<ctype.h>
#include<stdarg.h>
#include<stdbool.h>
#include<errno.h>
#include<string.h>
#include<stdint.h>
#include<strings.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<unistd.h>
#include<libgen.h>
#include<glob.h>
#include<sys/stat.h>
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
typedef struct Type Type;
typedef struct Member Member;
typedef struct Relocation Relocation;
typedef struct File File;
typedef struct Hideset Hideset;

//
// 共用头文件，定义了多个文件间共同使用的函数和数据
//

// 字符串数组
typedef struct {
    char **Data;  // 数据内容
    int Capacity; // 能容纳字符串的容量
    int Len;      // 当前字符串的数量，Len ≤ Capacity
} StringArray;


//
// 终结符分析，词法分析
//

// 为每个终结符(token)都设置种类来表示
typedef enum {
    TK_IDENT,   // 标记符，可以为变量名、函数名等
    TK_PUNCT,   // 操作符如： + -
    TK_KEYWORD, // 关键字
    TK_NUM,     // 数字
    TK_STR,     // 字符串字面量
    TK_PP_NUM,  // 预处理数值
    TK_EOF,     // 文件终止符，即文件的最后
} TokenKind;

// 终结符结构体
struct Token {
    TokenKind Kind; // 种类
    Token *Next;    // 指向下一终结符
    int64_t Val;    // 值
    double FVal;    // TK_NUM浮点值
    char *Loc;      // 在解析的字符串内的位置
    int Len;        // 长度
    Type *Ty;       // TK_NUM或TK_STR使用
    char *Str;      // 字符串字面量，包括'\0'
    File *File;     // 源文件位置
    int LineNo;     // 行号
    bool AtBOL;     // 终结符在行首(begin of line)
    bool HasSpace;    // 终结符前是否有空格
    Hideset *Hideset; // 用于宏展开时的隐藏集
    Token *Origin;    // 宏展开前的原始终结符
};

// 文件
typedef struct File{
    char *Name;     // 文件名
    int FileNo;     // 文件编号，从1开始
    char *Contents; // 文件内容
} File;

//
// 生成AST（抽象语法树），语法解析
//

// both variables and functions are objects
// connected by a linked list
struct Obj {
    Obj *Next;      // 指向下一对象
    char *Name;     // 变量名/函数名
    int Offset;     // fp的偏移量
    Type *Ty;       // 变量类型
    bool IsLocal;   // 是局部变量
    bool IsStatic;  // 是否为文件域内的
    bool IsDefinition; // 是否为函数定义
    int Align;      // 对齐量
    Token *Tok;     // 对应的终结符
    // 函数
    Obj *Params;    // 形参
    Node *Body;     // 函数体
    Obj *Locals;    // 本地变量
    int StackSize;  // 栈大小
    // 全局变量
    char *InitData;  // 用于初始化的数据
    Relocation *Rel; // 指向其他全局变量的指针
    Obj *VaArea;     // 可变参数区域
    // 结构体类型
    bool IsHalfByStack; // 一半用寄存器，一半用栈
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
    ND_STMT_EXPR,   // 语句表达式
    ND_VAR,         // 变量
    ND_ASSIGN,      // 赋值
    ND_NUM,         // 整形
    ND_RETURN,      // 返回
    ND_BLOCK,       // { ... }，代码块
    ND_IF,          // "if"，条件判断
    ND_FOR,         // "for" 或 "while" 循环
    ND_DO,          // "do"，用于do while语句
    ND_ADDR,        // 取地址 &
    ND_DEREF,       // 解引用 *
    ND_FUNCALL,     // 函数调用
    ND_COMMA,       // , 逗号
    ND_MEMBER,      // . 结构体成员访问
    ND_CAST,        // 类型转换
    ND_NOT,         // !，非
    ND_BITNOT,      // ~，按位取非
    ND_MOD,         // %，取余
    ND_BITAND,      // &，按位与
    ND_BITOR,       // |，按位或
    ND_BITXOR,      // ^，按位异或
    ND_LOGAND,      // &&，与
    ND_LOGOR,       // ||，或
    ND_GOTO,        // goto，直接跳转语句
    ND_LABEL,       // 标签语句
    ND_SWITCH,      // "switch"，分支语句
    ND_CASE,        // "case"
    ND_SHL,         // <<，左移
    ND_SHR,         // >>，右移
    ND_COND,        // ?:，条件运算符
    ND_NULL_EXPR,   // 空表达式
    ND_MEMZERO,     // 栈中变量清零
} NodeKind;

// AST中二叉树节点
struct Node {
    // node*中都是存储了一串指令(保存至ast中)。
    // 可理解为指向另外一颗树的根节点
    NodeKind Kind;  // 节点种类
    Node *Next;     // 下一节点，指代下一语句
    Node *LHS;      // 左部，left-hand side. unary node only uses this side
    Node *RHS;      // 右部，right-hand side
    Node *Body;     // 代码块 或 语句表达式
    Obj * Var;      // 存储ND_VAR种类的变量
    Type *Ty;       // 节点中数据的类型
    int64_t Val;    // 存储ND_NUM种类的值
    double FVal;    // 存储ND_NUM种类的浮点值
    Token * Tok;    // 节点对应的终结符. debug
    // 函数
    Node *Args;         // 函数被调用时代入的实参，可看作是一串表达式链表。 形参则保存在Nd->Ty->Parms中
    Type *FuncType;     // 函数类型
    bool PassByStack;   // 通过栈传递
    Obj *RetBuffer;     // 返回值缓冲区
    // "if"语句
    Node *Cond;     // 条件内的表达式
    Node *Then;     // 符合条件后的语句(do/while/if代码块内的语句)
    Node *Els;      // 不符合条件后的语句
    // "for"语句. 循环体存储在Then里
    Node *Init;     // 初始化语句
    Node *Inc;      // 递增语句
    // 结构体成员访问
    Member *Mem;
    // goto和标签语句
    char *Label;        // for match
    char *UniqueLabel;  // final target
    Node *GotoNext;     // for match
    // "break" 标签
    char *BrkLabel;
    // "continue" 标签
    char *ContLabel;
    // switch和case
    Node *CaseNext;
    Node *DefaultCase;

};


//
// 类型系统
//

// 类型种类
typedef enum {
    TY_INT,        // int整型
    TY_PTR,        // 指针
    TY_FUNC,       // 函数
    TY_ARRAY,      // 数组. very similar to ptr
    TY_CHAR,       // 字符类型
    TY_LONG,       // long长整型
    TY_SHORT,      // short短整型
    TY_VOID,       // void类型
    TY_STRUCT,     // 结构体
    TY_UNION,      // 联合体
    TY_BOOL,       // boolean
    TY_ENUM,       // 枚举类型
    TY_FLOAT,      // float类型
    TY_DOUBLE,     // double类型

} TypeKind;

struct Type {
    TypeKind Kind; // 种类
    int Size;      // 大小, sizeof返回的值
    int Align;     // 对齐
    bool IsUnsigned; // 是否为无符号的
    Type *Base;    // 基类, 指向的类型(only in effect for pointer)
    Token *Name;   // 类型对应名称，如：变量名、函数名
    Token *NamePos; // 名称位置
    // 函数类型
    Type *ReturnTy; // 函数返回的类型
    Type *Params;   // 存储形参的链表. head.
    Type *Next;     // 下一类型
    int ArrayLen;   // 数组长度, 元素总个数
    bool IsVariadic; // 是否为可变参数
    // 结构体
    Member *Mems;
    bool IsFlexible; // 是否为灵活的
    Token *Tok;      // 用于报错信息
    Type *FSReg1Ty;  // 浮点结构体的对应寄存器
    Type *FSReg2Ty;  // 浮点结构体的对应寄存器

};

// 结构体成员
struct Member {
    Member *Next; // 下一成员
    Type *Ty;     // 类型
    Token *Name;  // 名称
    int Offset;   // 偏移量
    int Idx;      // 索引值
    int Align;    // 对齐量

    // 位域
    bool IsBitfield; // 是否为位域
    int BitOffset;   // 位偏移量
    int BitWidth;    // 位宽度

};

// 全局变量可被 常量表达式 或者 指向其他全局变量的指针 初始化。
// 此结构体用于 指向其他全局变量的指针 的情况。
typedef struct Relocation Relocation;
struct Relocation {
    Relocation *Next; // 下一个
    int Offset;       // 偏移量
    char *Label;      // 标签名
    long Addend;      // 加数
};

// 声明一个全局变量，定义在type.c中。
extern Type *TyInt;
extern Type *TyChar;
extern Type *TyLong;
extern Type *TyShort;
extern Type *TyVoid;
extern Type *TyBool;

extern Type *TyUChar;
extern Type *TyUShort;
extern Type *TyUInt;
extern Type *TyULong;

extern Type *TyFloat;
extern Type *TyDouble;

//
// 主程序，驱动文件
//

/* ---------- main.c ---------- */
extern char *BaseFile;
void printTokens(Token *Tok);
bool fileExists(char *Path);
extern StringArray IncludePaths;
// other functions

/* ---------- tokenize.c ---------- */
// 词法分析
Token* tokenizeFile(char* Path);
void convertPPTokens(Token *tok);
bool equal(Token *Tok, char *Str);
bool equal2(Token *Tok, int n, char *kw[]);
Token *skip(Token *Tok, char *Str);
bool consume(Token **Rest, Token *Tok, char *Str);
char* tokenName(Token *Tok);
File **getInputFiles(void);
File *newFile(char *Name, int FileNo, char *Contents);
Token *tokenize(File *FP);


/* ---------- preprocess.c ---------- */
// 预处理器入口函数
Token *preprocess(Token *Tok);
// used in -D option
void define(char *Str);
void undefine(char *Name);

/* ---------- parse.c ---------- */
// 语法解析入口函数
Obj *parse(Token *Tok);


/* ---------- codegen.c ---------- */
// 代码生成入口函数
void codegen(Obj *Prog, FILE *Out);
int alignTo(int N, int Align);
bool OptW;

/* ---------- type.c ---------- */
// 判断是否为整型
bool isInteger(Type *TY);
// 判断是否为浮点类型
bool isFloNum(Type *Ty);
// 判断是否为数字
bool isNumeric(Type *Ty);
// 为节点内的所有节点添加类型
void addType(Node *Nd);
// 构建一个指针类型，并指向基类
Type *pointerTo(Type *Base);
// 函数类型
Type *funcType(Type *ReturnTy);
// 复制类型
Type *copyType(Type *Ty);
// 复制结构体的类型
Type *copyStructType(Type *Ty);
// 构造数组类型, 传入 数组基类, 元素个数
Type *arrayOf(Type *Base, int Len);

Type *enumType(void);
Type *structType(void);


/* ---------- string.c ---------- */
// 格式化后返回字符串
char *format(char *Fmt, ...);
void strArrayPush(StringArray *Arr, char *S);
// 判断字符串P是否以字符串Q结尾
bool endsWith(char *P, char *Q);

/* ---------- debug.c ---------- */

void errorTok(Token *Tok, char *Fmt, ...);
void errorAt(char *Loc, char *Fmt, ...);
//void error(char *fmt, ...);
void warnTok(Token *Tok, char *Fmt, ...);
//void Assert(int cond, char *fmt, ...);

/* ---------- parse-util.c ---------- */
int64_t eval(Node *Nd);
int64_t eval2(Node *Nd, char **Label);
double evalDouble(Node *Nd);
int64_t constExpr(Token **Rest, Token *Tok);
uint32_t simpleLog2(uint32_t v);

//
// macros
//

// stage2 cant recognize them now, so disable them temporarily

#define error(format, ...) \
    do{ \
        printf("\33[1;31m" "[%s:%d %s] " format "\33[0m" "\n", \
            __FILE__, __LINE__, __func__, ## __VA_ARGS__);\
        exit(1);\
    }\
    while(0);

//#define trace(format, ...) \
//    do{ \
//        printf("  # \33[1;33m" "[%s:%d %s - %s] " format "\33[0m" "\n", \
//            __FILE__, __LINE__, __func__, __TIME__, ## __VA_ARGS__);\
//    }\
//    while(0);

#define trace(format, ...) \
        printf("  # \33[1;33m" "[%s:%d %s - %s] " format "\33[0m" "\n", \
            __FILE__, __LINE__, __func__, __TIME__, ## __VA_ARGS__);

// 31=red, 32=green, 33=yellow, 34=blue, 35=purple, 36=cyan, 
#define color_text(s, n) "\x1b[1;" #n "m" s "\x1b[0m"

#define Assert(cond, fmt, ...) \
    do{\
        if(!(cond)) error(fmt, ## __VA_ARGS__);\
    }\
    while(0);

#define println(format, ...) fprintf(OutputFile, format "\n", ## __VA_ARGS__)

//#define MAX(x, y) ((x) < (y) ? (y) : (x))
//#define MIN(x, y) ((x) < (y) ? (x) : (y))
// this version looks cooler
#define MAX(x, y) \
    _Generic((x), \
            default: ((x) < (y) ? (y) : (x))\
    )

#define MIN(x, y) \
    _Generic((x), \
            default: ((x) > (y) ? (y) : (x))\
    )

#define stringSet(...) \
    (char*[]){__VA_ARGS__}

#endif