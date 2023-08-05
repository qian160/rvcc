//! 语法分析的核心部分，负责创建AST
#include"rvcc.h"
#include"parse.h"

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
//    这里也隐含了这样一层信息：类型为ND_BLOCK的节点并没有lhs与rhs. (每次构造的时候调用的也是newNode这个不完全初始化的函数)。 
//    真正有用的信息其实保留在Body里。最后codegen的时候遇到块语句，不用管lhs与rhs，直接去他的body里面遍历生成语句就好
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


// declarator = "*"* ( "(" ident ")" | "(" declarator ")" | ident ) typeSuffix


//                                     declarator1(type1)
//                                         ↑
//              +--------------------------+-----------------------------------+
//              |                                                              |
//              |                                                              |
//        int   **    (     *(  * (     foo     )   [6]          )     [6]     )   [6][6][6]
//      declspec            |   |      ident    | typeSuffix3    | typeSuffix2     typeSuffix1
//                          |   |               |                |
//                          |   +-------+-------+                |
//                          |           ↓                        |
//                          |    declarator3(type3)              |
//                          +--------------+---------------------+
//                                         ↓
//                                  declarator2(type2)


// final goal: to recognize declarator1, the biggest one
// note that inside our declarator1, there are many other sub-declarators
// since the function declarator() returns a type, we can call it recursively
// and build new type upon old ones(base)

// final type  = declspec + declarator1 + typeSuffix1
// declarator1 = declarator2 + typeSuffix2
// declarator2 = declarator3 + typeSuffix3

//      input = add(1 + 4, 2) + 5;
//
//                      +
//                     / \
//                   /     \
//                 /         \
//            ND_FUNCALL      5
//                |             1               // ugly...
//                ↓ Args       /
//           ND_EXPR_STMT -> +
//                |            \
//                ↓ Next        4
//           ND_EXPR_STMT -> 2

/*
//         input = 
//         switch (val) {
//           case 1:
//             ...
//           case 2:
//             ...
//           default:
//             ...
//         }
//
//                          Then
//          CurrentSwitch    ->     ND_BLOCK
//           /   |  \                   |
//         val   | BrkLabel             ↓ Body      CaseNext
//               ↓                    ND_CASE       ->      ND_CASE ->  NULL
//          DefaultCase             /    |    \            /   |   \
//            /      \        LHS=STMT val=2  label      LHS  val=1  label
//      LHS=STMT     label
//
//
//  function pointer:
//
//                      declarator (also the return type)
//                          ↑
//           +----------------------------+
//           |                            |
//      int  (*fnptr (int (*fn)(int, int)))  (int, int) {return fn;}
//       ↓                                   |        |
//    declspec(base type)                    +----+---+
//                                                ↓
//                                           funcParams (arg type)

//
*/

// ↓↑

// recursive descent
// 越往下优先级越高

// program = (functionDefination | global-variables)*
// functionDefinition = declspec declarator compoundStmt*
// declspec = ("int" | "char" | "long" | "short" | "void" | "_Bool"
//              | "typedef" | "static" | "extern" | "inline"
//              | "_Thread_local" | "__thread"
//              | "signed" | "unsigned"
//              | structDecl | unionDecl | typedefName
//              | enumSpecifier | typeofSpecifier
//              | "const" | "volatile" | "auto" | "register" | "restrict"
//              | "__restrict" | "__restrict__" | "_Noreturn")+


// enumSpecifier = ident? "{" enumList? "}"
//                 | ident ("{" enumList? "}")?
// enumList = ident ("=" constExpr)? ("," ident ("=" constExpr)?)* ","?

// declarator = pointers ("(" ident ")" | "(" declarator ")" | ident) typeSuffix
// pointers = ("*" ("const" | "volatile" | "restrict")*)*
// typeSuffix = ( funcParams  | "[" arrayDimensions? "]"  typeSuffix)? | ε
// arrayDimensions = ("static" | "restrict")* constExpr?  typeSuffix

// funcParams =  "(" "void" | (param ("," param)* "," "..." ? )? ")"
//      param = declspec declarator

// structDecl = structUnionDecl
// unionDecl = structUnionDecl
// structUnionDecl = attribute? ident? ("{" structMembers "}")?
// attribute = ("__attribute__" "(" "(" ("packed")
//                                    | ("aligned" "(" N ")") ")" ")")*

// structMembers = (declspec declarator (","  declarator)* ";")*

// compoundStmt = "{" ( typedef | stmt | declaration)* "}"

// declaration = declspec (declarator ("=" initializer)?
//                         ("," declarator ("=" initializer)?)*)? ";"
// initializer = "{" initializer ("," initializer)* "}" | assign

// stmt = "return" expr? ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | compoundStmt
//        | exprStmt
//        | asmStmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
//        | "do" stmt "while" "(" expr ")" ";"
//        | "goto" (ident | "*" expr) ";"
//        | ident ":" stmt
//        | "break" ";" | "continue" ";"
//        | "switch" "(" expr ")" stmt
//        | "case" constExpr ("..." constExpr)? ":" stmt
//        | "default" ":" stmt

// asmStmt = "asm" ("volatile" | "inline")* "(" stringLiteral ")"

// exprStmt = expr? ";"
// expr = assign ("," expr)?
// assign = conditional (assignOp assign)?
// conditional = logOr ("?" expr? ":" conditional)?
//      logOr = logAnd ("||" logAnd)*
//      logAnd = bitOr ("&&" bitOr)*
//      bitOr = bitXor ("|" bitXor)*
//      bitXor = bitAnd ("^" bitAnd)*
//      bitAnd = equality ("&" equality)*
//      assignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>="
// equality = relational ("==" relational | "!=" relational)*
// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
// shift = add ("<<" add | ">>" add)*
// add = mul ("+" mul | "-" mul)*
// mul = cast ("*" cast | "/" cast | "%" cast)*
// cast = "(" typeName ")" cast | unary
// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | postfix 
//       | ("++" | "--") unary
//       | "&&" ident

// postfix = "(" typeName ")" "{" initializerList "}"
//         = ident "(" funcArgs ")" postfixTail*
//         | primary postfixTail*
//
// postfixTail = "[" expr "]"
//             | "(" funcArgs ")"
//             | "." ident
//             | "->" ident
//             | "++"
//             | "--"

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" unary
//         | ident funcArgs?
//         | "__builtin_types_compatible_p" "(" typeName, typeName, ")"
//         | "_Generic" genericSelection
//         | str
//         | num
//         | "sizeof" "(typeName)"
//         | "_Alignof" unary
//         | "_Alignof" "(" typeName ")"
//
// genericSelection = "(" assign "," genericAssoc ("," genericAssoc)* ")"
// genericAssoc = typeName ":" assign
//              | "default" ":" assign

// typeName = declspec abstractDeclarator
// abstractDeclarator = "*"* ("(" abstractDeclarator ")")? typeSuffix

// FuncArgs = "(" (expr ("," expr)*)? ")"
// funcall = ident "(" (assign ("," assign)*)? ")"

static Token *function(Token *Tok, Type *BaseTy, VarAttr *Attr);
static Node *declaration(Token **Rest, Token *Tok, Type *BaseTy, VarAttr *Attr);
static Type *declspec(Token **Rest, Token *Tok, VarAttr *Attr);
static Type *typename(Token **Rest, Token *Tok);
static Type *enumSpecifier(Token **Rest, Token *Tok);
static Type *typeofSpecifier(Token **Rest, Token *Tok);
static Type *structDecl(Token **Rest, Token *Tok);
static Type *unionDecl(Token **Rest, Token *Tok);
/*  */ Type *declarator(Token **Rest, Token *Tok, Type *Ty);   // used in parse-util...
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty);
static Node *compoundStmt(Token **Rest, Token *Tok);
static Node *stmt(Token **Rest, Token *Tok);
static Node *exprStmt(Token **Rest, Token *Tok);
static Node *expr(Token **Rest, Token *Tok);
/*  */ Node *assign(Token **Rest, Token *Tok);
/*  */ Node *conditional(Token **Rest, Token *Tok);
static Node *logOr(Token **Rest, Token *Tok);
static Node *logAnd(Token **Rest, Token *Tok);
static Node *bitOr(Token **Rest, Token *Tok);
static Node *bitXor(Token **Rest, Token *Tok);
static Node *bitAnd(Token **Rest, Token *Tok);
static Node *equality(Token **Rest, Token *Tok);
static Node *relational(Token **Rest, Token *Tok);
static Node *shift(Token **Rest, Token *Tok);
static Node *add(Token **Rest, Token *Tok);
/*  */ Node *newAdd(Node *LHS, Node *RHS, Token *Tok);
/*  */ Node *newSub(Node *LHS, Node *RHS, Token *Tok);
static Node *cast(Token **Rest, Token *Tok);
static Type *typename(Token **Rest, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *unary(Token **Rest, Token *Tok);
static Node *postfix(Token **Rest, Token *Tok);
static Node *funCall(Token **Rest, Token *Tok, Node *Nd);
static Node *primary(Token **Rest, Token *Tok);

static Token *parseTypedef(Token *Tok, Type *BaseTy);

static bool isFunction(Token *Tok);

// 在解析时，全部的变量实例都被累加到这个列表里。
Obj *Locals;    // 局部变量
Obj *Globals;   // 全局变量
// note: it is allowed to have an variable defined both in global
// and local on this occasion, we will use the local variable

// 所有的域的链表
Scope *Scp = &(Scope){};

// 指向当前正在解析的函数
static Obj *CurrentFn;

// 当前函数内的goto和标签列表
Node *Gotos;
Node *Labels;

// 当前goto跳转的目标(break is implemented by goto)
static char *BrkLabel;
// 当前continue跳转的目标
static char *ContLabel;
// 如果我们正在解析switch语句，则指向表示switch的节点。 否则为空。
static Node *CurrentSwitch;
// 记录这些标签名、节点是为了在后续递归解析相关语句的时候能拿来给节点赋值。同时也可以防止stray现象

extern bool OptW;
extern char *typeNames[];
// 向下对齐值
// N % Align != 0 , 即 N 未对齐时,  AlignDown(N) = AlignTo(N) - Align
// N % Align == 0 , 即 N 已对齐时， AlignDown(N) = AlignTo(N)
static int alignDown(int N, int Align) { return alignTo(N - Align + 1, Align); }


//
// 生成AST（抽象语法树），语法解析
//


// 解析类型别名. ends with ";"
static Token *parseTypedef(Token *Tok, Type *BaseTy) {
    bool First = true;
    while (!consume(&Tok, Tok, ";")) {
        if (!First)
            Tok = skip(Tok, ",");
        First = false;
        Type *Ty = declarator(&Tok, Tok, BaseTy);
        if (!Ty->Name)
            errorTok(Ty->NamePos, "typedef name omitted");
        // 类型别名的变量名存入变量域中，并设置类型
        pushScope(getIdent(Ty->Name))->Typedef = Ty;
    }
    return Tok;
}

// 查找是否存在函数
static Obj *findFunc(char *Name) {
    Scope *Sc = Scp;
    // 递归到最内层域
    while (Sc->Next)
        Sc = Sc->Next;

    // 遍历查找函数是否存在
    VarScope *Sc2 = hashmapGet(&Sc->Vars, Name);
    if (Sc2 && Sc2->Var && Sc2->Var->Ty->Kind == TY_FUNC)
        return Sc2->Var;
    return NULL;
}

// 将函数标记为存活状态
static void markLive(Obj *Var) {
    // 如果不是函数或已存活，直接返回
    if (!Var->Ty->Kind == TY_FUNC || Var->IsLive)
        return;
    // 将函数设置为存活状态
    Var->IsLive = true;

    // 遍历该函数的所有引用过的函数，将它们也设为存活状态
    for (int I = 0; I < Var->Refs.Len; I++) {
        Obj *Fn = findFunc(Var->Refs.Data[I]);
        if (Fn)
            markLive(Fn);
    }
}

// functionDefinition = declspec declarator compoundStmt*
//                    | declspec declarator ["," declarator]* ";"
static Token *function(Token *Tok, Type *BaseTy, VarAttr *Attr) {
    Type *Ty = declarator(&Tok, Tok, BaseTy);
    if (!Ty->Name)
        errorTok(Ty->NamePos, "function name omitted");


    // functions are also global variables
    // 函数名称
    char *NameStr = getIdent(Ty->Name);

    Obj *Fn = findFunc(NameStr);
    if (Fn) {
        // 重复定义的函数
        if (Fn->Ty->Kind != TY_FUNC)
            errorTok(Tok, "redeclared as a different kind of symbol");
        if (Fn->IsDefinition && equal(Tok, "{"))
            errorTok(Tok, "redefinition of %s", NameStr);
        if (!Fn->IsStatic && Attr->IsStatic)
            errorTok(Tok, "static declaration follows a non-static declaration");
        Fn->IsDefinition = Fn->IsDefinition || equal(Tok, "{");
    } else {
        Fn = newGVar(NameStr, Ty);
        Fn->Ty->Kind = TY_FUNC;
        Fn->IsDefinition = equal(Tok, "{");
        Fn->IsStatic = Attr->IsStatic || (Attr->IsInline && !Attr->IsExtern);
        Fn->IsInline = Attr->IsInline;
    }

    // 非static inline函数标记为根函数
    Fn->IsRoot = !(Fn->IsStatic && Fn->IsInline);

    if(consume(&Tok, Tok, ";"))
        return Tok;

    // continous defination is also allowed, although this may be not very useful
    // e.g:
    // double a(), *b(), c(int a);
    bool isMultiDefination = consume(&Tok, Tok, ",");
    if(isMultiDefination){
        while(!equal(Tok, ";")){
            Type * Ty = declarator(&Tok, Tok, BaseTy);
            if (!Ty->Name)
                errorTok(Ty->NamePos, "function name omitted");
            Obj *Fn = newGVar(getIdent(Ty->Name), Ty);
            Fn->IsStatic = Attr->IsStatic;
            Fn->IsDefinition = true;
            if(!equal(Tok, ";"))
                Tok = skip(Tok, ",");
        }
        return Tok;
    }

    // this variable will later used by compoundStmt
    CurrentFn = Fn;
    // 清空全局变量Locals
    Locals = (void*)0;
    enterScope();
    // 函数参数
    createParamLVars(Ty->Params);

    // 有大于16字节的结构体返回值的函数
    Type *RTy = Ty->ReturnTy;
    if ((RTy->Kind == TY_STRUCT || RTy->Kind == TY_UNION) && RTy->Size > 16)
        // 第一个形参是隐式的，包含了结构体的地址
        newLVar("", pointerTo(RTy));

    Fn->Params = Locals;
    // 判断是否为可变参数
    if (Ty->IsVariadic)
        Fn->VaArea = newLVar("__va_area__", arrayOf(TyChar, 0));

    // 记录Alloca区域底部
    Fn->AllocaBottom = newLVar("__alloca_size__", pointerTo(TyChar));


    // __func__被定义为包含当前函数名称的局部?变量
    pushScope("__func__")->Var =
        newStringLiteral(Fn->Name, arrayOf(TyChar, strlen(Fn->Name) + 1));
    pushScope("__FUNCTION__")->Var =
        newStringLiteral(Fn->Name, arrayOf(TyChar, strlen(Fn->Name) + 1));


    // 函数体存储语句的AST，Locals存储变量
    Fn->Body = compoundStmt(&Tok, Tok);
    Fn->Locals = Locals;
    leaveScope();
    // 处理goto和标签
    resolveGotoLabels();
    return Tok;
}


// declspec = ("int" | "char" | "long" | "short" | "void"  | "_Bool"
//              | "typedef" | "static" | "extern" | "inline"
//              | "_Thread_local" | "__thread"
//              | "signed" | "unsigned"
//              | "_Alignas" ("(" typeName | constExpr ")")
//              | structDecl | unionDecl | typedefName
//              | enumSpecifier | typeofSpecifier
//              | "const" | "volatile" | "auto" | "register" | "restrict"
//              | "__restrict" | "__restrict__" | "_Noreturn")+
// 声明的 基础类型. declaration specifiers
static Type *declspec(Token **Rest, Token *Tok, VarAttr *Attr) {
    // 类型的组合，被表示为例如：LONG+LONG=1<<9
    // 可知long int和int long是等价的。
    enum {
        VOID   = 1 << 0,
        BOOL   = 1 << 2,
        CHAR   = 1 << 4,
        SHORT  = 1 << 6,
        INT    = 1 << 8,
        LONG   = 1 << 10,
        FLOAT  = 1 << 12,
        DOUBLE = 1 << 14,
        OTHER  = 1 << 16,
        SIGNED = 1 << 17,
        UNSIGNED = 1 << 18,
    };
    // default int
    Type *Ty = TyInt;
    bool IsAtomic = false;
    int Counter = 0; // 记录类型相加的数值
    // typedef int intt
    // 遍历所有类型名的Tok
    while (isTypename(Tok)) {
        // 处理typedef等关键字
        if (equal2(Tok, 6, stringSet("static", "typedef", "extern", "inline", "__thread", "_Thread_local"))) {
            if (!Attr)
                errorTok(Tok, "storage class specifier is not allowed in this context");
            if (equal(Tok, "typedef"))
                Attr->IsTypedef = true;
            else if(equal(Tok, "static"))
                Attr->IsStatic = true;
            else if(equal(Tok, "extern"))
                Attr->IsExtern = true;
            else if(equal(Tok, "inline"))
                Attr->IsInline = true;
            else
                Attr->IsTLS = true;

            // typedef不应与static/extern/inline/__thread/_Thread_local一起使用
            if (Attr->IsTypedef && (Attr->IsStatic || Attr->IsExtern || Attr->IsInline || Attr->IsTLS))
                errorTok(Tok, "typedef and static/extern/inline/__thread/_Thread_local may not be used together");

            Tok = Tok->Next;
            continue;
        }

        char * dontcare[] = 
            {"const", "volatile", "auto", "register", "restrict",
                "__restrict", "__restrict__", "_Noreturn"
            };
        if(equal2(Tok, sizeof(dontcare) / sizeof(*dontcare), dontcare)){
            Tok = Tok->Next;
            continue;
        }

        // 匹配是否为原子的
        if (equal(Tok, "_Atomic")) {
            Tok = Tok->Next;
            if (equal(Tok, "(")) {
                Ty = typename(&Tok, Tok->Next);
                Tok = skip(Tok, ")");
            }
            IsAtomic = true;
            continue;
        }

        // _Alignas "(" typeName | constExpr ")"
        if (equal(Tok, "_Alignas")) {
            // 不存在变量属性时，无法设置对齐值
            if (!Attr)
                errorTok(Tok, "_Alignas is not allowed in this context");
            Tok = skip(Tok->Next, "(");

            // 判断是类型名，或者常量表达式
            if (isTypename(Tok))
                Attr->Align = typename(&Tok, Tok)->Align;
            else
                Attr->Align = constExpr(&Tok, Tok);
            Tok = skip(Tok, ")");
            continue;
        }

        // 处理用户定义的类型
        Type *Ty2 = findTypedef(Tok);
        if (equal2(Tok, 4, stringSet("struct", "union", "enum", "typeof")) || Ty2) {
            if (Counter)
                break;

            if (equal(Tok, "struct")) {
                Ty = structDecl(&Tok, Tok->Next);
            }
            else if (equal(Tok, "union")) {
                Ty = unionDecl(&Tok, Tok->Next);
            }
            else if(equal(Tok, "enum")){
                Ty = enumSpecifier(&Tok, Tok->Next);
            }
            else if (equal(Tok, "typeof")) {
                Ty = typeofSpecifier(&Tok, Tok->Next);
            }
            else {
                // 将类型设为类型别名指向的类型
                Ty = Ty2;
                Tok = Tok->Next;
            }
            Counter += OTHER;
            continue;
        }


        // 对于出现的类型名加入Counter
        // 每一步的Counter都需要有合法值
        if (equal(Tok, "void"))
            Counter += VOID;
        else if(equal(Tok, "_Bool"))
            Counter += BOOL;
        else if (equal(Tok, "char"))
            Counter += CHAR;
        else if (equal(Tok, "short"))
            Counter += SHORT;
        else if (equal(Tok, "int"))
            Counter += INT;
        else if (equal(Tok, "long"))
            Counter += LONG;
        else if (equal(Tok, "signed"))
            Counter |= SIGNED;
        else if (equal(Tok, "unsigned"))
            Counter |= UNSIGNED;
        else if (equal(Tok, "float"))
            Counter += FLOAT;
        else if (equal(Tok, "double"))
            Counter += DOUBLE;
        else
            error("unreachable");

        // 根据Counter值映射到对应的Type
        switch (Counter) {
            case VOID:
                Ty = TyVoid;
                break;
            case BOOL:
                Ty = TyBool;
                break;
            // RISCV当中char是无符号类型的
            case CHAR:
            case UNSIGNED + CHAR:
                Ty = TyUChar;
                break;
            case SIGNED + CHAR:
                Ty = TyChar;
                break;
            case UNSIGNED + SHORT:
            case UNSIGNED + SHORT + INT:
                Ty = TyUShort;
                break;
            case SHORT:
            case SHORT + INT:
            case SIGNED + SHORT:
            case SIGNED + SHORT + INT:
                Ty = TyShort;
                break;
            case UNSIGNED:
            case UNSIGNED + INT:
                Ty = TyUInt;
                break;
            case INT:
            case SIGNED:
            case SIGNED + INT:
                Ty = TyInt;
                break;
            case UNSIGNED + LONG:
            case UNSIGNED + LONG + INT:
            case UNSIGNED + LONG + LONG:
            case UNSIGNED + LONG + LONG + INT:
                Ty = TyULong;
                break;
            case LONG:
            case LONG + INT:
            case LONG + LONG:
            case LONG + LONG + INT:
            case SIGNED + LONG:
            case SIGNED + LONG + INT:
            case SIGNED + LONG + LONG:
            case SIGNED + LONG + LONG + INT:
                Ty = TyLong;
                break;
            case FLOAT:
                Ty = TyFloat;
                break;
            case DOUBLE:
                Ty = TyDouble;
                break;
            case LONG + DOUBLE:
                Ty = TyLDouble;
                break;
            default:
                errorTok(Tok, "invalid type");
        }

        Tok = Tok->Next;
    }
    if (IsAtomic) {
        Ty = copyType(Ty);
        // 类型被标记为原子的
        Ty->IsAtomic = true;
    }

    *Rest = Tok;
    return Ty;
}

// pointers = ("*" ("const" | "volatile" | "restrict")*)*
static Type *pointers(Token **Rest, Token *Tok, Type *Ty) {
    // "*"*
    // 构建所有的（多重）指针
    while (consume(&Tok, Tok, "*")) {
        Ty = pointerTo(Ty);
        char *dontcare[] = {"const", "volatile", "restrict", "__restrict", "__restrict__"};
        // 识别这些关键字并忽略
        while(equal2(Tok, sizeof(dontcare)/ sizeof(*dontcare), dontcare))
            Tok = Tok->Next;
    }
    *Rest = Tok;
    return Ty;
}

/*declarator：
    声明符，其实是介于declspec（声明的基础类型）与一直到声明结束这之间的所有东西("{" for fn and ";" for var)。
    与前面的declspec搭配就完整地定义了一个函数的签名。也可以用来定义变量*/
// declarator = pointers ("(" ident ")" | "(" declarator ")" | ident) typeSuffix
// pointers = ("*" ("const" | "volatile" | "restrict")*)*
// int *** (a)[6] | int **(*(*(**a[6])))[6] | int **a[6]
// the 2nd case is a little difficult to handle with... and that's also where the recursion begins
// examples: ***var, fn(int x), a
// a further step on type parsing. also help to assign Ty->Name
Type *declarator(Token **Rest, Token *Tok, Type *Ty) {
    // 构建所有的（多重）指针
    Ty = pointers(&Tok, Tok, Ty);
    // "(" declarator ")", 嵌套类型声明符
    if (equal(Tok, "(")) {
        // 记录"("的位置
        Token *Start = Tok;
        Type Dummy = {};
        declarator(&Tok, Start->Next, &Dummy);
        Tok = skip(Tok, ")");
        // 获取到括号后面的类型后缀，Ty为解析完的类型，Rest指向分号
        Ty = typeSuffix(Rest, Tok, Ty);
        // Ty整体作为Base去构造，返回Type的值
        return declarator(&Tok, Start->Next, Ty);
    }

    // 默认名称为空
    Token *Name = NULL;
    // 名称位置指向类型后的区域
    // ideally this should point to the ident name
    // but also it could be emitted...
    Token *NamePos = Tok;

    // 存在名字则赋值
    if (Tok->Kind == TK_IDENT) {
        Name = Tok;
        Tok = Tok->Next;
    }

    // typeSuffix
    Ty = typeSuffix(Rest, Tok, Ty);
    // ident
    // 变量名 或 函数名, or typedef name
    Ty->Name = Name;
    Ty->NamePos = NamePos;
    return Ty;
}

// funcParams =  "(" "void" | (param ("," param)* "," "..." ? )? ")"
// param = declspec declarator
static Type *funcParams(Token **Rest, Token *Tok, Type *Ty) {
    // skip "(" at the begining of fn
    Tok = skip(Tok, "(");
    // "void"
    if (equal(Tok, "void") && equal(Tok->Next, ")")) {
        *Rest = Tok->Next->Next;
        return funcType(Ty);
    }
    
    // 存储形参的链表
    Type Head = {};
    Type *Cur = &Head;
    bool IsVariadic = false;
    while (!equal(Tok, ")")) {
        // funcParams = param ("," param)*
        // param = declspec declarator
        if (Cur != &Head)
            Tok = skip(Tok, ",");
        // ("," "...")?
        if (equal(Tok, "...")) {
            IsVariadic = true;
            Tok = Tok->Next;
            skip(Tok, ")");
            break;
        }

        Type *Ty2 = declspec(&Tok, Tok, NULL);
        Ty2 = declarator(&Tok, Tok, Ty2);
        Token *Name = Ty2->Name;
        if (Ty2 -> Kind == TY_ARRAY){
            // T类型的数组或函数被转换为T*
            // pointerTo will call calloc to create a new Type,
            // which will clear the name field, so we need to keep and 
            // reassign the name
            Ty2 = pointerTo(Ty2 -> Base);
            Ty2 -> Name = Name;
        }
        else if(Ty2->Kind == TY_FUNC){
            // 在函数参数中退化函数为指针
            Ty2 = pointerTo(Ty2);
            Ty2 -> Name = Name;
        }
        // 将类型复制到形参链表一份. why copy?
        // because we may need to modify(cast) the type in the future.
        // if not copy, then the original type(say TyInt) will also be changed
        // which is unacceptable. something like ownership here
        Cur->Next = copyType(Ty2);
            //Cur->Next = Ty2;
            Cur = Cur->Next;
    }

    // 设置空参函数调用为可变的
    if (Cur == &Head)
        IsVariadic = true;

    // 封装一个函数节点
    Ty = funcType(Ty);
    // 传递形参
    Ty -> Params = Head.Next;
    // 传递可变参数
    Ty->IsVariadic = IsVariadic;
    // skip ")" at the end of function
    *Rest = Tok->Next;
    return Ty;
}

// typeSuffix = ( funcParams?  | "[" arrayDimensions? "] typeSuffix")?
// arrayDimensions = ("static" | "restrict")* constExpr? "]" typeSuffix
// if function, construct its formal parms. otherwise do nothing
// since we want to recursively construct its type, we need to pass the former type as an argument
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty) {
    // ("(" funcParams? ")")?
    if (equal(Tok, "("))
        return funcParams(Rest, Tok, Ty);
    // "[" arrayDimensions? "] typeSuffix"
    if (equal(Tok, "[")) {
        Tok = skip(Tok, "[");
        // ("static" | "restrict")*
        while (equal(Tok, "static") || equal(Tok, "restrict"))
            Tok = Tok->Next;
        // 无数组维数的 "[]"
        // sizeof(int(*)[][10])
        if(equal(Tok, "]")){
            Ty = typeSuffix(Rest, Tok->Next, Ty);
            return arrayOf(Ty, -1);
        }
        // 有数组维数的情况
        else{
            Node *Expr = conditional(&Tok, Tok);
            Tok = skip(Tok, "]");
            Ty = typeSuffix(Rest, Tok, Ty);
            // 处理可变长度数组
            if (Ty->Kind == TY_VLA || !isConstExpr(Expr))
                return VLAOf(Ty, Expr);
            // 处理固定长度数组

            return arrayOf(Ty, eval(Expr));         // recursion 
        }
    }

    // nothing special here, just return the original type
    *Rest = Tok;
    return Ty;
}

// structMembers = (declspec declarator (","  declarator)* ";")*
static void structMembers(Token **Rest, Token *Tok, Type *Ty) {
    Member Head = {};
    Member *Cur = &Head;
    // 记录成员变量的索引值
    int Idx = 0;
    // struct {int a; int b;} x
    while (!equal(Tok, "}")) {
        // declspec
        VarAttr Attr = {};
        Type *BaseTy = declspec(&Tok, Tok, &Attr);
        int First = true;

        // 匿名的结构体成员
        if ((BaseTy->Kind == TY_STRUCT || BaseTy->Kind == TY_UNION) && 
                consume(&Tok, Tok, ";")) {
            Member *Mem = calloc(1, sizeof(Member));
            Mem->Ty = BaseTy;
            Mem->Idx = Idx++;
            // 如果对齐值不存在，则使用匿名成员的对齐值
            Mem->Align = Attr.Align ? Attr.Align : Mem->Ty->Align;
            Cur = Cur->Next = Mem;
            continue;
        }

        // 常规结构体成员
        while (!consume(&Tok, Tok, ";")) {
            if (!First)
                Tok = skip(Tok, ",");
            First = false;

            Member *Mem = calloc(1, sizeof(Member));
            // declarator
            Mem->Ty = declarator(&Tok, Tok, BaseTy);
            Mem->Name = Mem->Ty->Name;
            // 成员变量对应的索引值
            Mem->Idx = Idx++;
            // 设置对齐值
            Mem->Align = Attr.Align ? Attr.Align : Mem->Ty->Align;

            // 位域成员赋值
            if (consume(&Tok, Tok, ":")) {
                Mem->IsBitfield = true;
                Mem->BitWidth = constExpr(&Tok, Tok);
            }

            Cur = Cur->Next = Mem;
        }
    }

    // 解析灵活数组成员，数组大小设为0
    // C99 variable length array(flexible array)
    // see https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html
    if (Cur != &Head && Cur->Ty->Kind == TY_ARRAY && Cur->Ty->ArrayLen < 0){
        Cur->Ty = arrayOf(Cur->Ty->Base, 0);
        Ty -> IsFlexible = true;
    }
    *Rest = skip(Tok, "}");
    Ty->Mems = Head.Next;
}

// attribute = ("__attribute__" "(" "(" ("packed")
//                                    | ("aligned" "(" N ")") ")" ")")*
static Token *attribute(Token *Tok, Type *Ty) {
    // 解析__attribute__相关的属性
    while (consume(&Tok, Tok, "__attribute__")) {
        Tok = skip(Tok, "(");
        Tok = skip(Tok, "(");

        bool First = true;

        while (!consume(&Tok, Tok, ")")) {
            if (!First)
                Tok = skip(Tok, ",");
            First = false;

            // "packed"
            if (consume(&Tok, Tok, "packed")) {
                Ty->IsPacked = true;
                continue;
            }

            // "aligned" "(" N ")"
            if (consume(&Tok, Tok, "aligned")) {
                Tok = skip(Tok, "(");
                Ty->Align = constExpr(&Tok, Tok);
                Tok = skip(Tok, ")");
                continue;
            }

            errorTok(Tok, "unknown attribute");
        }

        Tok = skip(Tok, ")");
    }
    return Tok;
}
// structDecl = "{" structMembers "}"
// specially, this function has 2 usages:
//  1. declare a struct variable
//      struct (tag)? {int a; ...} foo; foo.a = 10;
//  2. use a struct tag to type a variable
//      struct tag bar; bar.a = 1;

// structUnionDecl = attribute? ident? ("{" structMembers "}")?
static Type *structUnionDecl(Token **Rest, Token *Tok) {
    // 构造结构体类型
    Type *Ty = structType();
    // 设置相关属性
    Tok = attribute(Tok, Ty);

    // 读取标签
    Token *Tag = NULL;
    if (Tok->Kind == TK_IDENT) {
        Tag = Tok;
        Tok = Tok->Next;
    }

    // 构造不完整结构体
    if (Tag && !equal(Tok, "{")) {
        *Rest = Tok;
        Type *Ty2 = findTag(Tag);
        if (Ty2)
            return Ty2;
        Ty->Size = -1;
        pushTagScope(Tag, Ty);
        return Ty;
    }

    Tok = skip(Tok, "{");
    // 构造一个结构体
    structMembers(&Tok, Tok, Ty);
    *Rest = attribute(Tok, Ty);

    // 如果是重复定义，就覆盖之前的定义。否则有名称就注册结构体类型
    if (Tag) {
        Type *Ty2 = hashmapGet(&Scp->Tags, tokenName(Tag));
        if (Ty2) {
            *Ty2 = *Ty;
            return Ty2;
        }
        pushTagScope(Tag, Ty);
    }
    return Ty;
}

// structDecl = structUnionDecl
static Type *structDecl(Token **Rest, Token *Tok) {
    Type *Ty = structUnionDecl(Rest, Tok);
    Ty->Kind = TY_STRUCT;

    // 不完整结构体
    if (Ty->Size < 0)
        return Ty;

    // 计算结构体内成员的偏移量
    int Bits = 0;
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
        if (Mem->IsBitfield && Mem->BitWidth == 0) {
            // 零宽度的位域有特殊含义，仅作用于对齐
            Bits = alignTo(Bits, Mem->Ty->Size * 8);
        }
        else if (Mem->IsBitfield){
            // 位域成员变量
            int Sz = Mem->Ty->Size;
            // Bits此时对应成员最低位，Bits + Mem->BitWidth - 1对应成员最高位
            // 二者若不相等，则说明当前这个类型剩余的空间存不下，需要新开辟空间
            if (Bits / (Sz * 8) != (Bits + Mem->BitWidth - 1) / (Sz * 8))
                // 新开辟一个当前当前类型的空间
                Bits = alignTo(Bits, Sz * 8);

                // 若当前字节能够存下，则向下对齐，得到成员变量的偏移量
                Mem->Offset = alignDown(Bits / 8, Sz);
                Mem->BitOffset = Bits % (Sz * 8);
                Bits += Mem->BitWidth;
        } else {
            // 常规结构体成员变量
            if (!Ty->IsPacked)
                Bits = alignTo(Bits, Mem->Align * 8);
            Mem->Offset = Bits / 8;
            Bits += Mem->Ty->Size * 8;
        }

        // 类型的对齐值，不小于当前成员变量的对齐值
        if (!Ty->IsPacked && Ty->Align < Mem->Align)
            Ty->Align = Mem->Align;
    }

    Ty->Size = alignTo(Bits, Ty->Align * 8) / 8;

    return Ty;
}

// unionDecl = structUnionDecl
static Type *unionDecl(Token **Rest, Token *Tok) {
    Type *Ty = structUnionDecl(Rest, Tok);
    Ty->Kind = TY_UNION;

    // 联合体需要设置为最大的对齐量与大小，变量偏移量都默认为0
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
        if (Ty->Align < Mem->Align)
            Ty->Align = Mem->Align;
        if (Ty->Size < Mem->Ty->Size)
            Ty->Size = Mem->Ty->Size;
    }
    // 将大小对齐
    Ty->Size = alignTo(Ty->Size, Ty->Align);
    return Ty;
}

// 获取结构体成员
Member *getStructMember(Type *Ty, Token *Tok) {
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next){
        // 匿名结构体成员，可以由上一级的结构体进行访问
        if ((Mem->Ty->Kind == TY_STRUCT || Mem->Ty->Kind == TY_UNION) && !Mem->Name) {
            if (getStructMember(Mem->Ty, Tok))
                return Mem;
            continue;
        }

        // 常规结构体成员
        if (Mem->Name->Len == Tok->Len &&
            !strncmp(Mem->Name->Loc, Tok->Loc, Tok->Len))
            return Mem;
    }
    return NULL;
}

// a.x 
//      ND_MEMBER -> x (Nd->Mem)
//       /   
//     a (ND_VAR)
// 构建结构体成员的节点. LHS = that struct variable
// 匿名结构体成员，可以由上一级的结构体进行访问
static Node *structRef(Node *Nd, Token *Tok) {
    addType(Nd);
    if (Nd->Ty->Kind != TY_STRUCT && Nd->Ty->Kind != TY_UNION)
        errorTok(Nd->Tok, "not a struct nor a union");

    // 节点类型
    Type *Ty = Nd->Ty;
    // 遍历匿名的成员，直到匹配到具名的
    while (true) {
        // 获取结构体成员
        Member *Mem = getStructMember(Ty, Tok);
        if (!Mem)
            errorTok(Tok, "no such member");
        // 构造成员节点
        Nd = newUnary(ND_MEMBER, Nd, Tok);
        Nd->Mem = Mem;
        // 判断是否具名
        if (Mem->Name)
            break;
            // 继续遍历匿名成员的成员
            Ty = Mem->Ty;
    }
    return Nd;
}

// typeofSpecifier = "(" (expr | typename) ")"
// typeof 获取对应的类型
static Type *typeofSpecifier(Token **Rest, Token *Tok) {
    // "("
    Tok = skip(Tok, "(");

    Type *Ty;
    if (isTypename(Tok)) {
        // typename
        // 匹配到相应的类型
        Ty = typename(&Tok, Tok);
    } else {
        // expr
        // 计算表达式，然后获取表达式的类型
        Node *Nd = expr(&Tok, Tok);
        addType(Nd);
        Ty = Nd->Ty;
    }
    // ")"
    *Rest = skip(Tok, ")");
    // 将获取的类型进行返回
    return Ty;
}

// 获取枚举类型信息
// enumSpecifier = ident? "{" enumList? "}"
//               | ident ("{" enumList? "}")?
// enumList      = ident ("=" constExpr)? ("," ident ("=" constExpr)?)* ","?
static Type *enumSpecifier(Token **Rest, Token *Tok) {
    Type *Ty = enumType();
    // 读取标签
    // ident?
    Token *Tag = NULL;
    if (Tok->Kind == TK_IDENT) {
        Tag = Tok;
        Tok = Tok->Next;
    }

    // 处理没有{}的情况
    if (Tag && !equal(Tok, "{")) {
        Type *Ty = findTag(Tag);
        if (!Ty)
            errorTok(Tag, "unknown enum type");
        if (Ty->Kind != TY_ENUM)
            errorTok(Tag, "not an enum tag");
        *Rest = Tok;
        return Ty;
    }

    // "{" enumList? "}"
    Tok = skip(Tok, "{");

    // enumList
    // 读取枚举列表
    int I = 0;   // 第几个枚举常量
    int Val = 0; // 枚举常量的值
    while (!consumeEnd(Rest, Tok)) {
        if (I++ > 0)
            Tok = skip(Tok, ",");

        char *Name = getIdent(Tok);
        Tok = Tok->Next;

        // 判断是否存在赋值
        if (equal(Tok, "="))
            Val = constExpr(&Tok, Tok->Next);
        // 存入枚举常量
        VarScope *S = pushScope(Name);
        S->EnumTy = Ty;
        S->EnumVal = Val++;
    }

    if (Tag)
        pushTagScope(Tag, Ty);
    return Ty;
}


// declaration = declspec (declarator ("=" initializer)?
//                         ("," declarator ("=" initializer)?)*)? ";"
// initializer = "{" initializer ("," initializer)* "}" | assign
// add a variable to current scope, then create a node with kind ND_ASSIGN if possible
static Node *declaration(Token **Rest, Token *Tok, Type *BaseTy, VarAttr *Attr) {
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
        Type *Ty = declarator(&Tok, Tok, BaseTy);

        if(Ty->Kind == TY_VOID)
            errorTok(Tok, "variable declared void");

        if (!Ty->Name)
            errorTok(Ty->NamePos, "variable name omitted");

        if (Attr && Attr->IsStatic) {
            // 静态局部变量
            // note: this is different from general global variable.
            // its name will be replaced with an anonymous one
            // although it's also global, the conflict of name duplication won't happen
            Obj *Var = newAnonGVar(Ty);
            pushScope(getIdent(Ty->Name))->Var = Var;
            if (equal(Tok, "="))
                GVarInitializer(&Tok, Tok->Next, Var);
            continue;
        }       

        // 生成代码计算VLA的大小
        // 在此生成是因为，即使Ty不是VLA，但也可能是一个指向VLA的指针
        Cur->Next = newUnary(ND_EXPR_STMT, computeVLASize(Ty, Tok), Tok);
        Cur = Cur->Next;

        // 处理可变长度数组
        if (Ty->Kind == TY_VLA) {
            if (equal(Tok, "="))
                errorTok(Tok, "variable-sized object may not be initialized");

            // VLA被改写为alloca()调用
            // 例如：`int X[N]`被改写为`Tmp = N, X = alloca(Tmp)`

            // X
            Obj *Var = newLVar(getIdent(Ty->Name), Ty);
            // X的类型名
            Token *Tok = Ty->Name;
            // X = alloca(Tmp)，VLASize对应N
            Node *Expr = newBinary(ND_ASSIGN, newVLAPtr(Var, Tok),
                                    newAlloca(newVarNode(Ty->VLASize, Tok)), Tok);

            // 存放在表达式语句中
            Cur->Next = newUnary(ND_EXPR_STMT, Expr, Tok);
            Cur = Cur->Next;
            continue;
        }

        Obj *Var = newLVar(getIdent(Ty->Name), Ty);
        // 读取是否存在变量的对齐值
        if (Attr && Attr->Align)
            Var->Align = Attr->Align;

        if (equal(Tok, "=")) {
            // 解析变量的初始化器
            Node *Expr = LVarInitializer(&Tok, Tok->Next, Var);
            // 存放在表达式语句中
            Cur->Next = newUnary(ND_EXPR_STMT, Expr, Tok);
            Cur = Cur->Next;
        }
        if (Var->Ty->Size < 0)
            errorTok(Ty->Name, "variable has incomplete type. size = %d?", Var->Ty->Size);
        if (Var->Ty->Kind == TY_VOID)
            errorTok(Ty->Name, "variable declared void");

    }
    // 将所有表达式语句，存放在代码块中
    Node *Nd = newNode(ND_BLOCK, Tok);
    Nd->Body = Head.Next;
    *Rest = Tok->Next;
    return Nd;
}

// 解析复合语句
// compoundStmt = "{" ( typedef | stmt | declaration)* "}"
static Node *compoundStmt(Token **Rest, Token *Tok) {
    // 这里使用了和词法分析类似的单向链表结构
    Tok = skip(Tok, "{");
    Node Head = {};
    Node *Cur = &Head;
    enterScope();
    // (stmt | declaration)* "}"
    while (!equal(Tok, "}")) {
        if (isTypename(Tok) && !equal(Tok->Next, ":")) {
            VarAttr Attr = {};
            Type *BaseTy = declspec(&Tok, Tok, &Attr);
            // 解析typedef的语句
            if (Attr.IsTypedef) {
                Tok = parseTypedef(Tok, BaseTy);
                continue;
            }

            // 处理块中的函数声明
            if (isFunction(Tok)) {
                Tok = function(Tok, BaseTy, &Attr);
                continue;
            }

            // 解析外部全局变量
            if (Attr.IsExtern) {
                Tok = globalVariable(Tok, BaseTy, &Attr);
                continue;
            }

            // 解析变量声明语句
            Cur->Next = declaration(&Tok, Tok, BaseTy, &Attr);
        }

        // stmt
        else
            Cur->Next = stmt(&Tok, Tok);
        Cur = Cur->Next;
        addType(Cur);
    }
    leaveScope();
    // Nd的Body存储了{}内解析的语句
    Node *Nd = newNode(ND_BLOCK, Tok);
    Nd->Body = Head.Next;
    *Rest = Tok->Next;
    return Nd;
}

// asmStmt = "asm" ("volatile" | "inline")* "(" stringLiteral ")"
static Node *asmStmt(Token **Rest, Token *Tok) {
    Node *Nd = newNode(ND_ASM, Tok);
    Tok = Tok->Next;

    // ("volatile" | "inline")*
    while (equal(Tok, "volatile") || equal(Tok, "inline"))
        Tok = Tok->Next;

    // "("
    Tok = skip(Tok, "(");
    // stringLiteral
    if (Tok->Kind != TK_STR || Tok->Ty->Base->Kind != TY_CHAR)
        errorTok(Tok, "expected string literal");
    Nd->AsmStr = Tok->Str;
    // ")"
    *Rest = skip(Tok->Next, ")");
    return Nd;
}

// 解析语句
// stmt = "return" expr? ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | compoundStmt
//        | exprStmt
//        | asmStmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "do" stmt "while" "(" expr ")" ";"
//        | "while" "(" expr ")" stmt
//        | "goto" (ident | "*" expr) ";"
//        | ident ":" stmt
//        | "break;" | "continue;"
//        | "switch" "(" expr ")" stmt
//        | "case" constExpr ("..." constExpr)? ":" stmt
//        | "default" ":" stmt
static Node *stmt(Token **Rest, Token *Tok) { 
    // "return" expr ";"
    if (equal(Tok, "return")) {
        Node *Nd = newNode(ND_RETURN, Tok);
        // 空返回语句
        if (consume(Rest, Tok->Next, ";"))
            return Nd;

        Node *Exp = expr(&Tok, Tok->Next);
        addType(Exp);
        *Rest = skip(Tok, ";");
        Type *Ty = CurrentFn->Ty->ReturnTy;

        // 对于返回值为结构体时不进行类型转换
        if (Ty->Kind != TY_STRUCT && Ty->Kind != TY_UNION)
            Exp = newCast(Exp, CurrentFn->Ty->ReturnTy);
        Nd->LHS = Exp;
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
        // 存储此前break和continue标签的名称
        char *Brk = BrkLabel;
        char *Cont = ContLabel;
        Node *Nd = newNode(ND_FOR, Tok);
        // 设置break标签的名称
        BrkLabel = Nd->BrkLabel = newUniqueName();
        ContLabel = Nd->ContLabel = newUniqueName();

        // 进入for循环域
        enterScope();
        // "("
        Tok = skip(Tok->Next, "(");

        if (isTypename(Tok)) {
            // 初始化循环变量
            Type *BaseTy = declspec(&Tok, Tok, NULL);
            Nd->Init = declaration(&Tok, Tok, BaseTy, NULL);
        } else {
            // 初始化语句
            Nd->Init = exprStmt(&Tok, Tok);
        }

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
        // 恢复此前的break和continue标签
        BrkLabel = Brk;
        ContLabel = Cont;

        leaveScope();
        return Nd;
    }

    // "while" "(" expr ")" stmt
    // while(cond){then...}
    // note: while is implemented by for
    if (equal(Tok, "while")) {
        // 存储此前break和continue标签的名称
        char *Brk = BrkLabel;
        char *Cont = ContLabel;
        Node *Nd = newNode(ND_FOR, Tok);
        // 设置break标签的名称
        BrkLabel = Nd->BrkLabel = newUniqueName();
        ContLabel = Nd->ContLabel = newUniqueName();
        // "("
        Tok = skip(Tok->Next, "(");
        // expr
        Nd->Cond = expr(&Tok, Tok);
        // ")"
        Tok = skip(Tok, ")");

        // stmt
        Nd->Then = stmt(Rest, Tok);
        // 恢复此前的break和continue标签
        BrkLabel = Brk;
        ContLabel = Cont;
        return Nd;
    }

    // "do" stmt "while" "(" expr ")" ";"
    if (equal(Tok, "do")) {
        Node *Nd = newNode(ND_DO, Tok);

        // 存储此前break和continue标签的名称
        char *Brk = BrkLabel;
        char *Cont = ContLabel;
        // 设置break和continue标签的名称
        BrkLabel = Nd->BrkLabel = newUniqueName();
        ContLabel = Nd->ContLabel = newUniqueName();

        // stmt
        // do代码块内的语句
        Nd->Then = stmt(&Tok, Tok->Next);

        // 恢复此前的break和continue标签
        BrkLabel = Brk;
        ContLabel = Cont;

        // "while" "(" expr ")" ";"
        Tok = skip(Tok, "while");
        Tok = skip(Tok, "(");
        // expr
        // while使用的条件表达式
        Nd->Cond = expr(&Tok, Tok);
        Tok = skip(Tok, ")");
        *Rest = skip(Tok, ";");
        return Nd;
    }

    // "goto" ident ";"
    if (equal(Tok, "goto")) {
        if (equal(Tok->Next, "*")) {
            // `goto *Ptr`跳转到Ptr指向的地址
            Node *Nd = newNode(ND_GOTO_EXPR, Tok);
            Nd->LHS = expr(&Tok, Tok->Next->Next);
            *Rest = skip(Tok, ";");
            return Nd;
        }
        Node *Nd = newNode(ND_GOTO, Tok);
        Nd->Label = getIdent(Tok->Next);
        // 将Nd同时存入Gotos，最后用于解析UniqueLabel
        Nd->GotoNext = Gotos;
        Gotos = Nd;
        *Rest = skip(Tok->Next->Next, ";");
        return Nd;
    }

    // "break" ";"
    if (equal(Tok, "break")) {
        if (!BrkLabel)
            errorTok(Tok, "stray break");
        // 跳转到break标签的位置
        Node *Nd = newNode(ND_GOTO, Tok);
        Nd->UniqueLabel = BrkLabel;
        *Rest = skip(Tok->Next, ";");
        return Nd;
    }

    // "continue" ";"
    if (equal(Tok, "continue")) {
        if (!ContLabel)
            errorTok(Tok, "stray continue");
        // 跳转到continue标签的位置
        Node *Nd = newNode(ND_GOTO, Tok);
        Nd->UniqueLabel = ContLabel;
        *Rest = skip(Tok->Next, ";");
        return Nd;
    }
    // "switch" "(" expr ")" stmt
        if (equal(Tok, "switch")) {
        // 记录此前的CurrentSwitch
        Node *Sw = CurrentSwitch;

        Node *Nd = newNode(ND_SWITCH, Tok);
        Tok = skip(Tok->Next, "(");
        Nd->Cond = expr(&Tok, Tok);
        Tok = skip(Tok, ")");

        // 设置当前的CurrentSwitch
        CurrentSwitch = Nd;

        // 存储此前break标签的名称
        char *Brk = BrkLabel;
        // 设置break标签的名称
        BrkLabel = Nd->BrkLabel = newUniqueName();

        // 进入解析各个case
        // stmt
        Nd->Then = stmt(Rest, Tok);

        // 恢复此前CurrentSwitch
        CurrentSwitch = Sw;
        // 恢复此前break标签的名称
        BrkLabel = Brk;
        return Nd;
    }

    // "case" constExpr ("..." constExpr)? ":" stmt
    // e.g. case 0 ... 10:
    if (equal(Tok, "case")) {
        if (!CurrentSwitch)
            errorTok(Tok, "stray case");
        // case后面的数值
        int Begin = constExpr(&Tok, Tok->Next);
        // ...后面的数值
        int End;
        // 存在...
        if (equal(Tok, "...")) {
            // 解析...后面的数值
            End = constExpr(&Tok, Tok->Next);
            if (End < Begin)
                errorTok(Tok, "empty case range specified");
        } else {
            // 不存在...
            End = Begin;
        }

        Node *Nd = newNode(ND_CASE, Tok);

        Tok = skip(Tok, ":");
        Nd->Label = newUniqueName();
        // case中的语句
        Nd->LHS = stmt(Rest, Tok);
        // case对应的数值
        Nd->Begin = Begin;
        Nd->End = End;
        // 将旧的CurrentSwitch链表的头部存入Nd的CaseNext
        Nd->CaseNext = CurrentSwitch->CaseNext;
        // 将Nd存入CurrentSwitch的CaseNext
        CurrentSwitch->CaseNext = Nd;
        return Nd;
    }

    // "default" ":" stmt
    if (equal(Tok, "default")) {
        if (!CurrentSwitch)
            errorTok(Tok, "stray default");

        Node *Nd = newNode(ND_CASE, Tok);
        Tok = skip(Tok->Next, ":");
        Nd->Label = newUniqueName();
        Nd->LHS = stmt(Rest, Tok);
        // 存入CurrentSwitch->DefaultCase的默认标签
        CurrentSwitch->DefaultCase = Nd;
        return Nd;
    }

    // asmStmt
    if (equal2(Tok, 2, stringSet("asm", "__asm__")))
        return asmStmt(Rest, Tok);

    // ident ":" stmt
    // labels
    if (Tok->Kind == TK_IDENT && equal(Tok->Next, ":")) {
        Node *Nd = newNode(ND_LABEL, Tok);
        Nd->Label = tokenName(Tok);
        Nd->UniqueLabel = newUniqueName();
        Nd->LHS = stmt(Rest, Tok->Next->Next);
        // 将Nd同时存入Labels，最后用于goto解析UniqueLabel
        Nd->GotoNext = Labels;
        Labels = Nd;
        return Nd;
    }


    // compoundStmt
    if (equal(Tok, "{"))
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
// expr = assign ("," expr)?
static Node *expr(Token **Rest, Token *Tok) {
    Node *Nd = assign(&Tok, Tok);

    if (equal(Tok, ","))
        // this is strange grammar...  the lhs will still make effects, and
        // the final value of the comma expr depends on its right-most one
        return newBinary(ND_COMMA, Nd, expr(Rest, Tok->Next), Tok);
    *Rest = Tok;
    return Nd;
}

// 转换 A op= B为 TMP = &A, *TMP = *TMP op B
static Node *toAssign(Node *Binary) {
    // A
    addType(Binary->LHS);
    // B
    addType(Binary->RHS);
    Token *Tok = Binary->Tok;

    // 结构体需要特殊处理
    // 转换 A.X op= B 为 TMP = &A, (*TMP).X = (*TMP).X op B
    if (Binary->LHS->Kind == ND_MEMBER) {
        // TMP
        Obj *Var = newLVar("", pointerTo(Binary->LHS->LHS->Ty));

        // TMP = &A
        Node *Expr1 = newBinary(ND_ASSIGN, newVarNode(Var, Tok),
                                newUnary(ND_ADDR, Binary->LHS->LHS, Tok), Tok);

        // (*TMP).X ，op=左边的
        Node *Expr2 =
            newUnary(ND_MEMBER, newUnary(ND_DEREF, newVarNode(Var, Tok), Tok), Tok);
        Expr2->Mem = Binary->LHS->Mem;

        // (*TMP).X ，op=右边的
        Node *Expr3 =
            newUnary(ND_MEMBER, newUnary(ND_DEREF, newVarNode(Var, Tok), Tok), Tok);
        Expr3->Mem = Binary->LHS->Mem;

        // (*TMP).X = (*TMP).X op B
        Node *Expr4 =
            newBinary(ND_ASSIGN, Expr2,
                    newBinary(Binary->Kind, Expr3, Binary->RHS, Tok), Tok);

        // TMP = &A, (*TMP).X = (*TMP).X op B
        return newBinary(ND_COMMA, Expr1, Expr4, Tok);
    }

    // 如果 A 是原子的类型，那么 `A op= B` 被转换为
    //
    // ({
    //   T1 *Addr = &A; T2 Val = (B); T1 Old = *Addr; T1 New;
    //   do {
    //     New = Old op Val;
    //   } while (!atomic_compare_exchange_strong(Addr, &Old, New));
    //   New;
    // })
    if (Binary->LHS->Ty->IsAtomic) {
        Node Head = {};
        Node *Cur = &Head;

        Obj *Addr = newLVar("", pointerTo(Binary->LHS->Ty));
        Obj *Val = newLVar("", Binary->RHS->Ty);
        Obj *Old = newLVar("", Binary->LHS->Ty);
        Obj *New = newLVar("", Binary->LHS->Ty);

        // T1 *Addr = &A;
        Cur = Cur->Next =
            newUnary(ND_EXPR_STMT,
                    newBinary(ND_ASSIGN, newVarNode(Addr, Tok),
                            newUnary(ND_ADDR, Binary->LHS, Tok), Tok),
                    Tok);

        // T2 Val = (B);
        Cur = Cur->Next = newUnary(ND_EXPR_STMT,
            newBinary(ND_ASSIGN, newVarNode(Val, Tok), Binary->RHS, Tok), Tok);

        // T1 Old = *Addr;
        Cur = Cur->Next =
                newUnary(ND_EXPR_STMT,
                        newBinary(ND_ASSIGN, newVarNode(Old, Tok),
                                newUnary(ND_DEREF, newVarNode(Addr, Tok), Tok), Tok),
                        Tok);

        //   do {
        //     New = Old op Val;
        //   }
        Node *Loop = newNode(ND_DO, Tok);
        Loop->BrkLabel = newUniqueName();
        Loop->ContLabel = newUniqueName();

        // New = Old op Val;
        Node *Body = newBinary(ND_ASSIGN, newVarNode(New, Tok),
                                newBinary(Binary->Kind, newVarNode(Old, Tok),
                                            newVarNode(Val, Tok), Tok),
                                Tok);

        Loop->Then = newNode(ND_BLOCK, Tok);
        Loop->Then->Body = newUnary(ND_EXPR_STMT, Body, Tok);

        // !atomic_compare_exchange_strong(Addr, &Old, New)
        Node *Cas = newNode(ND_CAS, Tok);
        Cas->CasAddr = newVarNode(Addr, Tok);
        Cas->CasOld = newUnary(ND_ADDR, newVarNode(Old, Tok), Tok);
        Cas->CasNew = newVarNode(New, Tok);
        Loop->Cond = newUnary(ND_NOT, Cas, Tok);

        // while (!atomic_compare_exchange_strong(Addr, &Old, New));
        Cur = Cur->Next = Loop;
        Cur = Cur->Next = newUnary(ND_EXPR_STMT, newVarNode(New, Tok), Tok);

        Node *Nd = newNode(ND_STMT_EXPR, Tok);
        Nd->Body = Head.Next;
        return Nd;
    }

    // 转换 A op= B为 TMP = &A, *TMP = *TMP op B
    // TMP
    Obj *Var = newLVar("", pointerTo(Binary->LHS->Ty));

    // TMP = &A
    Node *Expr1 = newBinary(
        ND_ASSIGN, 
        newVarNode(Var, Tok),
        newUnary(ND_ADDR, Binary->LHS, Tok), 
        Tok
    );

    // *TMP = *TMP op B
    Node *Expr2 = newBinary(
        ND_ASSIGN, 
        newUnary(ND_DEREF, newVarNode(Var, Tok), Tok),
        newBinary(Binary->Kind, newUnary(ND_DEREF, newVarNode(Var, Tok), Tok),
                Binary->RHS, Tok),
        Tok);

    // TMP = &A, *TMP = *TMP op B
    return newBinary(ND_COMMA, Expr1, Expr2, Tok);
}

// difference between expr and assign: expr will add a further step in parsing
// ND_COMMA, which could be unnecessary sometimes and causes bugs. use assign instead
// 解析赋值
// assign = conditional (assignOp assign)?
// assignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>="
Node *assign(Token **Rest, Token *Tok) {
    // equality
    Node *Nd = conditional(&Tok, Tok);

    // 可能存在递归赋值，如a=b=1
    // ("=" assign)?
    if (equal(Tok, "="))
        return Nd = newBinary(ND_ASSIGN, Nd, assign(Rest, Tok->Next), Tok);
    // ("+=" assign)?
    if (equal(Tok, "+="))
        return toAssign(newAdd(Nd, assign(Rest, Tok->Next), Tok));
    // ("-=" assign)?
    if (equal(Tok, "-="))
        return toAssign(newSub(Nd, assign(Rest, Tok->Next), Tok));
    // ("*=" assign)?
    if (equal(Tok, "*="))
        return toAssign(newBinary(ND_MUL, Nd, assign(Rest, Tok->Next), Tok));
    // ("/=" assign)?
    if (equal(Tok, "/="))
        return toAssign(newBinary(ND_DIV, Nd, assign(Rest, Tok->Next), Tok));
    if (equal(Tok, "%="))
        return toAssign(newBinary(ND_MOD, Nd, assign(Rest, Tok->Next), Tok));
    // ("&=" assign)?
    if (equal(Tok, "&="))
        return toAssign(newBinary(ND_BITAND, Nd, assign(Rest, Tok->Next), Tok));

    // ("|=" assign)?
    if (equal(Tok, "|="))
        return toAssign(newBinary(ND_BITOR, Nd, assign(Rest, Tok->Next), Tok));

    // ("^=" assign)?
    if (equal(Tok, "^="))
        return toAssign(newBinary(ND_BITXOR, Nd, assign(Rest, Tok->Next), Tok));

    // ("<<=" assign)?
    if (equal(Tok, "<<="))
        return toAssign(newBinary(ND_SHL, Nd, assign(Rest, Tok->Next), Tok));
    // (">>=" assign)?
    if (equal(Tok, ">>="))
        return toAssign(newBinary(ND_SHR, Nd, assign(Rest, Tok->Next), Tok));


    *Rest = Tok;
    return Nd;
}

// 解析条件运算符
// conditional = logOr ("?" expr? ":" conditional)?
Node *conditional(Token **Rest, Token *Tok) {
    // logOr
    Node *Cond = logOr(&Tok, Tok);

    // "?"
    if (!equal(Tok, "?")) {
        *Rest = Tok;
        return Cond;
    }

    // "?" ":"
    if (equal(Tok->Next, ":")) {
        // `A ?: B` 等价于 `Tmp = A, Tmp ? Tmp : B`
        addType(Cond);
        // Tmp
        Obj *Var = newLVar("", Cond->Ty);
        // Tmp = A
        Node *LHS = newBinary(ND_ASSIGN, newVarNode(Var, Tok), Cond, Tok);
        // Tmp ? Tmp : B
        Node *RHS = newNode(ND_COND, Tok);
        RHS->Cond = newVarNode(Var, Tok);
        RHS->Then = newVarNode(Var, Tok);
        RHS->Els = conditional(Rest, Tok->Next->Next);
        // Tmp = A, Tmp ? Tmp : B
        return newBinary(ND_COMMA, LHS, RHS, Tok);
    }

    // expr ":" conditional
    Node *Nd = newNode(ND_COND, Tok);
    Nd->Cond = Cond;
    // expr ":"
    Nd->Then = expr(&Tok, Tok->Next);
    Tok = skip(Tok, ":");
    // conditional，这里不能被解析为赋值式
    Nd->Els = conditional(Rest, Tok);
    return Nd;
}


// 按位或
// bitOr = bitXor ("|" bitXor)*
static Node *bitOr(Token **Rest, Token *Tok) {
    Node *Nd = bitXor(&Tok, Tok);
    while (equal(Tok, "|")) {
        Token *Start = Tok;
        Nd = newBinary(ND_BITOR, Nd, bitXor(&Tok, Tok->Next), Start);
    }
    *Rest = Tok;
    return Nd;
}

// 逻辑或
// logOr = logAnd ("||" logAnd)*
static Node *logOr(Token **Rest, Token *Tok) {
    Node *Nd = logAnd(&Tok, Tok);
    while (equal(Tok, "||")) {
        Token *Start = Tok;
        Nd = newBinary(ND_LOGOR, Nd, logAnd(&Tok, Tok->Next), Start);
    }
    *Rest = Tok;
    return Nd;
}

// 逻辑与
// logAnd = bitOr ("&&" bitOr)*
static Node *logAnd(Token **Rest, Token *Tok) {
    Node *Nd = bitOr(&Tok, Tok);
    while (equal(Tok, "&&")) {
        Token *Start = Tok;
        Nd = newBinary(ND_LOGAND, Nd, bitOr(&Tok, Tok->Next), Start);
    }
    *Rest = Tok;
    return Nd;
}


// 按位异或
// bitXor = bitAnd ("^" bitAnd)*
static Node *bitXor(Token **Rest, Token *Tok) {
    Node *Nd = bitAnd(&Tok, Tok);
    while (equal(Tok, "^")) {
        Token *Start = Tok;
        Nd = newBinary(ND_BITXOR, Nd, bitAnd(&Tok, Tok->Next), Start);
    }
    *Rest = Tok;
    return Nd;
}

// 按位与
// bitAnd = equality ("&" equality)*
static Node *bitAnd(Token **Rest, Token *Tok) {
    Node *Nd = equality(&Tok, Tok);
    while (equal(Tok, "&")) {
        Token *Start = Tok;
        Nd = newBinary(ND_BITAND, Nd, equality(&Tok, Tok->Next), Start);
    }
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
// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(Token **Rest, Token *Tok) {
    // add
    Node *Nd = shift(&Tok, Tok);

    // ("<" add | "<=" add | ">" add | ">=" add)*
    while (true) {
        Token *start = Tok;
        // "<" shift
        if (equal(Tok, "<")) {
            Nd = newBinary(ND_LT, Nd, shift(&Tok, Tok->Next), start);
            continue;
        }

        // "<=" shift
        if (equal(Tok, "<=")) {
            Nd = newBinary(ND_LE, Nd, shift(&Tok, Tok->Next), start);
            continue;
        }

        // ">" shift
        // X>Y等价于Y<X
        if (equal(Tok, ">")) {
            Nd = newBinary(ND_LT, shift(&Tok, Tok->Next), Nd, start);
            continue;
        }

        // ">=" shift
        // X>=Y等价于Y<=X
        if (equal(Tok, ">=")) {
            Nd = newBinary(ND_LE, shift(&Tok, Tok->Next), Nd, start);
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析位移
// shift = add ("<<" add | ">>" add)*
static Node *shift(Token **Rest, Token *Tok) {
    // add
    Node *Nd = add(&Tok, Tok);

    while (true) {
        Token *Start = Tok;
        // "<<" add
        if (equal(Tok, "<<")) {
            Nd = newBinary(ND_SHL, Nd, add(&Tok, Tok->Next), Start);
            continue;
        }
        // ">>" add
        if (equal(Tok, ">>")) {
            Nd = newBinary(ND_SHR, Nd, add(&Tok, Tok->Next), Start);
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
// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(Token **Rest, Token *Tok) {
    // unary
    Node *Nd = cast(&Tok, Tok);

    // ("*" cast | "/" cast)*
    while (true) {
        Token * start = Tok;
        // "*" cast
        if (equal(Tok, "*")) {
            Nd = newBinary(ND_MUL, Nd, cast(&Tok, Tok->Next), start);
            continue;
        }

        // "/" cast
        if (equal(Tok, "/")) {
            Nd = newBinary(ND_DIV, Nd, cast(&Tok, Tok->Next), start);
            continue;
        }

        if (equal(Tok, "%")) {
            Nd = newBinary(ND_MOD, Nd, cast(&Tok, Tok->Next), start);
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析类型转换
// cast = "(" typeName ")" cast | unary
static Node *cast(Token **Rest, Token *Tok) {
    // cast = "(" typeName ")" cast
    if (equal(Tok, "(") && isTypename(Tok->Next)) {
        Token *Start = Tok;
        Type *Ty = typename(&Tok, Tok->Next);
        Tok = skip(Tok, ")");

        // 复合字面量
        if (equal(Tok, "{"))
            return unary(Rest, Start);

        // 解析嵌套的类型转换
        Node *Nd = newCast(cast(Rest, Tok), Ty);
        Nd->Tok = Start;
        return Nd;
    }
    // unary
    return unary(Rest, Tok);
}



// 解析一元运算
// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | postfix 
//       | ("++" | "--") unary
//       | "&&" ident
static Node *unary(Token **Rest, Token *Tok) {
    // "+" cast
    if (equal(Tok, "+"))
        return cast(Rest, Tok->Next);
    // "-" cast
    if (equal(Tok, "-"))
        return newUnary(ND_NEG, cast(Rest, Tok->Next), Tok);
    // "*" cast. pointer
    if (equal(Tok, "*")) {
        Node *Nd = cast(Rest, Tok->Next);
        addType(Nd);
        // 如果func是函数，那么*func等价于func
        if (Nd->Ty->Kind == TY_FUNC)
            return Nd;
        return newUnary(ND_DEREF, Nd, Tok);
    }
    // "*" cast. pointer
    if (equal(Tok, "&")) {
        Node *LHS = cast(Rest, Tok->Next);
        addType(LHS);
        // 不能够获取位域的地址
        if (LHS->Kind == ND_MEMBER && LHS->Mem->IsBitfield)
            errorTok(Tok, "cannot take address of bitfield");
        return newUnary(ND_ADDR, LHS, Tok);
    }
    if (equal(Tok, "!"))
        return newUnary(ND_NOT, cast(Rest, Tok->Next), Tok);
    if (equal(Tok, "~"))
        return newUnary(ND_BITNOT, cast(Rest, Tok->Next), Tok);
    // 转换 ++i 为 i+=1;
    if (equal(Tok, "++"))
        return toAssign(
            newAdd(unary(Rest, Tok->Next), newNum(1, Tok), Tok));

    // 转换 +-i 为 i-=1
    // "--" unary
    if (equal(Tok, "--"))
        return toAssign(
            newSub(unary(Rest, Tok->Next), newNum(1, Tok), Tok));

    // GOTO的标签作为值
    if (equal(Tok, "&&")) {
        Node *Nd = newNode(ND_LABEL_VAL, Tok);
        Nd->Label = getIdent(Tok->Next);
        // 将Nd同时存入Gotos，最后用于解析UniqueLabel
        Nd->GotoNext = Gotos;
        Gotos = Nd;
        *Rest = Tok->Next->Next;
        return Nd;
    }

    // primary
    return postfix(Rest, Tok);
}

// 转换 A++ 为 `(typeof A)((A += 1) - 1)`
// Increase Decrease
static Node *newIncDec(Node *Nd, Token *Tok, int Addend) {
    addType(Nd);
    return newCast(
            newAdd(toAssign(newAdd(Nd, newNum(Addend, Tok), Tok)),
            newNum(-Addend, Tok), Tok),
    Nd->Ty);
}


/*
//  essence: convert the [] operator to some pointer dereferrence
//    input = a[5][10]

//    primary = a

//                   Nd
//                   | deref
//                   +
//                 /   \
//                /     \
//               /       \
//              Nd    expr(idx=10)
//              | deref
//              +
//            /   \
//       primary  expr(idx=5)       */

// postfix = "(" typeName ")" "{" initializerList "}"
//         = ident "(" funcArgs ")" postfixTail*
//         | primary postfixTail*
//
// postfixTail = "[" expr "]"
//             | "(" funcArgs ")"
//             | "." ident
//             | "->" ident
//             | "++"
//             | "--"
static Node *postfix(Token **Rest, Token *Tok) {
    // "(" typeName ")" "{" initializerList "}"
    // (struct x){1, 2, 6}; (int)1;
    if (equal(Tok, "(") && isTypename(Tok->Next)) {
        // 复合字面量
        Token *Start = Tok;
        Type *Ty = typename(&Tok, Tok->Next);
        Tok = skip(Tok, ")");
        // top level scope(global variable)
        if (Scp->Next == NULL) {
            Obj *Var = newAnonGVar(Ty);
            GVarInitializer(Rest, Tok, Var);
            return newVarNode(Var, Start);
        }

        Obj *Var = newLVar("", Ty);
        Node *LHS = LVarInitializer(Rest, Tok, Var);
        Node *RHS = newVarNode(Var, Tok);
        return newBinary(ND_COMMA, LHS, RHS, Start);
    }

    // primary
    Node *Nd = primary(&Tok, Tok);

    // ("[" expr "]")*
    while (true) {
        // ident "(" funcArgs ")"
        // 匹配到函数调用
        if (equal(Tok, "(")) {
            Nd = funCall(&Tok, Tok->Next, Nd);
            continue;
        }
        if (equal(Tok, "[")) {
            // x[y] 等价于 *(x+y)
            Token *Start = Tok;
            Node *Idx = expr(&Tok, Tok->Next);
            Tok = skip(Tok, "]");
            Nd = newUnary(ND_DEREF, newAdd(Nd, Idx, Start), Start);
            continue;
        }

        // "." ident
        if (equal(Tok, ".")) {
            Nd = structRef(Nd, Tok->Next);
            Tok = Tok->Next->Next;
            continue;
        }

        // "->" ident
        if (equal(Tok, "->")) {
            // x->y 等价于 (*x).y
            Nd = newUnary(ND_DEREF, Nd, Tok);
            Nd = structRef(Nd, Tok->Next);
            Tok = Tok->Next->Next;
            continue;
        }

        if (equal(Tok, "++")) {
            Nd = newIncDec(Nd, Tok, 1);
            Tok = Tok->Next;
            continue;
        }

        if (equal(Tok, "--")) {
            Nd = newIncDec(Nd, Tok, -1);
            Tok = Tok->Next;
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析函数调用. a helper function used by primary
// funcall = (assign ("," assign)*)? ")"
// the arg `Tok` is an ident
static Node *funCall(Token **Rest, Token *Tok, Node *Fn) {
    addType(Fn);
    Token *FnName = Tok;
    // 检查函数指针
    if (Fn->Ty->Kind != TY_FUNC &&
        (Fn->Ty->Kind != TY_PTR || Fn->Ty->Base->Kind != TY_FUNC))
        errorTok(Fn->Tok, "not a function");

    // 函数名的类型
    Type *Ty = (Fn->Ty->Kind == TY_FUNC) ? Fn->Ty : Fn -> Ty -> Base;
    // 函数形参的类型
    Type *ParamTy = Ty->Params;

    Node Head = {};
    Node *Cur = &Head;
    // expr ("," expr)*
    while (!equal(Tok, ")")) {
        if (Cur != &Head)
            Tok = skip(Tok, ",");
        // expr
        Node *Arg = assign(&Tok, Tok);
        addType(Arg);
        if (ParamTy) {
            // simple arg type check
            if (OptW && !isCompatible(Arg->Ty, ParamTy))
                warnTok(Tok, "type mismatch here. expected \"%s\" but get \"%s\"\n", typeNames[ParamTy->Kind], typeNames[Arg->Ty->Kind]);
            if (ParamTy->Kind != TY_STRUCT && ParamTy->Kind != TY_UNION)
                // 将参数节点的类型进行转换
                // sometimes -W flag will report a type mismatch, but it will be handled perfectly here
                Arg = newCast(Arg, ParamTy);
            // 前进到下一个形参类型
            ParamTy = ParamTy->Next;
        }
        else if (Arg->Ty->Kind == TY_FLOAT) {
            // 若无形参类型，浮点数会被提升为double
            Arg = newCast(Arg, TyDouble);
        }
        // 对参数进行存储
        Cur->Next = Arg;

        Cur = Cur->Next;
        addType(Cur);
    }

    *Rest = skip(Tok, ")");
    Node *Nd = newUnary(ND_FUNCALL, Fn, Tok);
    Nd->Args = Head.Next;
    Nd->FuncType = Ty;
    Nd->Ty = Ty->ReturnTy;

    // 如果函数返回值是结构体，那么调用者需为返回值开辟一块空间
    if (Nd->Ty->Kind == TY_STRUCT || Nd->Ty->Kind == TY_UNION)
        Nd->RetBuffer = newLVar("", Nd->Ty);

    return Nd;
    /*  can't do the return value cast here.
        instead we could do it in codegen,
        because we may meet conflict function declarations,
        eg we declare a fn returning _Bool in one .h header file,
        and import it but as returning int in another file, like
            "extern int fn(...);"
        then we should return int instead of _Bool in that file */
    //return newCast(Nd, Ty->ReturnTy);
}

// abstractDeclarator = pointers ("(" abstractDeclarator ")")? typeSuffix
// note: the ident is not needed, which is difference from declarator
static Type *abstractDeclarator(Token **Rest, Token *Tok, Type *Ty) {
    Ty = pointers(&Tok, Tok, Ty);
    // ("(" abstractDeclarator ")")?
    if (equal(Tok, "(")) {
        Token *Start = Tok;
        Type Dummy = {};
        // 使Tok前进到")"后面的位置
        abstractDeclarator(&Tok, Start->Next, &Dummy);
        Tok = skip(Tok, ")");
        // 获取到括号后面的类型后缀，Ty为解析完的类型，Rest指向分号
        Ty = typeSuffix(Rest, Tok, Ty);
        // 解析Ty整体作为Base去构造，返回Type的值
        return abstractDeclarator(&Tok, Start->Next, Ty);
    }

    // typeSuffix
    return typeSuffix(Rest, Tok, Ty);
}

// typeName = declspec abstractDeclarator
// 获取类型的相关信息
static Type *typename(Token **Rest, Token *Tok) {
    // declspec
    Type *Ty = declspec(&Tok, Tok, NULL);
    // abstractDeclarator
    return abstractDeclarator(Rest, Tok, Ty);
}

// genericSelection = "(" assign "," genericAssoc ("," genericAssoc)* ")"
//
// genericAssoc = typeName ":" assign
//              | "default" ":" assign
// 泛型选择
static Node *genericSelection(Token **Rest, Token *Tok) {
    Token *Start = Tok;
    // "("
    Tok = skip(Tok, "(");

    // assign
    // 泛型控制选择的值
    Node *Ctrl = assign(&Tok, Tok);
    addType(Ctrl);

    // 获取控制选择的值类型
    Type *T1 = Ctrl->Ty;
    // 函数转换为函数指针
    if (T1->Kind == TY_FUNC)
        T1 = pointerTo(T1);
    // 数组转换为数组指针
    else if (T1->Kind == TY_ARRAY)
        T1 = pointerTo(T1->Base);

    // 泛型返回的结果值
    Node *Ret = NULL;

    // "," genericAssoc ("," genericAssoc)*
    while (!consume(Rest, Tok, ")")) {
        Tok = skip(Tok, ",");

        // 默认类型及其对应的值
        // "default" ":" assign
        if (equal(Tok, "default")) {
            Tok = skip(Tok->Next, ":");
            Node *Nd = assign(&Tok, Tok);
            if (!Ret)
                Ret = Nd;
            continue;
        }

        // 各个类型及其对应的值
        // typeName ":" assign
        Type *T2 = typename(&Tok, Tok);
        Tok = skip(Tok, ":");
        Node *Nd = assign(&Tok, Tok);
        // 判断类型是否与控制选择的值类型相同
        if (isCompatible(T1, T2))
            Ret = Nd;
    }

    if (!Ret)
        errorTok(Start, "controlling expression type not compatible with"
                        " any generic association type");
    // 返回泛型最后的值
    return Ret;
}


// 解析括号、数字
// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" unary
//         | "__builtin_types_compatible_p" "(" typeName, typeName, ")"
//         | "_Generic" genericSelection
//         | ident
//         | str
//         | num
//         | "_Alignof" "(" typeName ")"
//         | "_Alignof" unary
// FuncArgs = "(" (expr ("," expr)*)? ")"
static Node *primary(Token **Rest, Token *Tok) {
    // this needs to be parsed before "(" expr ")", otherwise the "(" will be consumed
    // "(" "{" stmt+ "}" ")"
    Token *Start = Tok;
    if (equal(Tok, "(") && equal(Tok->Next, "{")) {
        // This is a GNU statement expresssion.
        Node *Nd = newNode(ND_STMT_EXPR, Tok);
        Nd->Body = compoundStmt(&Tok, Tok->Next)->Body;
        *Rest = skip(Tok, ")");
        return Nd;
    }

    // "(" expr ")"
    if (equal(Tok, "(")) {
        Node *Nd = expr(&Tok, Tok->Next);
        *Rest = skip(Tok, ")");     // ?
        return Nd;
    }

    // "sizeof" "(" typeName ")"
    if (equal(Tok, "sizeof") && equal(Tok->Next, "(") && isTypename(Tok->Next->Next)) {
        Type *Ty = typename(&Tok, Tok->Next->Next);
        *Rest = skip(Tok, ")");
        // sizeof 可变长度数组的大小
        if (Ty->Kind == TY_VLA) {
            // 对于变量的sizeof操作
            if (Ty->VLASize)
                return newVarNode(Ty->VLASize, Tok);
        
            // 对于VLA类型的sizeof操作
            // 获取VLA类型的VLASize
            Node *LHS = computeVLASize(Ty, Tok);
            Node *RHS = newVarNode(Ty->VLASize, Tok);
            return newBinary(ND_COMMA, LHS, RHS, Tok);
        }
        return newULong(Ty->Size, Start);
    }

    // "sizeof" unary
    if (equal(Tok, "sizeof")) {
        Node *Nd = unary(Rest, Tok->Next);
        addType(Nd);
        // sizeof 可变长度数组的大小
        if (Nd->Ty->Kind == TY_VLA)
            return newVarNode(Nd->Ty->VLASize, Tok);
        return newULong(Nd->Ty->Size, Tok);
    }

    // num
    if (Tok->Kind == TK_NUM) {
        Node *Nd;
        if (isFloNum(Tok->Ty)) {
            // 浮点数节点
            Nd = newNode(ND_NUM, Tok);
            Nd->FVal = Tok->FVal;
        } else {
            // 整型节点
            Nd = newNum(Tok->Val, Tok);
        }
        //Node *Nd = newNum(Tok->Val, Tok);
        *Rest = Tok->Next;
        Nd -> Ty = Tok->Ty;
        return Nd;
    }

    // "_Alignof" "(" typeName ")"
    // 读取类型的对齐值
    if (equal(Tok, "_Alignof") && equal(Tok->Next, "(") && isTypename(Tok->Next->Next)) {
        Type *Ty = typename(&Tok, Tok->Next->Next);
        *Rest = skip(Tok, ")");
        return newULong(Ty->Align, Tok);
    }

    // "_Alignof" unary
    // 读取变量的对齐值
    if (equal(Tok, "_Alignof")) {
        Node *Nd = unary(Rest, Tok->Next);
        addType(Nd);
        return newULong(Nd->Ty->Align, Tok);
    }

    // "__builtin_types_compatible_p" "(" typeName, typeName, ")"
    // 匹配内建的类型兼容性函数
    if (equal(Tok, "__builtin_types_compatible_p")) {
        Tok = skip(Tok->Next, "(");
        // 类型1
        Type *T1 = typename(&Tok, Tok);
        Tok = skip(Tok, ",");
        // 类型2
        Type *T2 = typename(&Tok, Tok);
        *Rest = skip(Tok, ")");
        // 返回二者兼容检查函数的结果
        return newNum(isCompatible(T1, T2), Start);
    }

    // 原子比较交换
    if (equal(Tok, "__builtin_compare_and_swap")) {
        Node *Nd = newNode(ND_CAS, Tok);
        Tok = skip(Tok->Next, "(");
        Nd->CasAddr = assign(&Tok, Tok);
        Tok = skip(Tok, ",");
        Nd->CasOld = assign(&Tok, Tok);
        Tok = skip(Tok, ",");
        Nd->CasNew = assign(&Tok, Tok);
        *Rest = skip(Tok, ")");
        return Nd;
    }

    // 原子交换
    if (equal(Tok, "__builtin_atomic_exchange")) {
        Node *Nd = newNode(ND_EXCH, Tok);
        Tok = skip(Tok->Next, "(");
        Nd->LHS = assign(&Tok, Tok);
        Tok = skip(Tok, ",");
        Nd->RHS = assign(&Tok, Tok);
        *Rest = skip(Tok, ")");
        return Nd;
    }

    // "_Generic" genericSelection
    // 进入到泛型的解析
    if (equal(Tok, "_Generic"))
        return genericSelection(Rest, Tok->Next);

    // ident
    if (Tok->Kind == TK_IDENT) {
        VarScope *S = findVar(Tok);

        // 用于static inline函数
        // 变量存在且为函数
        if (S && S->Var && S->Var->Ty->Kind == TY_FUNC) {
        if (CurrentFn)
            // 如果函数体内存在其他函数，则记录引用的其他函数
            strArrayPush(&CurrentFn->Refs, S->Var->Name);
        else
            // 标记为根函数
            S->Var->IsRoot = true;
        }

        *Rest = Tok->Next;
        if (S) {
            // 是否为变量
            if (S->Var)
                return newVarNode(S->Var, Tok);
            // 否则为枚举常量
            if (S->EnumTy)
                return newNum(S->EnumVal, Tok);
        }

        if(equal(Tok->Next, "(")){
            errorTok(Tok, "implicit declaration of a function");
            errorTok(Tok, "undefined variable");
        }
    }

    // str, recognized in tokenize
    if (Tok->Kind == TK_STR) {
        Obj *Var = newStringLiteral(Tok->Str, Tok->Ty);     // use Tok->Ty!
        *Rest = Tok->Next;
        return newVarNode(Var, Tok);
    }
    errorTok(Tok, "expected an expression");
    return NULL;
}

// 区分 函数还是全局变量
static bool isFunction(Token *Tok) {
    if (equal(Tok, ";"))
        return false;

    // 虚设变量，用于调用declarator
    Type Dummy = {};
    Type *Ty = declarator(&Tok, Tok, &Dummy);
    return Ty->Kind == TY_FUNC;
}

// 删除冗余的试探性定义
static void scanGlobals(void) {
    // 新的全局变量的链表
    Obj Head;
    Obj *Cur = &Head;

    // 遍历全局变量，删除冗余的试探性定义
    for (Obj *Var = Globals; Var; Var = Var->Next) {
        // 不是试探性定义，直接加入到新链表中
        if (!Var->IsTentative) {
            Cur = Cur->Next = Var;
            continue;
        }

        // 查找其他具有定义的同名标志符
        // 从头遍历
        Obj *Var2 = Globals;
        for (; Var2; Var2 = Var2->Next)
            // 判断 不是同一个变量，变量具有定义，二者同名
            if (Var != Var2 && Var2->IsDefinition && !strcmp(Var->Name, Var2->Name))
                break;

        // 如果Var2为空，说明需要生成代码，加入到新链表中
        // 如果Var2不为空，说明存在定义，那么就不需要生成试探性定义
        if (!Var2)
            Cur = Cur->Next = Var;
    }

    // 替换为新的全局变量链表
    Cur->Next = NULL;
    Globals = Head.Next;
}

// 语法解析入口函数
// program = ( typedef | functionDefinition* | global-variable)*
Obj *parse(Token *Tok) {
    Globals = NULL;
    // 声明内建函数
    declareBuiltinFunctions();
    // fn or gv?
    // int *** fn(){},  int**** a;
    while (Tok->Kind != TK_EOF) {
        VarAttr Attr = {};
        // at first I just use "VarAttr Attr;"
        // but then the struct's member got random init value...
        // use = {} to clear the member...
        Type *BaseTy = declspec(&Tok, Tok, &Attr);
        if(Attr.IsTypedef){
            Tok = parseTypedef(Tok, BaseTy);
            continue;
        }
        if (isFunction(Tok))
            Tok = function(Tok, BaseTy, &Attr);
        else
            Tok = globalVariable(Tok, BaseTy, &Attr);
    }

    // 遍历所有的函数
    for (Obj *Var = Globals; Var; Var = Var->Next)
        // 如果为根函数，则设置为存活状态
        if (Var->IsRoot)
            markLive(Var);

    scanGlobals();
    return Globals;
}