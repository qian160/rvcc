//! 语法分析过程中的辅助函数
#include"rvcc.h"
#include"parse.h"

extern Obj *Locals;    // 局部变量
extern Obj *Globals;   // 全局变量

// 当前函数内的goto和标签列表
extern Node *Gotos;
extern Node *Labels;
// note: it is allowed to have an variable defined both in global
// and local on this occasion, we will use the local variable

// 所有的域的链表
extern Scope *Scp;

// 进入域
// insert from head，后来加入的会先被移除出去。 其实也就是越深的作用域存活时间越短
void enterScope(void) {
    Scope *S = calloc(1, sizeof(Scope));
    // 后来的在链表头部
    // 类似于栈的结构，栈顶对应最近的域
    S->Next = Scp;
    Scp = S;
}

// 结束当前域
void leaveScope(void) {
    Scp = Scp->Next;
}

// 将变量存入当前的域中
// returning the varscope for further process
VarScope *pushScope(char *Name) {
    VarScope *S = calloc(1, sizeof(VarScope));
    S->Name = Name;
    // 后来的在链表头部
    S->Next = Scp->Vars;
    Scp->Vars = S;
    return S;
}

void pushTagScope(Token *Tok, Type *Ty) {
    TagScope *S = calloc(1, sizeof(TagScope));
    S->Name = tokenName(Tok);
    S->Ty = Ty;
    S->Next = Scp->Tags;
    Scp->Tags = S;
}

// ---------- variables managements ----------

// 通过名称，查找一个变量
VarScope *findVar(Token *Tok) {
    // 此处越先匹配的域，越深层
    // inner scope has access to outer's
    for (Scope *S = Scp; S; S = S->Next)
        // 遍历域内的所有变量
        for (VarScope *S2 = S->Vars; S2; S2 = S2->Next)
            //if (equal(Tok, S2->Var->Name))
            if (equal(Tok, S2->Name))
                return S2;
    // trace("%s: NOT FOUND", _TKNAME_);
    return NULL;
}

// 新建变量. default 'islocal' = 0. helper fnction of the 2 below
Obj *newVar(char *Name, Type *Ty) {
    Obj *Var = calloc(1, sizeof(Obj));
    Var->Name = Name;
    Var->Ty = Ty;
    pushScope(Name)->Var = Var;
    return Var;
}

// 在链表中新增一个局部变量
Obj *newLVar(char *Name, Type *Ty) {
    Obj *Var = newVar(Name, Ty);
    Var->IsLocal = true;
    // 将变量插入头部
    Var->Next = Locals;
    Locals = Var;
    return Var;
}

// 在链表中新增一个全局变量
Obj *newGVar(char *Name, Type *Ty) {
    Obj *Var = newVar(Name, Ty);
    Var->Next = Globals;
    Globals = Var;
    return Var;
}

// 通过Token查找标签
Type *findTag(Token *Tok) {
    for (Scope *S = Scp; S; S = S->Next)
        for (TagScope *S2 = S->Tags; S2; S2 = S2->Next)
            if (equal(Tok, S2->Name))
                return S2->Ty;
    return NULL;
}

// 获取标识符
char *getIdent(Token *Tok) {
    if (Tok->Kind != TK_IDENT)
        error("%s: expected an identifier", tokenName(Tok));
    return tokenName(Tok);
}

// 将形参添加到Locals. name, type
void createParamLVars(Type *Param) {
    if (Param) {
        // 先将最底部的加入Locals中，之后的都逐个加入到顶部，保持顺序不变
        createParamLVars(Param->Next);
        // 添加到Locals中
        newLVar(getIdent(Param->Name), Param);
    }
}

// 新增唯一名称
char *newUniqueName(void) {
    static int Id = 0;
    return format(".L..%d", Id++);
}

// 新增匿名全局变量
Obj *newAnonGVar(Type *Ty) {
    return newGVar(newUniqueName(), Ty);
}

// 新增字符串字面量
Obj *newStringLiteral(char *Str, Type *Ty) {
    Obj *Var = newAnonGVar(Ty);
    Var->InitData = Str;
    return Var;
}

// 查找类型别名
Type *findTypedef(Token *Tok) {
    // 类型别名是个标识符
    if (Tok->Kind == TK_IDENT) {
        // 查找是否存在于域内
        VarScope *S = findVar(Tok);
        if (S)
            return S->Typedef;  // could be NULL if that var is not typedefined
    }
    return NULL;
}

// 判断是否为类型名
bool isTypename(Token *Tok) 
{
    static char *types[] = 
        {"typedef", "char", "int", "struct", "union", 
            "long", "short", "void", "_Bool", "enum",
            "static"
        };

    for(int i = 0; i < sizeof(types) / sizeof(*types); i++){
        if(equal(Tok, types[i]))
            return true;
    }
    return findTypedef(Tok);
}


//
// 创建节点
//

// 新建一个未完全初始化的节点. kind and token
Node *newNode(NodeKind Kind, Token *Tok) {
    Node *Nd = calloc(1, sizeof(Node));
    Nd->Kind = Kind;
    Nd->Tok = Tok;
    return Nd;
}

// 新建一个单叉树
Node *newUnary(NodeKind Kind, Node *Expr, Token *Tok) {
    Node *Nd = newNode(Kind, Tok);
    Nd->LHS = Expr;
    return Nd;
}

// 新建一个二叉树节点
Node *newBinary(NodeKind Kind, Node *LHS, Node *RHS, Token *Tok) {
    Node *Nd = newNode(Kind, Tok);
    Nd->LHS = LHS;
    Nd->RHS = RHS;
    return Nd;
}

// 新建一个数字节点
Node *newNum(int64_t Val, Token *Tok) {
    Node *Nd = newNode(ND_NUM, Tok);
    Nd->Val = Val;
    return Nd;
}

// 新建一个长整型节点
Node *newLong(int64_t Val, Token *Tok) {
    Node *Nd = newNode(ND_NUM, Tok);
    Nd->Val = Val;
    Nd->Ty = TyLong;
    return Nd;
}

// 解析各种加法.
// 其实是newBinary的一种特殊包装。
// 专门用来处理加法。 会根据左右节点的类型自动对此次加法做出适应
Node *newAdd(Node *LHS, Node *RHS, Token *Tok) {
    // 为左右部添加类型
    addType(LHS);
    addType(RHS);

    // num + num
    if (isInteger(LHS->Ty) && isInteger(RHS->Ty))
        return newBinary(ND_ADD, LHS, RHS, Tok);

    // 不能解析 ptr + ptr
    // has base type, meaning that it's a pointer
    if (LHS->Ty->Base && RHS->Ty->Base){
        error("can not add up two pointers.");
    }

    // 将 num + ptr 转换为 ptr + num
    if (!LHS->Ty->Base && RHS->Ty->Base) {
        Node *Tmp = LHS;
        LHS = RHS;
        RHS = Tmp;
    }

    // ptr + num
    // 指针加法，ptr+1，这里的1不是1个字节，而是1个元素的空间，所以需要 ×size 操作
    // 指针用long类型存储
    RHS = newBinary(ND_MUL, RHS, newLong(LHS->Ty->Base->Size, Tok), Tok);
    return newBinary(ND_ADD, LHS, RHS, Tok);
}

// 解析各种减法. 与newAdd类似
Node *newSub(Node *LHS, Node *RHS, Token *Tok) {
    // 为左右部添加类型
    addType(LHS);
    addType(RHS);

    // num - num
    if (isInteger(LHS->Ty) && isInteger(RHS->Ty))
    return newBinary(ND_SUB, LHS, RHS, Tok);

    // ptr - num
    // 指针用long类型存储
    if (LHS->Ty->Base && isInteger(RHS->Ty)) {
        RHS = newBinary(ND_MUL, RHS, newLong(LHS->Ty->Base->Size, Tok), Tok);
        addType(RHS);
        Node *Nd = newBinary(ND_SUB, LHS, RHS, Tok);
        // 节点类型为指针
        Nd->Ty = LHS->Ty;
        return Nd;
    }

    // ptr - ptr，返回两指针间有多少元素
    if (LHS->Ty->Base && RHS->Ty->Base) {
        Node *Nd = newBinary(ND_SUB, LHS, RHS, Tok);
        Nd->Ty = TyInt;
        return newBinary(ND_DIV, Nd, newNum(LHS->Ty->Base->Size, Tok), Tok);
    }

    error("%s: invalid operands", strndup(Tok->Loc, Tok->Len));
    return NULL;
}

// 新变量
Node *newVarNode(Obj* Var, Token *Tok) {
    Node *Nd = newNode(ND_VAR, Tok);
    Nd->Var = Var;
    return Nd;
}

extern Type *declarator(Token **Rest, Token *Tok, Type *Ty);
// 构造全局变量
Token *globalVariable(Token *Tok, Type *BaseTy) {
    bool First = true;
    // keep searching until we meet a ";"
    while (!consume(&Tok, Tok, ";")) {
        if (!First)
        Tok = skip(Tok, ",");
        First = false;

        Type *Ty = declarator(&Tok, Tok, BaseTy);
        // 全局变量初始化
        Obj *Var = newGVar(getIdent(Ty->Name), Ty);
        if (equal(Tok, "="))
            GVarInitializer(&Tok, Tok->Next, Var);
    }
    return Tok;
}

// 新转换
Node *newCast(Node *Expr, Type *Ty) {
    addType(Expr);
    Node *Nd = calloc(1, sizeof(Node));
    Nd->Kind = ND_CAST;
    Nd->Tok = Expr->Tok;
    Nd->LHS = Expr;
    Nd->Ty = copyType(Ty);
    return Nd;
}

//
// others
//

// when meeting gotos and labels during parsing, just collect
// them using a linked list, and match them in the end. 
// 匹配goto和标签
// 因为标签可能会出现在goto后面，所以要在解析完函数后再进行goto和标签的解析
void resolveGotoLabels(void) {
    // 遍历使goto对应上label
    for (Node *X = Gotos; X; X = X->GotoNext) {
        for (Node *Y = Labels; Y; Y = Y->GotoNext) {
            if (!strcmp(X->Label, Y->Label)) {
                X->UniqueLabel = Y->UniqueLabel;
                break;
            }
        }

        if (X->UniqueLabel == NULL)
            errorTok(X->Tok->Next, "use of undeclared label");
    }

    Gotos = NULL;
    Labels = NULL;
}

// 计算给定节点的常量表达式计算
// note: the value must can be known at compile time.
// so variables are not allowed here
int64_t eval(Node *Nd) {
    addType(Nd);

    switch (Nd->Kind) {
        case ND_ADD:
            return eval(Nd->LHS) + eval(Nd->RHS);
        case ND_SUB:
            return eval(Nd->LHS) - eval(Nd->RHS);
        case ND_MUL:
            return eval(Nd->LHS) * eval(Nd->RHS);
        case ND_DIV:
            return eval(Nd->LHS) / eval(Nd->RHS);
        case ND_NEG:
            return -eval(Nd->LHS);
        case ND_MOD:
            return eval(Nd->LHS) % eval(Nd->RHS);
        case ND_BITAND:
            return eval(Nd->LHS) & eval(Nd->RHS);
        case ND_BITOR:
            return eval(Nd->LHS) | eval(Nd->RHS);
        case ND_BITXOR:
            return eval(Nd->LHS) ^ eval(Nd->RHS);
        case ND_SHL:
            return eval(Nd->LHS) << eval(Nd->RHS);
        case ND_SHR:
            return eval(Nd->LHS) >> eval(Nd->RHS);
        case ND_EQ:
            return eval(Nd->LHS) == eval(Nd->RHS);
        case ND_NE:
            return eval(Nd->LHS) != eval(Nd->RHS);
        case ND_LT:
            return eval(Nd->LHS) < eval(Nd->RHS);
        case ND_LE:
            return eval(Nd->LHS) <= eval(Nd->RHS);
        case ND_COND:
            return eval(Nd->Cond) ? eval(Nd->Then) : eval(Nd->Els);
        case ND_COMMA:
            return eval(Nd->RHS);
        case ND_NOT:
            return !eval(Nd->LHS);
        case ND_BITNOT:
            return ~eval(Nd->LHS);
        case ND_LOGAND:
            return eval(Nd->LHS) && eval(Nd->RHS);
        case ND_LOGOR:
            return eval(Nd->LHS) || eval(Nd->RHS);
        case ND_CAST:
            if (isInteger(Nd->Ty)) {
                switch (Nd->Ty->Size) {
                    case 1:
                        return (uint8_t)eval(Nd->LHS);
                    case 2:
                        return (uint16_t)eval(Nd->LHS);
                    case 4:
                        return (uint32_t)eval(Nd->LHS);
                }
            }
            return eval(Nd->LHS);
        case ND_NUM:
            return Nd->Val;
        default:
            break;
    }

    errorTok(Nd->Tok, "not a compile-time constant");
    return -1;
}

extern Node *conditional(Token **Rest, Token *Tok);
// 解析常量表达式
int64_t constExpr(Token **Rest, Token *Tok) {
    // 进行常量表达式的构造
    Node *Nd = conditional(Rest, Tok);
    // 进行常量表达式的计算
    return eval(Nd);
}
