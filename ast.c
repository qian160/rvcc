#include"rvcc.h"
#include<stdbool.h>
#include<stdlib.h>
#include<string.h>

// 判断Tok的值是否等于指定值，没有用char，是为了后续拓展
bool equal(Token *Tok, char *Str) {
    // 比较字符串LHS（左部），RHS（右部）的前N位，S2的长度应大于等于N.
    // 比较按照字典序，LHS<RHS回负值，LHS=RHS返回0，LHS>RHS返回正值
    // 同时确保，此处的Op位数=N
    return memcmp(Tok->Loc, Str, Tok->Len) == 0 && Str[Tok->Len] == '\0';
}

// 跳过指定的Str
Token *skip(Token *Tok, char *Str) {
    Assert(equal(Tok, Str), "expect '%s'", Str);
    return Tok->Next;
}

//
// 生成AST（抽象语法树），语法解析
//

// 新建一个leaf节点(lhs = rhs = 0, null)
static Node *newNode(NodeKind Kind) {
    Node *Nd = calloc(1, sizeof(Node));
    Nd->Kind = Kind;
    return Nd;
}

// 新建一个单叉树
static Node *newUnary(NodeKind Kind, Node *Expr) {
    Node *Nd = newNode(Kind);
    Nd->LHS = Expr; // why lhs? and what is expr?
    return Nd;
}

// 新建一个二叉树节点
static Node *newBinary(NodeKind Kind, Node *LHS, Node *RHS) {
    Node *Nd = newNode(Kind);
    Nd->LHS = LHS;
    Nd->RHS = RHS;
    return Nd;
}

// 新建一个数字节点
static Node *newNum(int Val) {
    Node *Nd = newNode(ND_NUM);
    Nd->Val = Val;
    return Nd;
}

// expr = equality
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | num
Node *expr(Token **Rest, Token *Tok);
static Node *equality(Token **Rest, Token *Tok);
static Node *relational(Token **Rest, Token *Tok);
static Node *add(Token **Rest, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *unary(Token **Rest, Token *Tok);
static Node *primary(Token **Rest, Token *Tok);

// 解析表达式
// expr = equality
Node *expr(Token **Rest, Token *Tok) { 
    return equality(Rest, Tok);
}

// 解析相等性
// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **Rest, Token *Tok) {
    // relational
    Node *Nd = relational(&Tok, Tok);

    // ("==" relational | "!=" relational)*
    while (true) {
        // "==" relational
        if (equal(Tok, "==")) {
            Nd = newBinary(ND_EQ, Nd, relational(&Tok, Tok->Next));
            continue;
        }

        // "!=" relational
        if (equal(Tok, "!=")) {
            Nd = newBinary(ND_NE, Nd, relational(&Tok, Tok->Next));
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析比较关系
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static Node *relational(Token **Rest, Token *Tok) {
    // add
    Node *Nd = add(&Tok, Tok);

    // ("<" add | "<=" add | ">" add | ">=" add)*
    while (true) {
        // "<" add
        if (equal(Tok, "<")) {
            Nd = newBinary(ND_LT, Nd, add(&Tok, Tok->Next));
            continue;
        }

        // "<=" add
        if (equal(Tok, "<=")) {
            Nd = newBinary(ND_LE, Nd, add(&Tok, Tok->Next));
            continue;
        }

        // ">" add
        // X>Y等价于Y<X
        if (equal(Tok, ">")) {
            Nd = newBinary(ND_LT, add(&Tok, Tok->Next), Nd);
            continue;
        }

        // ">=" add
        // X>=Y等价于Y<=X
        if (equal(Tok, ">=")) {
            Nd = newBinary(ND_LE, add(&Tok, Tok->Next), Nd);
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析加减
// add = mul ("+" mul | "-" mul)*
static Node *add(Token **Rest, Token *Tok) {
    // mul
    Node *Nd = mul(&Tok, Tok);

    // ("+" mul | "-" mul)*
    while (true) {
        // "+" mul
        if (equal(Tok, "+")) {
            Nd = newBinary(ND_ADD, Nd, mul(&Tok, Tok->Next));
            continue;
        }

        // "-" mul
        if (equal(Tok, "-")) {
            Nd = newBinary(ND_SUB, Nd, mul(&Tok, Tok->Next));
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析乘除
// mul = unary ("*" unary | "/" unary)*
static Node *mul(Token **Rest, Token *Tok) {
    // unary
    Node *Nd = unary(&Tok, Tok);

    // ("*" unary | "/" unary)*
    while (true) {
        // "*" unary
        if (equal(Tok, "*")) {
            Nd = newBinary(ND_MUL, Nd, unary(&Tok, Tok->Next));
            continue;
        }

        // "/" unary
        if (equal(Tok, "/")) {
            Nd = newBinary(ND_DIV, Nd, unary(&Tok, Tok->Next));
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析一元运算
// unary = ("+" | "-") unary | primary
static Node *unary(Token **Rest, Token *Tok) {
    // "+" unary
    if (equal(Tok, "+"))
        return unary(Rest, Tok->Next);

    // "-" unary
    if (equal(Tok, "-"))
        return newUnary(ND_NEG, unary(Rest, Tok->Next));

    // primary
    return primary(Rest, Tok);
}

// 解析括号、数字
// primary = "(" expr ")" | num
static Node *primary(Token **Rest, Token *Tok) {
    // "(" expr ")"
    if (equal(Tok, "(")) {
        Node *Nd = expr(&Tok, Tok->Next);
        *Rest = skip(Tok, ")");     // ?
        return Nd;
    }

    // num
    if (Tok->Kind == TK_NUM) {
        Node *Nd = newNum(Tok->Val);
        // this modifies `mul`'s Tok in fact
        *Rest = Tok->Next;
        return Nd;
    }

    error("expected an expression");
    return NULL;
}

//
// 语义分析与代码生成
//

// 记录栈深度
static int Depth;

// 压栈，将结果临时压入栈中备用
// sp为栈指针，栈反向向下增长，64位下，8个字节为一个单位，所以sp-8
// 当前栈指针的地址就是sp，将a0的值压入栈
// 不使用寄存器存储的原因是因为需要存储的值的数量是变化的。
static void push(void) {
    println("  addi sp, sp, -8");
    println("  sd a0, 0(sp)");
    Depth++;
}

// 弹栈，将sp指向的地址的值，弹出到a1
static void pop(char *Reg) {
    println("  ld %s, 0(sp)", Reg);
    println("  addi sp, sp, 8");
    Depth--;
}

// sementics: print the asm from an ast whose root node is `Nd`
// steps: for each node,
// 1. if it is a leaf node, then directly print the answer and return
// 2. otherwise:
//      get the value of its rhs sub-tree first, 
//      save that answer to the stack. 
//      then get the lhs sub-tree's value.
//      now we have both sub-tree's value, 
//      and how to deal with these two values depends on current root node
// 生成表达式
void genExpr(Node *Nd) {
    // 生成各个根节点
    switch (Nd->Kind) {
        // 加载数字到a0, leaf node
        case ND_NUM:
            println("  li a0, %d", Nd->Val);
            return;
        // 对寄存器取反
        case ND_NEG:
            genExpr(Nd->LHS);
            // neg a0, a0是sub a0, x0, a0的别名, 即a0=0-a0
            println("  neg a0, a0");
            return;
        default:
            break;
    }

    // 递归到最右节点
    genExpr(Nd->RHS);
    // 将结果 a0 压入栈
    // rhs sub-tree's answer
    push();
    // 递归到左节点
    genExpr(Nd->LHS);
    // 将结果弹栈到a1
    pop("a1");

    // a0: lhs value. a1: rhs value
    // 生成各个二叉树节点
    switch (Nd->Kind) {
        case ND_ADD: // + a0=a0+a1
            println("  add a0, a0, a1");
            return;
        case ND_SUB: // - a0=a0-a1
            println("  sub a0, a0, a1");
            return;
        case ND_MUL: // * a0=a0*a1
            println("  mul a0, a0, a1");
            return;
        case ND_DIV: // / a0=a0/a1
            println("  div a0, a0, a1");
            return;
        case ND_EQ:
            // if a0 == a1, then a0 ^ a1 should be 0
            println("  xor a0, a0, a1");
            println("  seqz a0, a0");
            return;
        case ND_NE: // a0 != a1
            // if a0 != a1, then a0 ^ a1 should not be 0
            println("  xor a0, a0, a1");
            println("  snez a0, a0");
            return;
        case ND_LE: // a0 <= a1
            // a0 <= a1 -> !(a0 > a1)
            // note: '!' here means 0 -> 1, 1-> 0. 
            // which is different from the 'neg' inst
            println("  slt a0, a1, a0");
            println("  xori a0, a0, 1");
            return;
        case ND_LT: // a0 < a1
            println("  slt a0, a0, a1");
            return;
        default:
            break;
    }

    error("invalid expression");
}