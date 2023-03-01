#include"rvcc.h"

//
// scope
//

// 局部变量，全局变量，typedef，enum常量的域(各种标识符)
typedef struct VarScope VarScope;
struct VarScope {
    VarScope *Next; // 下一变量域
    Obj *Var;       // 对应的变量
    char *Name;     // 变量域名称.
    Type *Typedef;  // 别名的类型info
    Type *EnumTy;   // 枚举的类型
    int EnumVal;    // 枚举的值
};

enum {
    STRUCT_TAG,
    UNION_TAG,
    ENUM_TAG
}tagType;

// 结构体和联合体标签的域
typedef struct TagScope TagScope;
struct TagScope {
    TagScope *Next; // 下一标签域
    char *Name;     // struct's name
    Type *Ty;       // 域类型
    //tagType type;
};

// 表示一个块域
// 里面存放了域中的各种标识符，包括变量名、函数名、别名, enum常量
typedef struct Scope Scope;
struct Scope {
    Scope *Next;            // 指向上一级的域
    VarScope *Vars;         // 指向当前域内的变量
    TagScope *Tags;         // 指向当前域内的结构体/union/enum标签
};

// 变量属性
typedef struct {
    bool IsTypedef; // 是否为类型别名
    bool IsStatic;  // 是否为文件域内
} VarAttr;

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
Token *globalVariable(Token *Tok, Type *BaseTy);

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