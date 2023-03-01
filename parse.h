#include"rvcc.h"

//
// scope
//

// 局部和全局变量或是typedef的域.
// a varscope can only represent 1 variable... its name may be confusing
typedef struct VarScope VarScope;
struct VarScope {
    VarScope *Next; // 下一变量域, another block
    Obj *Var;       // 对应的变量/别名. the header of linked list
    Type *Typedef;  // 别名的类型info
};

// 结构体和联合体标签的域
typedef struct TagScope TagScope;
struct TagScope {
    TagScope *Next; // 下一标签域
    char *Name;     // struct's name
    Type *Ty;       // 域类型
};

// 表示一个块域
typedef struct Scope Scope;
struct Scope {
    Scope *Next;            // 指向上一级的域
    VarScope *Vars;         // 指向当前域内的变量
    TagScope *structTags;   // 指向当前域内的结构体标签
    TagScope *unionTags;    // 指向当前域内的union标签
};

// 变量属性
typedef struct {
    bool IsTypedef; // 是否为类型别名
} VarAttr;

//
// helper functions
//

// --------- scope ----------

void enterScope(void);
void leaveScope(void);
VarScope *pushScope(Obj *Var);
void pushTagScope(Token *Tok, Type *Ty, bool is_struct);

// ---------- variable management ----------

VarScope *findVar(Token *Tok);
Obj *newVar(char *Name, Type *Ty);
Obj *newLVar(char *Name, Type *Ty);
Obj *newGVar(char *Name, Type *Ty);
Type *findTag(Token *Tok, bool is_struct);
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