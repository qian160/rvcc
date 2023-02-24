#include"rvcc.h"

//    input = "1+2; 3-4;"
//    add a field 'next' to ast-tree node (下一语句, expr_stmt). see parse()
//    (TOK)
//    head -> EXPR_STMT   -> EXPR_STMT    -> (other stmts...)
//                |               |   (looks like straight, but in fact lhs. note: a node of )
//                |               |   (type 'EXPR_STMT' is also unary. see function exprStmt())
//               '+'             '-'
//               / \             / \
//              /   \           /   \
//            1      2         3     4

//    input = "{1; {2;}; 3;}" :
//
//                  compoundStmt                           LHS = NULL <- ND_BLOCK  -> RHS = NULL
//                      |                                                   |
//      ----------------+------------------                                 ↓ Body
//      |               |                 |                  LHS = 1 <- ND_EXPR_STMT -> RHS = NULL  . note: expr_stmt is unary.
//  ND_EXPR_STMT   compoundStmt      ND_EXPR_STMT      ->                   |
//      |               |                 |                                 ↓ Next         
//      1          ND_EXPR_STMT           3                             ND_BLOCK -> Body = ND_EXPR_STMT, LHS = 2
//                      |                                                   |
//                      2                                                   ↓ Next
//                                                           LHS = 3 <- ND_EXPR_STMT -> RHS = NULL

/*
//    这里也隐含了这样一层信息：类型为ND_BLOCK的节点并没有lhs与rhs
//    (每次构造的时候调用的也是newNode这个不完全初始化的函数)。 
//    真正有用的信息其实保留在Body里。最后codegen的时候遇到块语句，
//    不用管lhs与rhs，直接去他的body里面遍历生成语句就好
*/
//      
//                           declarator
//                               ↑
//                +--------------+-------------+
//                |                funcParams  |
//                |                +----------+|
//                |                |          ||
//        int     **      fn      (int a, int b)  { ... ... }         and the whole thing is a functionDefination
//         ↑               ↑      |            |  |         |
//      declspec         ident    +-----+------+  +----+----+
//                                      ↓              ↓
//                                 typeSuffix     compoundStmt

//      input = add(1 + 4, 2) + 5;
//
//                      +
//                     / \
//                   /     \
//                 /         \
//            ND_FUNCALL      2
//                |             1               // ugly...
//                ↓ Args       /
//           ND_EXPR_STMT -> +
//                |            \
//                ↓ Next        4
//           ND_EXPR_STMT -> 2

// ↓↑
//                                                                      //e.g.
// note: a single number can match almost all the cases.
// 越往下优先级越高

// program = functionDefination*                                        "int main(){ int a=3*6-7; int b=a+3;b; } | void f1(){6;} void f2(){;;;}
// functionDefinition = declspec declarator compoundStmt*           int ***foo(int a, int b, int fn(int a, int b)){return 1;}
// declspec = "int"
// declarator = "*"* ident typeSuffix                                   ***** a |  a | a()
// typeSuffix = ( funcParams  | "[" num "]")?                           null | () | (int a) | (int a, int f(int a)) | [4]   // these suffix tells that an ident is special
// funcParams =  "(" (param ("," param)*)? ")"                          (int a, int b) | (int fn(int x), int a, int b) | ()
//      param = declspec declarator                                     int * a | int a | int fn(int a, int b)      // function para also allowed, not not supproted yet...
// compoundStmt = "{" (stmt | declaration)* "}"                         { int a=4; return a; } | {6;} | {{;;;}}     // always be wrapped in pairs of "{}"

// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
//                                                                      int; | int a = 1; | int *a = &b; | int *******a; | int a = 3, b = 2; | int a, b = 2; 

// stmt = "return" expr ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | compoundStmt        // recursion
//        | exprStmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
// exprStmt = expr? ";"                                                 a = 3+5; | 6; | ;   note: must ends up with a ';'

// expr = assign
// assign = equality ("=" assign)?                                      a = (3*6-7) | a = b = 6 | 6  note: if it's the first case, then its lhs must be a lvalue
// equality = relational ("==" relational | "!=" relational)*           3*6==18 | 6
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*          (3*a+5) > (2*2+8) | 6
// add = mul ("+" mul | "-" mul)*                                       -4*5 + 4* (*a) | 6
// mul = unary ("*" unary | "/" unary)*                                 -(3+4) * a | -(5+6) | *a * b | 6
// unary = ("+" | "-" | "*" | "&") unary | primary                      -(3+5) | -4 | +4 | a | &a | *a | *****a | 6 
// primary = "(" expr ")" | num | ident args?                           (1+8*5 / a != 2) | a | fx(6) | 6
// args = "(" (expr ("," expr)*)? ")"
// funcall = ident "(" (expr ("," expr)*)? ")"                          foo(1, 2, 3+5, bar(6, 4))

static Function *function(Token **Rest, Token *Tok);
static Node *declaration(Token **Rest, Token *Tok);
static Type *declspec(Token **Rest, Token *Tok);
static Type *declarator(Token **Rest, Token *Tok, Type *Ty);
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty);
static Node *compoundStmt(Token **Rest, Token *Tok);
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
// generated by declaration()
Obj *Locals;

//
// 生成AST（抽象语法树），语法解析
//

// functionDefinition = declspec declarator compoundStmt*
static Function *function(Token **Rest, Token *Tok) {
    // declspec
    Type *Ty = declspec(&Tok, Tok);
    // declarator? ident "(" ")"
    Ty = declarator(&Tok, Tok, Ty);

    // 清空全局变量Locals
    Obj *tmp = Locals;
    while(tmp){
//        println(" # free %s", tmp->Name);
        Locals = Locals->Next;
        free(tmp);
        tmp = Locals;
    }

    // 从解析完成的Ty中读取ident
    Function *Fn = calloc(1, sizeof(Function));
    Fn->Name = getIdent(Ty->Name);
    // 函数参数. add the formal parms to its local variables
    createParamLVars(Ty->Params);
    Fn->Params = Locals;

    // 函数体存储语句的AST，Locals存储变量
    Fn->Body = compoundStmt(Rest, Tok);
    Fn->Locals = Locals;

    return Fn;
}

// declspec = "int"
// 声明的 基础类型
static Type *declspec(Token **Rest, Token *Tok) {
    *Rest = skip(Tok, "int");
    return TyInt;
}

/*declarator：
    声明符，其实是介于declspec（声明的基础类型）与一直到声明结束(目前是左花括号)
    这之间的所有东西。与前面的declspec搭配就完整地定义了一个函数的签名。也可以用来定义变量*/
// declarator = "*"* ident typeSuffix
// examples: ***var, fn(int x), a
// a further step on type parsing. also help to assign Ty->Name
static Type *declarator(Token **Rest, Token *Tok, Type *Ty) {
    // "*"*
    // 构建所有的（多重）指针
    while (consume(&Tok, Tok, "*"))
        Ty = pointerTo(Ty);
    // not an identifier, can't be declared
    if (Tok->Kind != TK_IDENT)
        error("%s: expect an identifier name", tokenName(Tok));

    // a small bug: if the function doesn't have a "()" suffix, it still compiles. such as int main{...}
    // temp solution: if Tok -> next = "{", then report an error. todo
    if(equal(Tok->Next, "{"))
        error("%s: expect a function", tokenName(Tok));

    // typeSuffix
    Ty = typeSuffix(Rest, Tok->Next, Ty);
    // ident
    // 变量名 或 函数名
    Ty->Name = Tok;
//    println(" #### name = %s, type = %d", tokenName(Tok), Ty->Kind);

    return Ty;
}

// funcParams =  "(" (param ("," param)*)? ")"
// param = declspec declarator
static Type *funcParams(Token **Rest, Token *Tok, Type *Ty) {
        // skip "(" at the begining of fn
        Tok = Tok -> Next;
        // 存储形参的链表
        Type Head = {};
        Type *Cur = &Head;
        while (!equal(Tok, ")")) {
            // funcParams = param ("," param)*
            // param = declspec declarator
            if (Cur != &Head)
                Tok = skip(Tok, ",");
            Type *BaseTy = declspec(&Tok, Tok);
            Type *DeclarTy = declarator(&Tok, Tok, BaseTy);
            // 将类型复制到形参链表一份
            Cur->Next = copyType(DeclarTy);
            Cur = Cur->Next;
        }

        // 封装一个函数节点
        Ty = funcType(Ty);
        // 传递形参
        Ty -> Params = Head.Next;
        // skip ")" at the end of function
        *Rest = Tok->Next;
        return Ty;
}

// typeSuffix = ( funcParams?  | "[" expr "]")?
// if function, construct its formal parms. otherwise do nothing
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty) {
    // ("(" funcParams? ")")?
    if (equal(Tok, "("))
        return funcParams(Rest, Tok, Ty);

    if (equal(Tok, "[")) {
        int Sz = getNumber(Tok->Next);
        *Rest = skip(Tok->Next->Next, "]");
        return arrayOf(Ty, Sz);
    }

    // not a function, nothing to do here
    *Rest = Tok;
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
        Node *RHS = expr(&Tok, Tok->Next);
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

// 解析复合语句
// compoundStmt = "{" (stmt | declaration)* "}"
static Node *compoundStmt(Token **Rest, Token *Tok) {
    // 这里使用了和词法分析类似的单向链表结构
    Tok = skip(Tok, "{");
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
//        | compoundStmt
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


    // compoundStmt
    if (equal(Tok, "{"))
//        return compoundStmt(Rest, Tok->Next);
        return compoundStmt(Rest, Tok);

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

// 解析函数调用. a helper function used by primary
// funcall = ident "(" (expr ("," expr)*)? ")"
// the arg `Tok` is an ident
static Node *funCall(Token **Rest, Token *Tok) {
    Token *Start = Tok;
    // get the 1st arg, or ")". jump skip indet and "("
    Tok = Tok->Next->Next;

    Node Head = {};
    Node *Cur = &Head;
    // expr ("," expr)*
    while (!equal(Tok, ")")) {
        if (Cur != &Head)
            Tok = skip(Tok, ",");
        // expr
        Cur->Next = expr(&Tok, Tok);
        Cur = Cur->Next;
    }

    *Rest = skip(Tok, ")");

    Node *Nd = newNode(ND_FUNCALL, Start);
    // ident
    Nd->FuncName = tokenName(Start);
    Nd->Args = Head.Next;
    return Nd;
}


// 解析括号、数字
// primary = "(" expr ")" | num | ident args?
// args = "(" (expr ("," expr)*)? ")"
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
        *Rest = Tok->Next;
        return Nd;
    }

    // ident args?
    // args = "(" (expr ("," expr)*)? ")"
    if (Tok->Kind == TK_IDENT) {
        // 函数调用
        if (equal(Tok->Next, "("))
            return funCall(Rest, Tok);

        // ident
        Obj *Var = findVar(Tok);
        if (!Var)
            error("%s: undefined variable", tokenName(Tok));
        *Rest = Tok->Next;
        return newVarNode(Var, Tok);
    }

    error("expected an expression");
    return NULL;
}

// 语法解析入口函数
// program = functionDefination*
// difference: construct multiple functions
Function *parse(Token *Tok) {
    Function Head = {};
    Function *Cur = &Head;

    while (Tok->Kind != TK_EOF)
        Cur = Cur->Next = function(&Tok, Tok);
    return Head.Next;
}
