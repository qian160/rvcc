//! 语法分析阶段用到的各种东西，包括数据结构，和一些公共函数
#include"rvcc.h"

//
// scope
//

// 局部变量，全局变量，typedef，enum常量的域(各种标识符)
typedef struct {
    Obj *Var;       // 对应的变量
    Type *Typedef;  // 别名的类型info
    Type *EnumTy;   // 枚举的类型
    int EnumVal;    // 枚举的值
} VarScope;

// 表示一个块域
// 里面存放了域中的各种标识符，包括变量名、函数名、别名, enum常量
typedef struct Scope Scope;
struct Scope {
    Scope *Next;    // 指向上一级的域
    // C有两个域：变量（或类型别名）域，结构体（或联合体，枚举）标签域
    HashMap Vars;   // 指向当前域内的变量
    HashMap Tags;   // 指向当前域内的结构体标签
};

// 变量属性
typedef struct {
    bool IsTypedef; // 是否为类型别名
    bool IsStatic;  // 是否为文件域内
    bool IsExtern;  // 是否为外部变量
    bool IsInline;  // 是否为内联
    bool IsTLS;     // 是否为线程局部存储，Thread Local Storage
    int Align;      // 对齐量, 通过_Alignas手动设置
} VarAttr;

//
// initializer list
//

// 可变的初始化器。此处为树状结构。
// 因为初始化器可以是嵌套的，
// 类似于 int x[2][2] = {{1, 2}, {3, 4}} ，
typedef struct Initializer Initializer;
struct Initializer {
    Initializer *Next; // 下一个
    Type *Ty;          // 原始类型
    bool IsFlexible;   // 可调整的，表示需要重新构造
//    Token *Tok;        // 终结符

    // 如果不是聚合类型，并且有一个初始化器，Expr 有对应的初始化表达式。
    Node *Expr;

    // 如果是聚合类型（如数组或结构体），Children有子节点的初始化器
    // array of pointers
    Initializer **Children;

    // 联合体中只有一个成员能被初始化，此处用来标记是哪个成员被初始化
    Member *Mem;
};

// 指派初始化，用于局部变量的初始化器
typedef struct InitDesig InitDesig;
struct InitDesig {
    InitDesig *Next; // 下一个
    int Idx;         // 数组中的索引
    Obj *Var;        // 对应的变量
    Member *Mem;     // 成员变量
    // note: the field "Next" is used to represent the times of deref.
    // see InitDesigVar(), only the outmost desig has var, and each
    // recursion we add a ND_DEREF to AST if var is not found
};


//
// helper functions
//

// --------- scope ----------

void enterScope(void);
void leaveScope(void);
VarScope *pushScope(char *Var);
void pushTagScope(Token *Tok, Type *Ty);

// ---------- variable management ----------

VarScope *findVar(Token *Tok);
Obj *newVar(char *Name, Type *Ty);
Obj *newLVar(char *Name, Type *Ty);
Obj *newGVar(char *Name, Type *Ty);
Type *findTag(Token *Tok);
char *getIdent(Token *Tok);
void createParamLVars(Type *Param);
char *newUniqueName(void);
Obj *newAnonGVar(Type *Ty);
Obj *newStringLiteral(char *Str, Type *Ty);
Type *findTypedef(Token *Tok);
bool isTypename(Token *Tok);
Token *globalVariable(Token *Tok, Type *BaseTy, VarAttr *Attr);

// ---------- creating AST nodes ----------

Node *newNode(NodeKind Kind, Token *Tok);
Node *newUnary(NodeKind Kind, Node *Expr, Token *Tok);
Node *newBinary(NodeKind Kind, Node *LHS, Node *RHS, Token *Tok);
Node *newNum(int64_t Val, Token *Tok);
Node *newAdd(Node *LHS, Node *RHS, Token *Tok);
Node *newSub(Node *LHS, Node *RHS, Token *Tok);
Node *newVarNode(Obj* Var, Token *Tok);
Node *newCast(Node *Expr, Type *Ty);
Node *newLong(int64_t Val, Token *Tok);
Node *newULong(long Val, Token *Tok);
Node *newAlloca(Node *Sz);
Node *newVLAPtr(Obj *Var, Token *Tok);

// ---------- initializer-list ----------

Node *LVarInitializer(Token **Rest, Token *Tok, Obj *Var);
void GVarInitializer(Token **Rest, Token *Tok, Obj *Var);

// ---------- others ----------
void resolveGotoLabels(void);
bool isEnd(Token *Tok);
bool consumeEnd(Token **Rest, Token *Tok);
Node *computeVLASize(Type *Ty, Token *Tok);
bool isConstExpr(Node *Nd);
void declareBuiltinFunctions(void);