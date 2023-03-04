//! 解析初始化列表
#include"rvcc.h"
#include"parse.h"

/*
//  int A[2][3] = { {1, 2, 3}, {4, 5, 6}};
//
//  initializer-list:
//
//                                    Init(TY_ARRAY,len=2)
//                     +---------------+----------------+
//                     ↓                                ↓
//                 children(TY_ARRAY,len=3)          children(TY_ARRAY,len=3)
//          +----------+----------+           +---------+---------+ 
//          ↓          ↓          ↓           ↓         ↓         ↓
//      children   children   children    children   children   children      ->  ALL TY_INT
//         ↓           ↓          ↓           ↓         ↓         ↓
//       EXPR=1      EXPR=2     EXPR=3      EXPR=4    EXPR=5    EXPR=6    

//  AST:
//                                                    ND_EXPR_STMT
//                                                         ↓
//                                                      ND_COMMA
//                                          +--------------+---------------+
//                                      ND_COMMA                        ND_ASSIGN (A[1][2]=6)
//                                    +------+------+                   +--------+--------+              
//                                    ↓             ↓                   ↓                 ↓
//                               ND_COMMA       ND_ASSIGN          ND_DEREF           Init->Expr, 6
//                            +------+------+    (A[1][1]=5)           |
//                            ↓             ↓                          ↓
//                       ND_COMMA       ND_ASSIGN                   ND_ADD
//                    +------+------+     (A[1][0]=4)         +--------+--------+    
//                    ↓             ↓                         ↓                 ↓
//                ND_COMMA       ND_ASSIGN                ND_DEREF          ND_MUL (newAdd)
//             +------+------+    (A[0][2]=3)                  |            +------+------+ 
//             ↓             ↓                                 ↓            ↓             ↓
//         ND_COMMA       ND_ASSIGN                         ND_ADD         2(IDX)      4(sizeof(int))
//      +------+------+    (A[0][1]=2)                +--------+--------+ 
//      ↓             ↓                               ↓                 ↓
//  ND_NULL_EXPR    ND_ASSIGN                      ND_VAR            ND_MUL
//                    (A[0][0]=1)                             +--------+--------+ 
//                                                            ↓                 ↓
//                                                        1(Idx)        12(sizeof(A[3]))

*/


extern Node *assign(Token **Rest, Token *Tok);

// 新建初始化器. 这里只创建了初始化器的框架结构
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
// 这里往框架结构上面添加了叶子节点(assign语句)
static void _initializer(Token **Rest, Token *Tok, Initializer *Init) {
    // "{" initializer ("," initializer)* "}"
    if (Init->Ty->Kind == TY_ARRAY) {
        Tok = skip(Tok, "{");

        // 遍历数组
        for (int I = 0; I < Init->Ty->ArrayLen && !equal(Tok, "}"); I++) {
            if (I > 0)
                Tok = skip(Tok, ",");
            _initializer(&Tok, Tok, Init->Children[I]);
        }
        *Rest = skip(Tok, "}");
        return;
    }
    // 为节点存储对应的表达式
    Init->Expr = assign(Rest, Tok);
}

// CREATE 初始化器
static Initializer *initializer(Token **Rest, Token *Tok, Type *Ty) {
    // 新建一个解析了类型的初始化器
    Initializer *Init = newInitializer(Ty);
    // 解析需要赋值到Init中
    _initializer(Rest, Tok, Init);
    return Init;
}

// 指派初始化表达式
// 这里其实是在找出一个地址，来充当assign节点的LHS.
// 一个指派器负责给数组（如果是的话）中的一个元素赋值。
static Node *initDesigExpr(InitDesig *Desig, Token *Tok) {
    // 返回Desig中的变量
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

// 创建局部变量的初始化, 利用init这个初始化器给desig完成赋值
// 之前创建好的初始化器还不能直接使用，需要转换到AST中的相应节点
static Node *createLVarInit(Initializer *Init, Type *Ty, InitDesig *Desig, Token *Tok) {
    if (Ty->Kind == TY_ARRAY) {
        // 预备空表达式的情况, 左下角的那个
        Node *Nd = newNode(ND_NULL_EXPR, Tok);
        for (int I = 0; I < Ty->ArrayLen; I++) {
            // 这里next指向了上一级Desig的信息
            InitDesig Desig2 = {Desig, I};  // next = Desig, index = I, var = NULL
            // 局部变量进行初始化
            Node *RHS = createLVarInit(Init->Children[I], Ty->Base, &Desig2, Tok);
            // 构造一个形如：NULL_EXPR，EXPR1，EXPR2…的二叉树
            Nd = newBinary(ND_COMMA, Nd, RHS, Tok);
        }
        return Nd;
    }

    // 如果需要作为右值的表达式为空，则设为空表达式
    if (!Init->Expr)
        return newNode(ND_NULL_EXPR, Tok);

    // 变量等可以直接赋值的左值
    Node *LHS = initDesigExpr(Desig, Tok);
    return newBinary(ND_ASSIGN, LHS, Init->Expr, Tok);
}

// 局部变量初始化器
Node *LVarInitializer(Token **Rest, Token *Tok, Obj *Var) {
    // 获取初始化器，将值与数据结构一一对应
    // 这里的Tok指向的是 "=" 后面的那个token
    Initializer *Init = initializer(Rest, Tok, Var->Ty);
    // 指派初始化
    InitDesig Desig = {NULL, 0, Var};   // next, idx, var

    // 我们首先为所有元素赋0，然后有指定值的再进行赋值
    Node *LHS = newNode(ND_MEMZERO, Tok);
    LHS->Var = Var;

    // 创建局部变量的初始化
    Node *RHS = createLVarInit(Init, Var->Ty, &Desig, Tok);
    // 左部为全部清零，右部为需要赋值的部分
    return newBinary(ND_COMMA, LHS, RHS, Tok);
}