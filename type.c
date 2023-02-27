#include "rvcc.h"

// (Type){...}构造了一个复合字面量，相当于Type的匿名变量。
// TyInt这个全局变量的作用主要是方便了其他变量的初始化。直接设置为指向他就好。
// 而且似乎也节省了空间，创建一次就能被用很多次
Type *TyInt = &(Type){TY_INT, 8};
Type *TyChar = &(Type){TY_CHAR, 1};

// 判断Type是否为int类型
bool isInteger(Type *Ty){
    return Ty->Kind == TY_INT || Ty -> Kind == TY_CHAR; 
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

// 指针类型，并且指向基类
Type *pointerTo(Type *Base) {
    Type *Ty = calloc(1, sizeof(Type));
    Ty->Kind = TY_PTR;
    Ty->Base = Base;
    Ty->Size = 8;
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
    Type *Ty = calloc(1, sizeof(Type));
    Ty->Kind = TY_ARRAY;
    // 数组大小为所有元素大小之和
    Ty->Size = Base->Size * Len;        // higher level array uses lower's as base
    Ty->Base = Base;
    Ty->ArrayLen = Len;
    return Ty;
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
        // 将节点类型设为 节点左部的类型
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_NEG:
            Nd->Ty = Nd->LHS->Ty;
            return;
        // 左部不能是数组节点
        case ND_ASSIGN:
            Assert(Nd->LHS->Ty->Kind != TY_ARRAY, "not a lvalue");
            Nd->Ty = Nd->LHS->Ty;
            return;
        // 将节点类型设为 int
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_NUM:
        case ND_FUNCALL:
            Nd->Ty = TyInt;
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
                error("%s: invalid pointer dereference", tokenName(Nd->Tok));
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
        default:
            break;
    }
}
