//! 解析初始化列表
#include"rvcc.h"
#include"parse.h"

/*
//  int A[2][3] = { {1, 2, 3}, {4, 5, 6}};
//
//  initializer-list:
//
//                  TY_ARRAY
//                      ↓
//                  children
//          +-----------+------------+
//          ↓                        ↓
//      TY_ARRAY                 TY_ARRAY
//   +------+------+          +------+------+ 
//   ↓      ↓      ↓          ↓      ↓      ↓
// TY_INT TY_INT TY_INT     TY_INT TY_INT TY_INT

//  AST:
//                                                    ND_EXPR_STMT
//                                                         ↓
//                                                      ND_COMMA
//                                          +--------------+---------------+
//                                      ND_COMMA                        ND_ASSIGN (A[1][2]=6)
//                                    +------+------+                   +--------+--------+              
//                                    ↓             ↓                   ↓                 ↓
//                               ND_COMMA       ND_ASSIGN          ND_DEREF           Init->Expr
//                            +------+------+    (A[1][1]=5)           |
//                            ↓             ↓                          ↓
//                       ND_COMMA       ND_ASSIGN                   ND_ADD
//                    +------+------+     (A[1][0]=4)         +--------+--------+    
//                    ↓             ↓                         ↓                 ↓
//                ND_COMMA       ND_ASSIGN                ND_DEREF          ND_MUL (newAdd)
//             +------+------+    (A[0][2]=3)                  |            +------+------+ 
//             ↓             ↓                                 ↓            ↓             ↓
//         ND_COMMA       ND_ASSIGN                         ND_ADD         2(IDX)      4(sizeof(int))
//      +------+------+    (A[0][1]=2)              +--------+--------+ 
//      ↓             ↓                             ↓                 ↓
//  ND_NULL_EXPR    ND_ASSIGN                    ND_VAR              ND_MUL
//                    (A[0][0]=1)                             +--------+--------+ 
//                                                            ↓                 ↓
//                                                        1(Idx)        12(sizeof(A[3]))

*/


extern Node *assign(Token **Rest, Token *Tok);

// 新建初始化器
static Initializer *newInitializer(Type *Ty) {
    Initializer *Init = calloc(1, sizeof(Initializer));
    // 存储原始类型
    Init->Ty = Ty;

    // 处理数组类型
    if (Ty->Kind == TY_ARRAY) {
        // 为数组的最外层的每个元素分配空间
        Init->Children = calloc(Ty->ArrayLen, sizeof(Initializer *));
        // 遍历解析数组最外层的每个元素
        for (int I = 0; I < Ty->ArrayLen; ++I)
            Init->Children[I] = newInitializer(Ty->Base);
    }

    return Init;
}

/*  a[2][3] = {{1,2,3}, {4,5,6}};    */
// initializer = "{" initializer ("," initializer)* "}" | assign
static void _initializer(Token **Rest, Token *Tok, Initializer *Init) {
    // "{" initializer ("," initializer)* "}"
    if (Init->Ty->Kind == TY_ARRAY) {
        Tok = skip(Tok, "{");

        // 遍历数组
        for (int I = 0; I < Init->Ty->ArrayLen; I++) {
            if (I > 0)
                Tok = skip(Tok, ",");
            _initializer(&Tok, Tok, Init->Children[I]);
        }
        *Rest = skip(Tok, "}");
        return;
    }

    // assign
    // 为节点存储对应的表达式
    Init->Expr = assign(Rest, Tok);
}

// 初始化器
static Initializer *initializer(Token **Rest, Token *Tok, Type *Ty) {
    // 新建一个解析了类型的初始化器
    Initializer *Init = newInitializer(Ty);
    // 解析需要赋值到Init中
    _initializer(Rest, Tok, Init);
    return Init;
}

// 指派初始化表达式
// 这里其实是在找出一个地址，或者是变量, 来充当assign节点的LHS.
// 一个指派器负责给数组中的一个元素赋值。（如果是的话）
static Node *initDesigExpr(InitDesig *Desig, Token *Tok) {
    // 返回Desig中的变量
    // only the first elem of array has var.
    // other elems will use deref to assign
    if (Desig->Var)
        return newVarNode(Desig->Var, Tok);

    // 需要赋值的变量名
    // 递归到次外层Desig，有此时最外层有Desig->Var
    // 然后逐层计算偏移量
    Node *LHS = initDesigExpr(Desig->Next, Tok);
    // 偏移量
    Node *RHS = newNum(Desig->Idx, Tok);
    // 返回偏移后的变量地址
    return newUnary(ND_DEREF, newAdd(LHS, RHS, Tok), Tok);
}

// 创建局部变量的初始化
static Node *createLVarInit(Initializer *Init, Type *Ty, InitDesig *Desig, Token *Tok) {
    if (Ty->Kind == TY_ARRAY) {
        // 预备空表达式的情况
        Node *Nd = newNode(ND_NULL_EXPR, Tok);
        for (int I = 0; I < Ty->ArrayLen; I++) {
            // 这里next指向了上一级Desig的信息，以及在其中的偏移量。
            InitDesig Desig2 = {Desig, I};  // index = I
            // 局部变量进行初始化
            Node *RHS = createLVarInit(Init->Children[I], Ty->Base, &Desig2, Tok);
            // 构造一个形如：NULL_EXPR，EXPR1，EXPR2…的二叉树
            Nd = newBinary(ND_COMMA, Nd, RHS, Tok);
        }
        return Nd;
    }

    // 变量等可以直接赋值的左值
    Node *LHS = initDesigExpr(Desig, Tok);
    // 初始化的右值
    Node *RHS = Init->Expr;
    return newBinary(ND_ASSIGN, LHS, RHS, Tok);
}

// 局部变量初始化器
Node *LVarInitializer(Token **Rest, Token *Tok, Obj *Var) {
    // 获取初始化器，将值与数据结构一一对应
    // 这里的Tok指向的是 "=" 后面的那个token
    // Var则是declarator返回后得到的
    Initializer *Init = initializer(Rest, Tok, Var->Ty);
    // 指派初始化
    InitDesig Desig = {NULL, 0, Var};
    // 创建局部变量的初始化
    return createLVarInit(Init, Var->Ty, &Desig, Tok);
}