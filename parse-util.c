//! 语法分析过程中的辅助函数
#include"rvcc.h"
#include"parse.h"

extern Obj *Locals;    // 局部变量
extern Obj *Globals;   // 全局变量

// 当前函数内的goto和标签列表
extern Node *Gotos;
extern Node *Labels;

// 所有的域的链表
extern Scope *Scp;

static Obj *BuiltinAlloca;

char *typeNames[] = {
    "int",
    "ptr",
    "func",
    "array",
    "char",
    "long",
    "short",
    "void",
    "struct",
    "union",
    "bool",
    "enum",
    "float",
    "double",
    "VLA",
    "long double",
};

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
VarScope *pushScope(char *Name) {
    VarScope *S = calloc(1, sizeof(VarScope));
    hashmapPut(&Scp->Vars, Name, S);
    return S;
}

void pushTagScope(Token *Tok, Type *Ty) {
    hashmapPut(&Scp->Tags, tokenName(Tok), Ty);
}

// ---------- variables managements ----------

// 通过名称，查找一个变量
VarScope *findVar(Token *Tok) {
    // 此处越先匹配的域，越深层
    for (Scope *S = Scp; S; S = S->Next) {
        // 遍历域内的所有变量
        VarScope *S2 = hashmapGet(&S->Vars, tokenName(Tok));
        if (S2)
        return S2;
    }
    return NULL;
}

// 通过Token查找标签
Type *findTag(Token *Tok) {
    for (Scope *S = Scp; S; S = S->Next) {
        Type *Ty = hashmapGet(&S->Tags, tokenName(Tok));
        if (Ty)
        return Ty;
    }
    return NULL;
}

// 新建变量. default 'islocal' = 0. helper fnction of the 2 below
Obj *newVar(char *Name, Type *Ty) {
    Obj *Var = calloc(1, sizeof(Obj));
    Var->Name = Name;
    Var->Ty = Ty;
    Var->Align = Ty->Align;
    pushScope(Name)->Var = Var;
    return Var;
}

// 在链表中新增一个局部变量
Obj *newLVar(char *Name, Type *Ty) {
    Obj *Var = newVar(Name, Ty);
    Var->IsLocal = true;
    // 将变量插入头部
    Var->Next = Locals;
    Var->IsDefinition = true;
    Locals = Var;
    return Var;
}

// 在链表中新增一个全局变量
Obj *newGVar(char *Name, Type *Ty) {
    Obj *Var = newVar(Name, Ty);
    Var->Next = Globals;
    Var->IsStatic = true;
    Var->IsDefinition = true;
    Globals = Var;
    return Var;
}

// 获取标识符
char *getIdent(Token *Tok) {
    if (Tok->Kind != TK_IDENT)
        errorTok(Tok, "expected an identifier");
    return tokenName(Tok);
}

// 将形参添加到Locals. name, type
void createParamLVars(Type *Param) {
    if (Param) {
        // 先将最底部的加入Locals中，之后的都逐个加入到顶部，保持顺序不变
        createParamLVars(Param->Next);
        if (!Param->Name)
            errorTok(Param->NamePos, "parameter name omitted");
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

// 新增字符串字面量, name = .L..%d, Ty = char[]
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
    static HashMap Map;

    // 哈希表容量为0，说明还没初始化
    if (Map.Capacity == 0) {
        static char *Kw[] = {
            "void",       "_Bool",        "char",          "short",    "int",
            "long",       "struct",       "union",         "typedef",  "enum",
            "static",     "extern",       "_Alignas",      "signed",   "unsigned",
            "const",      "volatile",     "auto",          "register", "restrict",
            "__restrict", "__restrict__", "_Noreturn",     "float",    "double",
            "typeof",     "inline",       "_Thread_local", "__thread",
            "_Atomic",
        };
    
        // 遍历类型名列表插入哈希表
        for (int I = 0; I < sizeof(Kw) / sizeof(*Kw); I++)
        hashmapPut(&Map, Kw[I], (void *)1);
    }
    return hashmapGet(&Map, tokenName(Tok)) || findTypedef(Tok);
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

// 新建一个无符号长整型节点
Node *newULong(long Val, Token *Tok) {
    Node *node = newNode(ND_NUM, Tok);
    node->Val = Val;
    node->Ty = TyULong;
    return node;
}

// 解析各种加法.
// 其实是newBinary的一种特殊包装。
// 专门用来处理加法。 会根据左右节点的类型自动对此次加法做出适应
Node *newAdd(Node *LHS, Node *RHS, Token *Tok) {
    // 为左右部添加类型
    addType(LHS);
    addType(RHS);

    // num + num
    if (isNumeric(LHS->Ty) && isNumeric(RHS->Ty))
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

    // VLA + num
    // 指针加法，需要num×VLASize操作
    if (LHS->Ty->Base->Kind == TY_VLA) {
        RHS = newBinary(ND_MUL, RHS, newVarNode(LHS->Ty->Base->VLASize, Tok), Tok);
        return newBinary(ND_ADD, LHS, RHS, Tok);
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
    if (isNumeric(LHS->Ty) && isNumeric(RHS->Ty))
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
        Nd->Ty = TyLong;
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

// VLA指针
Node *newVLAPtr(Obj *Var, Token *Tok) {
    Node *Nd = newNode(ND_VLA_PTR, Tok);
    Nd->Var = Var;
    return Nd;
}

extern Type *declarator(Token **Rest, Token *Tok, Type *Ty);
// 构造全局变量
Token *globalVariable(Token *Tok, Type *BaseTy, VarAttr *Attr) {
    bool First = true;
    // keep searching until we meet a ";"
    while (!consume(&Tok, Tok, ";")) {
        if (!First)
        Tok = skip(Tok, ",");
        First = false;

        Type *Ty = declarator(&Tok, Tok, BaseTy);
        if (!Ty->Name)
            errorTok(Ty->NamePos, "variable name omitted");

        // 全局变量初始化
        Obj *Var = newGVar(getIdent(Ty->Name), Ty);
        Var->IsDefinition = !Attr->IsExtern;
        Var->IsStatic = Attr->IsStatic;
        Var->IsTLS = Attr->IsTLS;
        // 若有设置，则覆盖全局变量的对齐值
        if (Attr->Align)
            Var->Align = Attr->Align;

        if (equal(Tok, "="))
            GVarInitializer(&Tok, Tok->Next, Var);
        else if (!Attr->IsExtern && !Attr->IsTLS)
            // 没有初始化器的全局变量设为试探性的
            Var->IsTentative = true;

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

// 判断是否终结符匹配到了结尾
bool isEnd(Token *Tok) {
    // "}" | ",}"
    return equal(Tok, "}") || (equal(Tok, ",") && equal(Tok->Next, "}"));
}

// 消耗掉结尾的终结符
// "}" | ",}"
bool consumeEnd(Token **Rest, Token *Tok) {
    // "}"
    if (equal(Tok, "}")) {
        *Rest = Tok->Next;
        return true;
    }

    // ",}"
    if (equal(Tok, ",") && equal(Tok->Next, "}")) {
        *Rest = Tok->Next->Next;
        return true;
    }

    // 没有消耗到指定字符
    return false;
}

static int64_t evalRVal(Node *Nd, char ***Label);
int64_t eval2(Node *Nd, char ***Label);

// 解析浮点表达式
double evalDouble(Node *Nd) {
    addType(Nd);

    // 处理是整型的情况
    if (isInteger(Nd->Ty)) {
        if (Nd->Ty->IsUnsigned)
            return (unsigned long)eval(Nd);
        return eval(Nd);
    }

    switch (Nd->Kind) {
        case ND_ADD:
            return evalDouble(Nd->LHS) + evalDouble(Nd->RHS);
        case ND_SUB:
            return evalDouble(Nd->LHS) - evalDouble(Nd->RHS);
        case ND_MUL:
            return evalDouble(Nd->LHS) * evalDouble(Nd->RHS);
        case ND_DIV:
            return evalDouble(Nd->LHS) / evalDouble(Nd->RHS);
        case ND_NEG:
            return -evalDouble(Nd->LHS);
        case ND_COND:
            return evalDouble(Nd->Cond) ? evalDouble(Nd->Then) : evalDouble(Nd->Els);
        case ND_COMMA:
            return evalDouble(Nd->RHS);
        case ND_CAST:
            if (isFloNum(Nd->LHS->Ty))
            return evalDouble(Nd->LHS);
            return eval(Nd->LHS);
        case ND_NUM:
            return Nd->FVal;
        default:
            errorTok(Nd->Tok, "not a compile-time constant");
            return -1;
    }
}

// 计算给定节点的常量表达式计算(a constant known at compile-time)
// eval不使用label，所以功能较eval2弱一些，不能计算带有其他变量的常量表达式
// 其实常量表达式本身就是不能带变量的。不过当变量是全局的时候比较特殊，因为我们可以找到他的标签完成间接赋值
int64_t eval(Node *Nd) { return eval2(Nd, NULL); }

// 计算给定节点的常量表达式计算
// 常量表达式可以是数字或者是 ptr±n，ptr是指向全局变量的指针，n是偏移量。
int64_t eval2(Node *Nd, char ***Label) {
    addType(Nd);

    // 处理浮点数
    if (isFloNum(Nd->Ty))
        return evalDouble(Nd);

    switch (Nd->Kind) {
    case ND_ADD:
        return eval2(Nd->LHS, Label) + eval(Nd->RHS);
    case ND_SUB:
        return eval2(Nd->LHS, Label) - eval(Nd->RHS);
    case ND_MUL:
        return eval(Nd->LHS) * eval(Nd->RHS);
    case ND_DIV:
        if(Nd->Ty->IsUnsigned)
            return (uint64_t)eval(Nd->LHS) / eval(Nd->RHS);
        return eval(Nd->LHS) / eval(Nd->RHS);
    case ND_NEG:
        return -eval(Nd->LHS);
    case ND_MOD:
        if(Nd->Ty->IsUnsigned)
            return (uint64_t)eval(Nd->LHS) % eval(Nd->RHS);
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
        if(Nd->Ty->IsUnsigned)
            return (uint64_t)eval(Nd->LHS) >> eval(Nd->RHS);
        return eval(Nd->LHS) >> eval(Nd->RHS);
    case ND_EQ:
        return eval(Nd->LHS) == eval(Nd->RHS);
    case ND_NE:
        return eval(Nd->LHS) != eval(Nd->RHS);
    case ND_LT:
        if(Nd->Ty->IsUnsigned)
            return (uint64_t)eval(Nd->LHS) < eval(Nd->RHS);
        return eval(Nd->LHS) < eval(Nd->RHS);
    case ND_LE:
        if(Nd->Ty->IsUnsigned)
            return (uint64_t)eval(Nd->LHS) <= eval(Nd->RHS);
        return eval(Nd->LHS) <= eval(Nd->RHS);
    case ND_COND:
        return eval(Nd->Cond) ? eval2(Nd->Then, Label) : eval2(Nd->Els, Label);
    case ND_COMMA:
        return eval2(Nd->RHS, Label);
    case ND_NOT:
        return !eval(Nd->LHS);
    case ND_BITNOT:
        return ~eval(Nd->LHS);
    case ND_LOGAND:
        return eval(Nd->LHS) && eval(Nd->RHS);
    case ND_LOGOR:
        return eval(Nd->LHS) || eval(Nd->RHS);
    case ND_CAST: {
        int64_t Val = eval2(Nd->LHS, Label);
        if (isInteger(Nd->Ty)) {
            switch (Nd->Ty->Size) {
            case 1:
                return Nd->Ty->IsUnsigned ? (uint8_t)Val : (int8_t)Val;
            case 2:
                return Nd->Ty->IsUnsigned ? (uint16_t)Val : (int16_t)Val;
            case 4:
                return Nd->Ty->IsUnsigned ? (uint32_t)Val : (int32_t)Val;
            }
        }
        return Val;
    }
    case ND_ADDR:
        // find the label's address
        return evalRVal(Nd->LHS, Label);
    case ND_LABEL_VAL:
        // 将标签值也作为常量
        *Label = &Nd->UniqueLabel;
        return 0;
    case ND_MEMBER:
        // 未开辟Label的地址，则表明不是表达式常量
        if (!Label)
            errorTok(Nd->Tok, "not a compile-time constant");
        // 不能为数组
        if (Nd->Ty->Kind != TY_ARRAY)
            errorTok(Nd->Tok, "invalid initializer");
        // 返回左部的值（并解析Label），加上成员变量的偏移量
        return evalRVal(Nd->LHS, Label) + Nd->Mem->Offset;
    case ND_VAR:
        // 未开辟Label的地址，则表明不是表达式常量
        if (!Label)
            errorTok(Nd->Tok, "not a compile-time constant");
        // 不能为数组或者函数
        if (Nd->Var->Ty->Kind != TY_ARRAY && Nd->Var->Ty->Kind != TY_FUNC)
            errorTok(Nd->Tok, "invalid initializer");
        // 将标签值也作为常量
        *Label = &Nd->Var->Name;
        return 0;
    case ND_NUM:
        return Nd->Val;
    default:
        break;
    }

    errorTok(Nd->Tok, "not a compile-time constant");
    return -1;
}

// 计算重定位变量
static int64_t evalRVal(Node *Nd, char ***Label) {
    switch (Nd->Kind) {
        case ND_VAR:
            // 局部变量不能参与全局变量的初始化
            if (Nd->Var->IsLocal)
                errorTok(Nd->Tok, "not a compile-time constant");
            // 将标签值也作为常量
            *Label = &Nd->Var->Name;
            return 0;
        case ND_DEREF:
            // 直接进入到解引用的地址
            return eval2(Nd->LHS, Label);
        case ND_MEMBER:
            // 加上成员变量的偏移量
            return evalRVal(Nd->LHS, Label) + Nd->Mem->Offset;
        default:
            break;
    }

    errorTok(Nd->Tok, "invalid initializer");
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

// 判断是否为常量表达式
bool isConstExpr(Node *Nd) {
    addType(Nd);

    switch (Nd->Kind) {
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_BITAND:
        case ND_BITOR:
        case ND_BITXOR:
        case ND_SHL:
        case ND_SHR:
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_LOGAND:
        case ND_LOGOR:
            // 左部右部 都为常量表达式时 为真
            return isConstExpr(Nd->LHS) && isConstExpr(Nd->RHS);
        case ND_COND:
            // 条件不为常量表达式时 为假
            if (!isConstExpr(Nd->Cond))
                return false;
            // 条件为常量表达式时，判断相应分支语句是否为真
            return isConstExpr(eval(Nd->Cond) ? Nd->Then : Nd->Els);
        case ND_COMMA:
            // 判断逗号最右表达式是否为 常量表达式
            return isConstExpr(Nd->RHS);
        case ND_NEG:
        case ND_NOT:
        case ND_BITNOT:
        case ND_CAST:
            // 判断左部是否为常量表达式
            return isConstExpr(Nd->LHS);
        case ND_NUM:
            // 数字恒为常量表达式
            return true;
        default:
            // 其他情况默认为假
            return false;
    }
}

uint32_t simpleLog2(uint32_t v){ 
    Assert((v & (v-1)) == 0, "wrong value: %d", v);
    static const uint32_t b[] = {0xAAAAAAAA, 0xCCCCCCCC, 0xF0F0F0F0, 0xFF00FF00, 0xFFFF0000}; 
    register uint32_t r = (v & b[0]) != 0;  
    for (uint32_t i = 4; i > 0; i--) 
    // unroll for speed... 
            r |= ((v & b[i]) != 0) << i;
    return r;
}

// 生成代码计算VLA的大小
Node *computeVLASize(Type *Ty, Token *Tok) {
    // 空表达式
    Node *Nd = newNode(ND_NULL_EXPR, Tok);

    // 处理指针的基部
    if (Ty->Base)
        Nd = newBinary(ND_COMMA, Nd, computeVLASize(Ty->Base, Tok), Tok);

    // 如果都不是VLA，则返回空表达式
    if (Ty->Kind != TY_VLA)
        return Nd;

    // 基类的大小
    Node *BaseSz;
    if (Ty->Base->Kind == TY_VLA)
        // 指向的是VLA
        BaseSz = newVarNode(Ty->Base->VLASize, Tok);
    else
        // 本身是VLA
        BaseSz = newNum(Ty->Base->Size, Tok);

    Ty->VLASize = newLVar("", TyULong);
    // VLASize=VLALen*BaseSz，VLA大小=基类个数*基类大小
    Node *Expr = newBinary(ND_ASSIGN, newVarNode(Ty->VLASize, Tok),
                            newBinary(ND_MUL, Ty->VLALen, BaseSz, Tok), Tok);
    return newBinary(ND_COMMA, Nd, Expr, Tok);
}

// 声明内建函数
void declareBuiltinFunctions(void) {
    // 处理alloca函数
    Type *Ty = funcType(pointerTo(TyVoid));
    Ty->Params = copyType(TyInt);
    BuiltinAlloca = newGVar("alloca", Ty);
    BuiltinAlloca->IsDefinition = false;
}

// 根据相应Sz，新建一个Alloca函数
Node *newAlloca(Node *Sz) {
    Node *Nd = newUnary(ND_FUNCALL, newVarNode(BuiltinAlloca, Sz->Tok), Sz->Tok);
    Nd->FuncType = BuiltinAlloca->Ty;
    Nd->Ty = BuiltinAlloca->Ty->ReturnTy;
    Nd->Args = Sz;
    addType(Sz);
    return Nd;
}
