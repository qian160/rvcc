#include"rvcc.h"

// (Type){...}构造了一个复合字面量，相当于Type的匿名变量。
// TyInt这个全局变量的作用主要是方便了其他变量的初始化。直接设置为指向他就好。
// 而且似乎也节省了空间，创建一次就能被用很多次
// type, size, align, isUnsigned
Type *TyChar = &(Type){TY_CHAR, 1, 1};
Type *TyShort = &(Type){TY_SHORT, 2, 2};
Type *TyInt = &(Type){TY_INT, 4, 4};
Type *TyLong = &(Type){TY_LONG, 8, 8};

Type *TyVoid = &(Type){TY_VOID, 1, 1};
Type *TyBool = &(Type){TY_BOOL, 1, 1};

Type *TyUChar = &(Type){TY_CHAR, 1, 1, true};
Type *TyUShort = &(Type){TY_SHORT, 2, 2, true};
Type *TyUInt = &(Type){TY_INT, 4, 4, true};
Type *TyULong = &(Type){TY_LONG, 8, 8, true};

Type *TyFloat = &(Type){TY_FLOAT, 4, 4};
Type *TyDouble = &(Type){TY_DOUBLE, 8, 8};

static Type *newType(TypeKind Kind, int Size, int Align) {
    Type *Ty = calloc(1, sizeof(Type));
    Ty->Kind = Kind;
    Ty->Size = Size;
    Ty->Align = Align;
    return Ty;
}

// 判断Type是否为int类型
bool isInteger(Type *Ty){
    return Ty->Kind == TY_INT || Ty->Kind == TY_CHAR
        || Ty->Kind == TY_LONG || Ty->Kind == TY_SHORT
        || Ty->Kind == TY_BOOL || Ty->Kind == TY_ENUM; 
}

// 判断Type是否为浮点数
bool isFloNum(Type *Ty) {
    return Ty->Kind == TY_FLOAT || Ty->Kind == TY_DOUBLE;
}

// 判断是否为数字
bool isNumeric(Type *Ty) { 
    return isInteger(Ty) || isFloNum(Ty); 
}

bool isChar(Type *Ty){
    return Ty->Kind == TY_CHAR;
}

// 复制类型
Type *copyType(Type *Ty) {
    Type *Ret = calloc(1, sizeof(Type));
    *Ret = *Ty;
    return Ret;
}

// 复制结构体的类型
Type *copyStructType(Type *Ty) {
    // 复制结构体的类型
    Ty = copyType(Ty);

    // 复制结构体成员的类型
    Member Head = {};
    Member *Cur = &Head;
    // 遍历成员
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
        Member *M = calloc(1, sizeof(Member));
        *M = *Mem;
        Cur->Next = M;
        Cur = Cur->Next;
    }

    Ty->Mems = Head.Next;
    return Ty;
}


// 指针类型，并且指向基类
Type *pointerTo(Type *Base) {
    Type *Ty = newType(TY_PTR, 8, 8);
    Ty->Base = Base;
    Ty->IsUnsigned = true;
    return Ty;
}

// 函数类型，并赋返回类型
Type *funcType(Type *ReturnTy) {
    Type *Ty = calloc(1, sizeof(Type));
    Ty->Kind = TY_FUNC;
    Ty->ReturnTy = ReturnTy;
    return Ty;
}

// 构造数组类型, 传入 数组基类, 元素个数
// array of the base type
Type *arrayOf(Type *Base, int Len) {
    // 数组大小为所有元素大小之和
    Type *Ty = newType(TY_ARRAY, Base -> Size * Len, Base -> Align);
    Ty->Base = Base;
    Ty->ArrayLen = Len;
    return Ty;
}

Type *enumType(void){
    return newType(TY_ENUM, 4, 4);
}

Type *structType(void) {
    return newType(TY_STRUCT, 0, 1);
}



// 获取容纳左右部的类型
Type *getCommonType(Type *Ty1, Type *Ty2) {
    if (Ty1->Base)
        return pointerTo(Ty1->Base);

    // 为函数指针进行常规算术转换
    if (Ty1->Kind == TY_FUNC)
        return pointerTo(Ty1);
    if (Ty2->Kind == TY_FUNC)
        return pointerTo(Ty2);

    // 处理浮点类型
    // 优先使用double类型
    if (Ty1->Kind == TY_DOUBLE || Ty2->Kind == TY_DOUBLE)
        return TyDouble;
    // 其次使用float类型
    if (Ty1->Kind == TY_FLOAT || Ty2->Kind == TY_FLOAT)
        return TyFloat;

    // 小于四字节则为int
    if (Ty1->Size < 4)
        Ty1 = TyInt;
    if (Ty2->Size < 4)
        Ty2 = TyInt;

    // 选择二者中更大的类型
    if (Ty1->Size != Ty2->Size)
        return (Ty1->Size < Ty2->Size) ? Ty2 : Ty1;

    // 优先返回无符号类型（更大）
    if (Ty2->IsUnsigned)
        return Ty2;
    return Ty1;
}

extern Node *newCast(Node *Expr, Type *Ty);
// 进行常规的算术转换
void usualArithConv(Node **LHS, Node **RHS) {
    Type *Ty = getCommonType((*LHS)->Ty, (*RHS)->Ty);
    // 将左右部转换到兼容的类型
    *LHS = newCast(*LHS, Ty);
    *RHS = newCast(*RHS, Ty);
}

// 为节点内的所有节点添加类型
void addType(Node *Nd) {
    // 判断 节点是否为空 或者 节点类型已经有值，那么就直接返回
    if (!Nd || Nd->Ty)
        return;

    // 递归访问所有节点以增加类型
    addType(Nd->LHS);
    addType(Nd->RHS);
    addType(Nd->Cond);
    addType(Nd->Then);
    addType(Nd->Els);
    addType(Nd->Init);
    addType(Nd->Inc);

    // 访问链表内的所有节点以增加类型
    for (Node *N = Nd->Body; N; N = N->Next)
        addType(N);
    // 访问链表内的所有参数节点以增加类型
    for (Node *N = Nd->Args; N; N = N->Next)
        addType(N);

    switch (Nd->Kind) {
        // 判断是否Val强制转换为int后依然完整，完整则用int否则用long
        case ND_NUM:
            Nd->Ty = (Nd->Val == (int)Nd->Val) ? TyInt : TyLong;
            return;
        // 将节点类型设为 节点左部的类型
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_BITAND:
        case ND_BITXOR:
        case ND_BITOR:
            // 对左右部转换
            usualArithConv(&Nd->LHS, &Nd->RHS);
            Nd->Ty = Nd->LHS->Ty;
            return;
        case ND_NEG:{
            // 对左部转换
            Type *Ty = getCommonType(TyInt, Nd->LHS->Ty);
            Nd->LHS = newCast(Nd->LHS, Ty);
            Nd->Ty = Ty;
            return;
        }
        // 左部不能是数组节点
        case ND_ASSIGN:
            if (Nd->LHS->Ty->Kind == TY_ARRAY)
                errorTok(Nd->LHS->Tok, "not an lvalue");
            if (Nd->LHS->Ty->Kind != TY_STRUCT)
                // 对右部转换
                Nd->RHS = newCast(Nd->RHS, Nd->LHS->Ty);
            Nd->Ty = Nd->LHS->Ty;
            return;
        // 将节点类型设为 int
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            // 对左右部转换
            usualArithConv(&Nd->LHS, &Nd->RHS);
            Nd->Ty = TyInt;
            return;
        case ND_FUNCALL:
            Nd->Ty = Nd->FuncType->ReturnTy;
            return;
        // 将节点类型设为 变量的类型
        case ND_VAR:
            Nd->Ty = Nd->Var->Ty;
            return;
        // 将节点类型设为 指针，并指向左部的类型
        case ND_ADDR:{
            Type *Ty = Nd->LHS->Ty;
            if(Ty -> Kind == TY_ARRAY)
                Nd -> Ty = pointerTo(Ty -> Base);
            else
                Nd->Ty = pointerTo(Nd->LHS->Ty);
            return;
        }
        case ND_DEREF:
            // 如果不存在基类, 则无法解引用
            if (!Nd->LHS->Ty->Base)
                errorTok(Nd->Tok, "invalid pointer dereference");
            if (Nd->LHS->Ty->Base->Kind == TY_VOID)
                errorTok(Nd->Tok, "can not dereference a void pointer");

            Nd->Ty = Nd->LHS->Ty->Base;
            return;
        // 节点类型为 最后的表达式语句的类型
        case ND_STMT_EXPR:
            if (Nd->Body) {
                Node *Stmt = Nd->Body;
                while (Stmt->Next)
                    Stmt = Stmt->Next;
                if (Stmt->Kind == ND_EXPR_STMT) {
                    Nd->Ty = Stmt->LHS->Ty;
                    return;
                }
            }
            errorTok(Nd->Tok, "statement expression returning void is not supported");
        // 将节点类型设为 右部的类型
        case ND_COMMA:
            Nd->Ty = Nd->RHS->Ty;
            return;
        // 将节点类型设为 成员的类型
        case ND_MEMBER:
            Nd->Ty = Nd->Mem->Ty;
            return;
        case ND_NOT:
        case ND_LOGOR:
        case ND_LOGAND:
            Nd->Ty = TyInt;
            return;
        case ND_BITNOT:
        case ND_SHL:
        case ND_SHR:
            Nd->Ty = Nd->LHS->Ty;
            return;
        // 如果:左或右部为void则为void，否则为二者兼容的类型
        case ND_COND:
            if (Nd->Then->Ty->Kind == TY_VOID || Nd->Els->Ty->Kind == TY_VOID) {
                Nd->Ty = TyVoid;
            } else {
                usualArithConv(&Nd->Then, &Nd->Els);
                Nd->Ty = Nd->Then->Ty;
            }
            return;
        default:
            break;
    }
}
