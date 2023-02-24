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
typedef struct Type Type;

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
    Type *Ty;   // 变量类型
};

// 函数.
struct Function {
    Node *Body;     // 函数体, made up by statements.
    Obj *Locals;    // 本地变量,
    int StackSize;  // 栈大小
    Function *Next; // 下一函数
    char *Name;     // 函数名
    Obj *Params;    // 形参
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
    ND_IF,          // "if"，条件判断
    ND_FOR,         // "for" 或 "while" 循环
    ND_ADDR,        // 取地址 &
    ND_DEREF,       // 解引用 *
    ND_FUNCALL      // 函数调用

} NodeKind;

// AST中二叉树节点
struct Node {
    // node*中都是存储了一串指令(保存至ast中)。
    // 可理解为指向另外一颗树的根节点
    NodeKind Kind;  // 节点种类
    Node *Next;     // 下一节点，指代下一语句
    Node *LHS;      // 左部，left-hand side. unary node also uses this side
    Node *RHS;      // 右部，right-hand side
    Node *Body;     // 块语句{...}的根节点。得到根节点后使用next进行遍历 
    Obj * Var;      // 存储ND_VAR的字符串
    Type *Ty;       // 节点中数据的类型
    int Val;        // 存储ND_NUM种类的值
    Token * Tok;    // 节点对应的终结符. debug
    // 函数
    char *FuncName; // 函数名
    Node *Args;     // 函数被调用时代入的实参，可看作是一串表达式链表。 形参则保存在Nd->Ty->Parms中
    // "if"语句
    Node *Cond;     // 条件内的表达式
    Node *Then;     // 符合条件后的语句
    Node *Els;      // 不符合条件后的语句
    // "for"语句. 循环体存储在Then里
    Node *Init;     // 初始化语句
    Node *Inc;      // 递增语句
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
} TypeKind;

struct Type {
    TypeKind Kind; // 种类
    int Size;      // 大小, sizeof返回的值
    Type *Base;    // 基类, 指向的类型(only in effect for pointer)
    Token *Name;   // 类型对应名称，如：变量名、函数名
    // 函数类型
    Type *ReturnTy; // 函数返回的类型
    Type *Params;   // 存储形参的链表. head.
    Type *Next;     // 下一类型
    int ArrayLen;   // 数组长度, 元素总个数
};

// 声明一个全局变量，定义在type.c中。
extern Type *TyInt;

// functions

/* ---------- tokenize.c ---------- */
// 词法分析
Token* tokenize(char* P);
bool equal(Token *Tok, char *Str);
Token *skip(Token *Tok, char *Str);
bool consume(Token **Rest, Token *Tok, char *Str);
char* tokenName(Token *Tok);
int getNumber(Token *Tok);

/* ---------- parse.c ---------- */
// 语法解析入口函数
Function *parse(Token *Tok);

/* ---------- codegen.c ---------- */
// 代码生成入口函数
void codegen(Function *Prog);

/* ---------- type.c ---------- */
// 判断是否为整型
bool isInteger(Type *TY);
// 为节点内的所有节点添加类型
void addType(Node *Nd);
// 构建一个指针类型，并指向基类
Type *pointerTo(Type *Base);
// 函数类型
Type *funcType(Type *ReturnTy);
// 复制类型
Type *copyType(Type *Ty);
// 构造数组类型, 传入 数组基类, 元素个数
Type *arrayOf(Type *Base, int Len);

/* ---------- parse-helper.c ---------- */
Node *newNode(NodeKind Kind, Token *Tok);
Node *newUnary(NodeKind Kind, Node *Expr, Token *Tok);
Node *newBinary(NodeKind Kind, Node *LHS, Node *RHS, Token *Tok);
Node *newNum(int Val, Token *Tok);
Node *newAdd(Node *LHS, Node *RHS, Token *Tok);
Node *newSub(Node *LHS, Node *RHS, Token *Tok);
Node *newVarNode(Obj* Var, Token *Tok);
Obj *findVar(Token *Tok);
Obj *newLVar(char *Name, Type *Ty);
char *getIdent(Token *Tok);
void createParamLVars(Type *Param);
int getNumber(Token *Tok);

//
// macros
//

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

// macro testing
// See https://stackoverflow.com/questions/26099745/test-if-preprocessor-symbol-is-defined-inside-macro
#define CHOOSE2nd(a, b, ...) b
#define MUX_WITH_COMMA(contain_comma, a, b) CHOOSE2nd(contain_comma a, b)
#define MUX_MACRO_PROPERTY(p, macro, a, b) MUX_WITH_COMMA(concat(p, macro), a, b)   
//define placeholders for some property. these will cause a "contain_comma"
/*argument passed to CHOOSE2nd : good match vs bad match
    good : X(just a placeholder), X, Y    bad: __P_DEF{/ONE/ZERO}_macroname X, Y(there is a space in the 1st arg). 
    seems like a grammer mistake? But no big deal since preprocess stage only expand the macro and doesn't check mistakes.
    And since we naver choose the 1st arg, the mistake will never be taken in the future stage*/

#define __P_DEF_0  X,   //only good match will generate this comma, which divide the args
#define __P_DEF_1  X,   //a comma is generated when a match happens. Or 
#define __P_ONE_1  X,
#define __P_ZERO_0 X,
// define some selection functions based on the properties of BOOLEAN macro(that is, the macro's value should be 1 or 0)
#define MUXDEF(macro, X, Y)  MUX_MACRO_PROPERTY(__P_DEF_, macro, X, Y)
#define MUXNDEF(macro, X, Y) MUX_MACRO_PROPERTY(__P_DEF_, macro, Y, X)  //here we change the order of X, Y
#define MUXONE(macro, X, Y)  MUX_MACRO_PROPERTY(__P_ONE_, macro, X, Y)
#define MUXZERO(macro, X, Y) MUX_MACRO_PROPERTY(__P_ZERO_,macro, X, Y)

// test if a boolean macro is defined
#define ISDEF(macro) MUXDEF(macro, 1, 0)
// test if a boolean macro is undefined
#define ISNDEF(macro) MUXNDEF(macro, 1, 0)
// test if a boolean macro is defined to 1
#define ISONE(macro) MUXONE(macro, 1, 0)
// test if a boolean macro is defined to 0
#define ISZERO(macro) MUXZERO(macro, 1, 0)
// test if a macro of ANY type is defined
// NOTE1: it ONLY works inside a function, since it calls `strcmp()`
// NOTE2: macros defined to themselves (#define A A) will get wrong results. Since its name = its value, the test will be past
#define isdef(macro) (strcmp("" #macro, "" str(macro)) != 0)

// simplification for conditional compilation
#define __IGNORE(...)
#define __KEEP(...) __VA_ARGS__
// keep the code if a boolean macro is defined
#define IFDEF(macro, ...) MUXDEF(macro, __KEEP, __IGNORE)(__VA_ARGS__)
// keep the code if a boolean macro is undefined
#define IFNDEF(macro, ...) MUXNDEF(macro, __KEEP, __IGNORE)(__VA_ARGS__)
// keep the code if a boolean macro is defined to 1
#define IFONE(macro, ...) MUXONE(macro, __KEEP, __IGNORE)(__VA_ARGS__)
// keep the code if a boolean macro is defined to 0
#define IFZERO(macro, ...) MUXZERO(macro, __KEEP, __IGNORE)(__VA_ARGS__)