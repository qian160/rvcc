#include"rvcc.h"

/*
    input = "1+2; 3-4;"
    add a field 'next' to ast-tree node (下一语句, expr_stmt). see parse()
    (TOK)
    head -> EXPR_STMT   -> EXPR_STMT    -> (other stmts...)
                |               |   (looks like straight, but in fact lhs. note: a node of )
                |               |   (type 'EXPR_STMT' is also unary. see function exprStmt())
               '+'             '-'
               / \             / \
              /   \           /   \
            1      2         3     4

    input = "{1; {2;}; 3;}"

                compoundStmt
                    |
    ----------------+------------------
    |               |                 |
ND_EXPR_STMT   compoundStmt      ND_EXPR_STMT
    |               |                 |
    1          ND_EXPR_STMT           3
                    |
                    2


*/                                                                      //e.g.
// note: a single number can match almost all the cases.
// 越往下优先级越高

// program = "{" compoundStmt                                           "{" int a=3*6-7; int b=a+3;b; "}" | {6;}  | "{" return 1; "}"
// compoundStmt = (stmt | declaration)* "}"                             "{" int a=4; return a; "}" | {6;} | return 1; "}"
// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
//                                                                      int; | int a = 1; | int *a = &b; | int *******a; | int a = 3, b = 2; | int a, b = 2; 
// declspec = "int"
// declarator = "*"* ident                                              ***** a |  a
// stmt = "return" expr ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | "{" compoundStmt        // recursion
//        | exprStmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
// exprStmt = expr? ";"                                                 a = 3+5; | 6; | ;   note: must ends up with a ';'

// expr = assign
// assign = equality ("=" assign)?                                      a = (3*6-7) | 4  note: if it's the first case, then its lhs must be a lvalue
// equality = relational ("==" relational | "!=" relational)*           3*6==18 | 6
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*          (3*a+5) > (2*2+8) | 6
// add = mul ("+" mul | "-" mul)*                                       -4*5 + 4* (*a) | 6
// mul = unary ("*" unary | "/" unary)*                                 -(3*4) * a | -(5*6) | *a * b | 6
// unary = ("+" | "-" | "*" | "&") unary | primary                      -(3+5) | -4 | +4 | a | &a | *a | *****a | 6 
// primary = "(" expr ")" | num | ident args?                            (1+8*5 / a != 2) | a | fn() | 6
// args = "(" ")"
static Node *compoundStmt(Token **Rest, Token *Tok);
static Node *declaration(Token **Rest, Token *Tok);
static Node *stmt(Token **Rest, Token *Tok);
static Node *exprStmt(Token **Rest, Token *Tok);
static Node *expr(Token **Rest, Token *Tok);
static Node *assign(Token **Rest, Token *Tok);
static Node *equality(Token **Rest, Token *Tok);
static Node *relational(Token **Rest, Token *Tok);
static Node *add(Token **Rest, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *unary(Token **Rest, Token *Tok);
static Node *primary(Token **Rest, Token *Tok);

// 在解析时，全部的变量实例都被累加到这个列表里。
Obj *Locals;

// 通过名称，查找一个本地变量
static Obj *findVar(Token *Tok) {
    // 查找Locals变量中是否存在同名变量
    for (Obj *Var = Locals; Var; Var = Var->Next)
        // 判断变量名是否和终结符名长度一致，然后逐字比较。
        if (strlen(Var->Name) == Tok->Len &&
            !strncmp(Tok->Loc, Var->Name, Tok->Len))
        return Var;
    return NULL;
}

// 在链表中新增一个变量. insert from head
static Obj *newLVar(char *Name, Type *Ty) {
    Obj *Var = calloc(1, sizeof(Obj));
    Var->Name = Name;
    Var->Ty = Ty;
    // 将变量插入头部
    Var->Next = Locals;
    Locals = Var;
    return Var;
}

// 获取标识符
static char *getIdent(Token *Tok) {
    if (Tok->Kind != TK_IDENT)
        error("%s: expected an identifier", tokenName(Tok));
    return strndup(Tok->Loc, Tok->Len);
}

// declspec = "int"
// declarator specifier
static Type *declspec(Token **Rest, Token *Tok) {
    *Rest = skip(Tok, "int");
    return TyInt;
}

// declarator = "*"* ident
static Type *declarator(Token **Rest, Token *Tok, Type *Ty) {
    // "*"*
    // 构建所有的（多重）指针
    while (consume(&Tok, Tok, "*"))
        Ty = pointerTo(Ty);

    if (Tok->Kind != TK_IDENT)
        error("%s: expected a variable name", tokenName(Tok));

    // ident
    // 变量名
    Ty->Name = Tok;
    *Rest = Tok->Next;
    return Ty;
}

// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node *declaration(Token **Rest, Token *Tok) {
    // declspec
    // 声明的 基础类型
    Type *Basety = declspec(&Tok, Tok);

    Node Head = {};
    Node *Cur = &Head;
    // 对变量声明次数计数
    int I = 0;

    // (declarator ("=" expr)? ("," declarator ("=" expr)?)*)?
    while (!equal(Tok, ";")) {
        // 第1个变量不必匹配 ","
        if (I++ > 0)
            Tok = skip(Tok, ",");

        // declarator
        // 声明获取到变量类型，包括变量名
        Type *Ty = declarator(&Tok, Tok, Basety);
        Obj *Var = newLVar(getIdent(Ty->Name), Ty);

        // 如果不存在"="则为变量声明，不需要生成节点，已经存储在Locals中了
        if (!equal(Tok, "="))
            continue;

        // 解析“=”后面的Token
        Node *LHS = newVarNode(Var, Ty->Name);
        // 解析递归赋值语句
        Node *RHS = assign(&Tok, Tok->Next);
        Node *Node = newBinary(ND_ASSIGN, LHS, RHS, Tok);
        // 存放在表达式语句中
        Cur->Next = newUnary(ND_EXPR_STMT, Node, Tok);
        Cur = Cur->Next;
    }

    // 将所有表达式语句，存放在代码块中
    Node *Nd = newNode(ND_BLOCK, Tok);
    Nd->Body = Head.Next;
    *Rest = Tok->Next;
    return Nd;
}

//
// 生成AST（抽象语法树），语法解析
//

// 解析复合语句
// compoundStmt = (stmt | declaration)* "}"
static Node *compoundStmt(Token **Rest, Token *Tok) {
    // 这里使用了和词法分析类似的单向链表结构
    Node Head = {};
    Node *Cur = &Head;
    // (stmt | declaration)* "}"
    while (!equal(Tok, "}")) {
        if (equal(Tok, "int"))
            Cur->Next = declaration(&Tok, Tok);
        // stmt
        else
            Cur->Next = stmt(&Tok, Tok);
        Cur = Cur->Next;
        addType(Cur);
    }
    // Nd的Body存储了{}内解析的语句
    Node *Nd = newNode(ND_BLOCK, Tok);
    Nd->Body = Head.Next;
    *Rest = Tok->Next;
    return Nd;
}

// 解析语句
// stmt = "return" expr ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | "{" compoundStmt
//        | exprStmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
static Node *stmt(Token **Rest, Token *Tok) { 
    // "return" expr ";"
    if (equal(Tok, "return")) {
        Node *Nd = newNode(ND_RETURN, Tok);
        Nd -> LHS = expr(&Tok, Tok->Next);
        *Rest = skip(Tok, ";");
        return Nd;
    }

    // 解析if语句
    // "if" "(" expr ")" stmt ("else" stmt)?
    if (equal(Tok, "if")) {
        Node *Nd = newNode(ND_IF, Tok);
        // "(" expr ")"，条件内语句
        Tok = skip(Tok->Next, "(");
        Nd->Cond = expr(&Tok, Tok);
        Tok = skip(Tok, ")");
        // stmt，符合条件后的语句
        Nd->Then = stmt(&Tok, Tok);
        // ("else" stmt)?，不符合条件后的语句
        if (equal(Tok, "else"))
            Nd->Els = stmt(&Tok, Tok->Next);
        *Rest = Tok;
        return Nd;
    }

    // "for" "(" exprStmt expr? ";" expr? ")" stmt
    if (equal(Tok, "for")) {
        Node *Nd = newNode(ND_FOR, Tok);
        // "("
        Tok = skip(Tok->Next, "(");

        // exprStmt
        Nd->Init = exprStmt(&Tok, Tok);

        // expr?
        if (!equal(Tok, ";"))
            Nd->Cond = expr(&Tok, Tok);
        // ";"
        Tok = skip(Tok, ";");

        // expr?
        if (!equal(Tok, ")"))
            Nd->Inc = expr(&Tok, Tok);
        // ")"
        Tok = skip(Tok, ")");

        // stmt
        Nd->Then = stmt(Rest, Tok);
        return Nd;
    }

    // "while" "(" expr ")" stmt
    // while(cond){then...}
    if (equal(Tok, "while")) {
        Node *Nd = newNode(ND_FOR, Tok);
        // "("
        Tok = skip(Tok->Next, "(");
        // expr
        Nd->Cond = expr(&Tok, Tok);
        // ")"
        Tok = skip(Tok, ")");
        // stmt
        Nd->Then = stmt(Rest, Tok);
        return Nd;
    }


    // "{" compoundStmt
    if (equal(Tok, "{"))
        return compoundStmt(Rest, Tok->Next);

    // exprStmt
    return exprStmt(Rest, Tok);
}

// 解析表达式语句
// exprStmt = expr? ";"
static Node *exprStmt(Token **Rest, Token *Tok) {
    // ";". empty statment
    // note: empty statement is marked as a block statement.
    // in genStmt(), a block statment will print all its inner nodes
    // which should be nothing here
    if (equal(Tok, ";")) {
        *Rest = Tok->Next;
        return newNode(ND_BLOCK, Tok);
    }
    // expr ";"
    Node *Nd = newNode(ND_EXPR_STMT, Tok);
    Nd -> LHS = expr(&Tok, Tok);
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
        return Nd = newBinary(ND_ASSIGN, Nd, assign(Rest, Tok->Next), Tok);
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
        Token * start = Tok;
        // "==" relational
        if (equal(Tok, "==")) {
            Nd = newBinary(ND_EQ, Nd, relational(&Tok, Tok->Next), start);
            continue;
        }

        // "!=" relational
        if (equal(Tok, "!=")) {
            Nd = newBinary(ND_NE, Nd, relational(&Tok, Tok->Next), start);
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
        Token *start = Tok;
        // "<" add
        if (equal(Tok, "<")) {
            Nd = newBinary(ND_LT, Nd, add(&Tok, Tok->Next), start);
            continue;
        }

        // "<=" add
        if (equal(Tok, "<=")) {
            Nd = newBinary(ND_LE, Nd, add(&Tok, Tok->Next), start);
            continue;
        }

        // ">" add
        // X>Y等价于Y<X
        if (equal(Tok, ">")) {
            Nd = newBinary(ND_LT, add(&Tok, Tok->Next), Nd, start);
            continue;
        }

        // ">=" add
        // X>=Y等价于Y<=X
        if (equal(Tok, ">=")) {
            Nd = newBinary(ND_LE, add(&Tok, Tok->Next), Nd, start);
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
        Token * start = Tok;
        // "+" mul
        if (equal(Tok, "+")) {
            Nd = newAdd(Nd, mul(&Tok, Tok->Next), start);
            continue;
        }

        // "-" mul
        if (equal(Tok, "-")) {
            Nd = newSub(Nd, mul(&Tok, Tok->Next), start);
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
        Token * start = Tok;
        // "*" unary
        if (equal(Tok, "*")) {
            Nd = newBinary(ND_MUL, Nd, unary(&Tok, Tok->Next), start);
            continue;
        }

        // "/" unary
        if (equal(Tok, "/")) {
            Nd = newBinary(ND_DIV, Nd, unary(&Tok, Tok->Next), start);
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析一元运算
// unary = ("+" | "-" | "*" | "&") unary | primary
static Node *unary(Token **Rest, Token *Tok) {
    // "+" unary
    if (equal(Tok, "+"))
        return unary(Rest, Tok->Next);
    // "-" unary
    if (equal(Tok, "-"))
        return newUnary(ND_NEG, unary(Rest, Tok->Next), Tok);
    // "*" unary. pointer
    if (equal(Tok, "*")) {
        return newUnary(ND_DEREF, unary(Rest, Tok->Next), Tok);
    }
    // "*" unary. pointer
    if (equal(Tok, "&")) {
        return newUnary(ND_ADDR, unary(Rest, Tok->Next), Tok);
    }
    // primary
    return primary(Rest, Tok);
}

// 解析括号、数字
// primary = "(" expr ")" | num | ident args?
// args = "(" ")"
static Node *primary(Token **Rest, Token *Tok) {
    // "(" expr ")"
    if (equal(Tok, "(")) {
        Node *Nd = expr(&Tok, Tok->Next);
        *Rest = skip(Tok, ")");     // ?
        return Nd;
    }

    // num
    if (Tok->Kind == TK_NUM) {
        Node *Nd = newNum(Tok->Val, Tok);
        // this modifies `mul`'s Tok in fact
        *Rest = Tok->Next;
        return Nd;
    }

    // ident args?
    // args = "(" ")"
    if (Tok->Kind == TK_IDENT) {
        // 函数调用
        if (equal(Tok->Next, "(")) {
            Node *Nd = newNode(ND_FUNCALL, Tok);
            // ident
            Nd->FuncName = tokenName(Tok);
            *Rest = skip(Tok->Next->Next, ")");
            return Nd;
        }

        // ident
        // 查找变量
        Obj *Var = findVar(Tok);
        // 如果变量不存在，就在链表中新增一个变量
        if (!Var)
            error("%s: undefined variable", tokenName(Tok));
        *Rest = Tok->Next;
        return newVarNode(Var, Tok);
    }

    error("expected an expression");
    return NULL;
}

// 语法解析入口函数
// program = "{" compoundStmt
Function *parse(Token *Tok) {
    // "{"
    Tok = skip(Tok, "{");

    // 函数体存储语句的AST，Locals存储变量
    Function *Prog = calloc(1, sizeof(Function));
    Prog->Body = compoundStmt(&Tok, Tok);
    Prog->Locals = Locals;
    return Prog;
}
