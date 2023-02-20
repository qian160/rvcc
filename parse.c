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

// 新变量
static Node *newVarNode(char Name) {
    Node *Nd = newNode(ND_VAR);
    Nd->Name = Name;
    return Nd;
}


// program = stmt*
// stmt = exprStmt
// exprStmt = expr ";"
// expr = assign
// assign = equality ("=" assign)?
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | num | ident
static Node *exprStmt(Token **Rest, Token *Tok);
static Node *expr(Token **Rest, Token *Tok);
static Node *assign(Token **Rest, Token *Tok);
static Node *equality(Token **Rest, Token *Tok);
static Node *relational(Token **Rest, Token *Tok);
static Node *add(Token **Rest, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *unary(Token **Rest, Token *Tok);
static Node *primary(Token **Rest, Token *Tok);

// 解析语句
// stmt = exprStmt
static Node *stmt(Token **Rest, Token *Tok) { 
    return exprStmt(Rest, Tok); 
}

// 解析表达式语句
// exprStmt = expr ";"
static Node *exprStmt(Token **Rest, Token *Tok) {
    Node *Nd = newUnary(ND_EXPR_STMT, expr(&Tok, Tok));
    *Rest = skip(Tok, ";");
    return Nd;
}

// 解析表达式
// expr = equality
static Node *expr(Token **Rest, Token *Tok) { 
    return assign(Rest, Tok);
}

// 解析赋值
// assign = equality ("=" assign)?
static Node *assign(Token **Rest, Token *Tok) {
    // equality
    Node *Nd = equality(&Tok, Tok);

    // 可能存在递归赋值，如a=b=1
    // ("=" assign)?
    if (equal(Tok, "="))
        Nd = newBinary(ND_ASSIGN, Nd, assign(&Tok, Tok->Next));
    *Rest = Tok;
    return Nd;
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

    // ident
    if (Tok->Kind == TK_IDENT) {
        Node *Nd = newVarNode(*Tok->Loc);
        *Rest = Tok->Next;
        return Nd;
    }


    error("expected an expression");
    return NULL;
}

// 语法解析入口函数
// program = stmt*
Node *parse(Token *Tok) {
    // 这里使用了和词法分析类似的单向链表结构
    Node Head = {};
    Node *Cur = &Head;
    // stmt*
    while (Tok->Kind != TK_EOF) {
        Cur->Next = stmt(&Tok, Tok);
        Cur = Cur->Next;
    }
    return Head.Next;
}
