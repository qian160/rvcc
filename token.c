#include<stdbool.h>
#include<string.h>
#include<stdlib.h>
#include<ctype.h>
#include"rvcc.h"

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

// 返回TK_NUM的值
int getNumber(Token *Tok) {
    Assert(Tok -> Kind == TK_NUM, "expect a number");
    return Tok->Val;
}

// 生成新的Token
Token *newToken(TokenKind Kind, char *Start, char *End) {
    // 分配1个Token的内存空间
    Token *Tok = calloc(1, sizeof(Token));
    Tok->Kind = Kind;
    Tok->Loc = Start;
    Tok->Len = End - Start;
    return Tok;
}

// 终结符解析. try to generate a list of tokens from P
// (head) -> '1' -> '+' -> '2' -> '-' -> '10'
Token *tokenize(char *P) {
    Token Head = {};
    Token *Cur = &Head;

    while (*P) {
        // 跳过所有空白符如：空格、回车
        if (isspace(*P)) {
            ++P;
            continue;
        }
        // 解析数字
        if (isdigit(*P)) {
            // 初始化，类似于C++的构造函数
            // 我们不使用Head来存储信息，仅用来表示链表入口，这样每次都是存储在Cur->Next
            // 否则下述操作将使第一个Token的地址不在Head中。
            Cur->Next = newToken(TK_NUM, P, P);
            // 指针前进
            Cur = Cur->Next;
            const char *OldPtr = P;
            Cur->Val = strtoul(P, &P, 10);
            Cur->Len = P - OldPtr;
            continue;
        }
        // 解析操作符
        if (ispunct(*P)) {
            // 操作符长度都为1
            Cur->Next = newToken(TK_PUNCT, P, P + 1);
            Cur = Cur->Next;
            ++P;
            continue;
        }

        // 处理无法识别的字符
        error("invalid token: %c", *P);
    }
    // 解析结束，增加一个EOF，表示终止符。
    Cur->Next = newToken(TK_EOF, P, P);
    // Head无内容，所以直接返回Next
    return Head.Next;
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

// 新建一个二叉树节点. VALUE?
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

// expr = mul ("+" mul | "-" mul)*              =>  (1 + 2) * 8 + (1 + 3) / 5 | 4
// mul = primary ("*" primary | "/" primary)*   =>  (1 + 2) * 8 | 3 * 8 | 4
// primary = "(" expr ")" | num                 =>  ((1 + 2) * 8 - 2) | 4
// 表达式：由一个或多个乘式进行相加减得到
// 乘式：由一个或多个基数进行相乘或相除得到
// 基数：括号(必须有)内的表达式或者一个简单的数字
// 越往下优先级越高

/*  rest表示本轮构建结束后新的token链表头的位置(构建过程中会改变所以类型是**)，
    tok表示本轮构建开始时的token链表头位置。expr函数的语义就是：
    “尝试从tok处开始构建一个表达式，把构建完成后的链表头保存到rest中，
    并返回构建完成的二叉树节点”。具体做法是：先从tok处开始尝试构建一个mul，
    并把构建完mul后的新链表头保存到tok中(直接覆盖，因为tok的任务已经完成了留着也没用)，
    然后根据返回的新链表头情况，继续尝试能否进一步构造。例如如果它是+，那就新建一个二叉树结点，
    他的左孩子是刚刚生成的那个mul，右孩子则是从新链表头(+)的下一个位置开始继续去寻找一个mul。
    这样根据定义一个expr就生成了(mul (± mul)* )。另外两个函数也差不多。 */

Node *expr(Token **Rest, Token *Tok);
Node *mul(Token **Rest, Token *Tok);
Node *primary(Token **Rest, Token *Tok);

// 解析加减.
// expr = mul ("+" mul | "-" mul)*
Node *expr(Token **Rest, Token *Tok) {
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

// 解析乘除. 
// mul = primary ("*" primary | "/" primary)*
Node *mul(Token **Rest, Token *Tok) {
    // primary
    Node *Nd = primary(&Tok, Tok);

    // ("*" primary | "/" primary)*
    while (true) {
        // "*" primary
        if (equal(Tok, "*")) {
            Nd = newBinary(ND_MUL, Nd, primary(&Tok, Tok->Next));
            continue;
        }

        // "/" primary
        if (equal(Tok, "/")) {
            Nd = newBinary(ND_DIV, Nd, primary(&Tok, Tok->Next));
            continue;
        }
        *Rest = Tok;
        return Nd;
    }
}

// 解析括号、数字
// primary = "(" expr ")" | num
Node *primary(Token **Rest, Token *Tok) {
    // "(" expr ")"
    if (equal(Tok, "(")) {
        Node *Nd = expr(&Tok, Tok->Next);
        *Rest = skip(Tok, ")");
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

// 生成表达式
void genExpr(Node *Nd) {
    // 加载数字到a0, leaf node
    if (Nd->Kind == ND_NUM) {
        println("  li a0, %d", Nd->Val);
        return;
    }

    // 递归到最右节点
    genExpr(Nd->RHS);
    // 将结果 a0 压入栈
    push();
    // 递归到左节点
    genExpr(Nd->LHS);
    // 将结果弹栈到a1
    pop("a1");

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
    default:
        break;
    }

    error("invalid expression");
}
