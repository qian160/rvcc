#include "rvcc.h"

//
// some helper functions
//

// 记录栈深度
static int Depth;

// 用于函数参数的寄存器们
static char *ArgReg[] = {"a0", "a1", "a2", "a3", "a4", "a5"};

static Obj *CurrentFn;

// 输出文件
static FILE *OutputFile;

// 代码段计数
static int count(void) {
    static int I = 1;
    return I++;
}

// 对齐到Align的整数倍
// (0,Align]返回Align
static int alignTo(int N, int Align) {
    return (N + Align - 1) / Align * Align;
}

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

// 加载a0(为一个地址)指向的值
static void load(Type *Ty) {
    // to load a value, for normal types we need to get its address
    // in stack and load from that address. but for pointer type we just need
    // to get its address. the true "load" op is done by later deref
    if (Ty->Kind == TY_ARRAY)
        return;
    switch (Ty->Size)
    {
        case 1:
            println("  lb a0, 0(a0)");
            break;
        case 2:
            println("  lh a0, 0(a0)");
            break;
        case 4:
            println("  lw a0, 0(a0)");
            break;
        case 8:
            println("  ld a0, 0(a0)");
            break;
        default:
            error("wtf");
    }
}

// 将a0存入栈顶值(为一个地址)
static void store(Type *Ty) {
    pop("a1");
    switch (Ty->Size)
    {
        case 1:
            println("  sb a0, 0(a1)");
            break;
        case 2:
            println("  sh a0, 0(a1)");
            break;
        case 4:
            println("  sw a0, 0(a1)");
            break;
        case 8:
            println("  sd a0, 0(a1)");
            break;
        default:
            error("wtf");
    }
};

//
// codeGen
//

static void genExpr(Node *Nd);
// 计算给定节点的绝对地址, 并打印
// 如果报错，说明节点不在内存中
static void genAddr(Node *Nd) {
    switch (Nd->Kind){
        // 变量
        case ND_VAR:
            if (Nd->Var->IsLocal) {
                // 局部变量的偏移量是相对于fp的, 栈内
                println("  addi a0, fp, %d", Nd->Var->Offset);
            } else {
                // 获取全局变量的地址
                println("  la a0, %s", Nd->Var->Name);
            }
            return;
        // 解引用*
        case ND_DEREF:
            genExpr(Nd -> LHS);
            return;
        // 逗号
        case ND_COMMA:
            genExpr(Nd->LHS);
            genAddr(Nd->RHS);
            return;

        default:
            error("%s: not an lvalue", strndup(Nd->Tok->Loc, Nd->Tok->Len));
            break;
    }
}

// 根据变量的链表计算出偏移量
// 其实是为每个变量分配地址
static void assignLVarOffsets(Obj *Prog) {
    // 为每个函数计算其变量所用的栈空间
    for (Obj *Fn = Prog; Fn; Fn = Fn->Next) {
        int Offset = 0;
        if(Fn->Ty->Kind != TY_FUNC)
            continue;
        println(" # local variables of Fn %s:", Fn->Name);
        // 读取所有变量
        for (Obj *Var = Fn->Locals; Var; Var = Var->Next) {
            // the offset here is relevent to fp, which is at top of stack
            // 每个变量分配空间
            Offset += Var->Ty->Size;
            // 为每个变量赋一个偏移量，或者说是栈中地址
            Var->Offset = -Offset;
            println(" # %s, offset = %d", Var->Name, Var->Offset);
        }
        // 将栈对齐到16字节
        Fn->StackSize = alignTo(Offset, 16);
    }
}

static void genStmt(Node *Nd);

// sementics: print the asm from an ast whose root node is `Nd`
// steps: for each node,
// 1. if it is a leaf node, then directly print the answer and return
// 2. otherwise:
//      get the value of its rhs sub-tree first, 
//      save that answer to the stack. 
//      then get the lhs sub-tree's value.
//      now we have both sub-tree's value, 
//      and how to deal with these two values depends on current root node
// 生成表达式. after expr is generated its value will be put to a0
static void genExpr(Node *Nd) {
    // .loc 文件编号 行号. debug use
    println("  .loc 1 %d", Nd->Tok->LineNo);

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
        // 变量. note: array also has VAR type
        case ND_VAR:
            // 计算出变量的地址, 存入a0
            genAddr(Nd);
            // load a value from the generated address
            load(Nd->Ty);
            return;
        // 赋值
        case ND_ASSIGN:
            // 左部是左值，保存值到的地址
            genAddr(Nd->LHS);
            push();
            // 右部是右值，为表达式的值
            genExpr(Nd->RHS);
            store(Nd->Ty);
            return;
        // 解引用. *var
        case ND_DEREF:
            // get the address first
            genExpr(Nd->LHS);
            load(Nd->Ty);
            return;
        // 取地址 &var
        case ND_ADDR:
            genAddr(Nd->LHS);
            return;
        // 函数调用
        case ND_FUNCALL:{
            // 记录参数个数
            int NArgs = 0;
            // 计算所有参数的值，正向压栈
            for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
                genExpr(Arg);
                push();
                NArgs++;
            }
            // 反向弹栈，a0->参数1，a1->参数2...
            for (int i = NArgs - 1; i >= 0; i--)
                pop(ArgReg[i]);
            // 调用函数
            // the contents of the function is generated by test.sh, not by rvccl
            println("  call %s", Nd->FuncName);
            return;
        }
        // 语句表达式
        case ND_STMT_EXPR:
            for (Node *N = Nd->Body; N; N = N->Next)
                genStmt(N);
            return;
        // 逗号
        case ND_COMMA:
            genExpr(Nd->LHS);
            genExpr(Nd->RHS);
            return;

        default:
            break;
    }
    // EXPR_STMT
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

    error("%s: invalid expression", Nd -> Tok -> Loc);
}

// 生成语句
static void genStmt(Node *Nd) {
    // .loc 文件编号 行号, debug use
    println("  .loc 1 %d", Nd->Tok->LineNo);

    switch (Nd->Kind){
        // 生成代码块，遍历代码块的语句链表
        case ND_BLOCK:
            for (Node *N = Nd->Body; N; N = N->Next)
                genStmt(N);
            return;
        case ND_EXPR_STMT:
            // node of type EXPR_STMT is unary
            genExpr(Nd->LHS);
            return;
        case ND_RETURN:
            genExpr(Nd->LHS);
            // 无条件跳转语句，跳转到.L.return.%s段
            // j offset是 jal x0, offset的别名指令
            println("  j .L.return.%s", CurrentFn->Name);
            return;
        // 生成if语句
        case ND_IF: {
            /*
            if (!cond)  // judged by beqz inst
                j .L.else.%d
                ... (then)
                ...
            .L.else.%d:
                ... (else)
                ...
            .L.end.%d:
            */
            // 代码段计数
            int C = count();
            // 生成条件内语句
            genExpr(Nd->Cond);
            // 判断结果是否为0，为0(false)则跳转到else标签
            println("  beqz a0, .L.else.%d", C);
            // 生成符合条件后的语句
            genStmt(Nd->Then);
            // 执行完后跳转到if语句后面的语句
            println("  j .L.end.%d", C);
            // else代码块，else可能为空，故输出标签
            println(".L.else.%d:", C);
            // 生成不符合条件后的语句
            if (Nd->Els)
                genStmt(Nd->Els);
            // 结束if语句，继续执行后面的语句
            println(".L.end.%d:", C);
            return;
        }
        // 生成for 或 "while" 循环语句 
        // "for" "(" exprStmt expr? ";" expr? ")" stmt
/*
            ... (init)                  // optional
            ... (cond)
    +-->.L.begin.%d:                    // loop begins
    |       ... (body)            
    |       ... (cond)            
    |      (beqz a0, .L.end.%d)---+     // loop condition. optional
    +------(j .L.begin.%d)        |  
        .L.end.%d:  <-------------+
        
    note: when entering the loop body for the `1st time`,
    we will insert an cond and branch.
    this works as the init cond check.
    if not satisfied, we will bot enter the loop body
        */
        case ND_FOR: {
            // 代码段计数
            int C = count();
            // 生成初始化语句
            if(Nd->Init){
                genStmt(Nd->Init);
            }
            // 输出循环头部标签
            println(".L.begin.%d:", C);
            // 处理循环条件语句
            if (Nd->Cond) {
                // 生成条件循环语句
                genExpr(Nd->Cond);
                // 判断结果是否为0，为0则跳转到结束部分
                println("  beqz a0, .L.end.%d", C);
            }
            // 生成循环体语句
            genStmt(Nd->Then);
            // 处理循环递增语句
            if (Nd -> Inc){
                println("\n# Inc语句%d", C);
                // 生成循环递增语句
                genExpr(Nd->Inc);
            }
            // 跳转到循环头部
            println("  j .L.begin.%d", C);
            // 输出循环尾部标签
            println(".L.end.%d:", C);
            return;
        }
        default:
            error("%s: invalid statement", Nd->Tok->Loc);
    }

}
    // 栈布局
    //-------------------------------// sp
    //              ra
    //-------------------------------// ra = sp-8
    //              fp
    //-------------------------------// fp = sp-16
    //             变量
    //-------------------------------// sp = sp-16-StackSize
    //           表达式计算
    //-------------------------------//

static void emitData(Obj *Prog) {
    for (Obj *Var = Prog; Var; Var = Var->Next) {
        if (Var->Ty->Kind == TY_FUNC)
            continue;

        println("  .data");
        if (Var -> InitData){
            println("%s:", Var->Name);
            for(int i = 0; i < Var->Ty->Size; i++){
                char C = Var->InitData[i];
                if (isprint(C))
                    println("  .byte %d\t# ：%c", C, C);
                else
                    println("  .byte %d", C);
            }
        }
        else{
            println("  .globl %s", Var->Name);
            println("%s:", Var->Name);
            println("  # 全局变量零填充%d位", Var->Ty->Size);
            println("  .zero %d", Var->Ty->Size);
        }
    }
}


// 代码生成入口函数，包含代码块的基础信息
void emitText(Obj *Prog) {
    // 为每个函数单独生成代码
    for (Obj *Fn = Prog; Fn; Fn = Fn->Next) {
        if (Fn->Ty->Kind != TY_FUNC)
            continue;

        println("  .globl %s", Fn->Name);

        println("  .text");
        println("# =====%s段开始===============", Fn->Name);
        println("%s:", Fn->Name);
        CurrentFn = Fn;

        // Prologue, 前言
        // 将ra寄存器压栈,保存ra的值
        println("  addi sp, sp, -16");
        println("  sd ra, 8(sp)");
        // 将fp压入栈中，保存fp的值
        println("  sd fp, 0(sp)");
        // 将sp写入fp
        println("  mv fp, sp");

        // 偏移量为实际变量所用的栈大小
        println("  addi sp, sp, -%d", Fn->StackSize);

        // map the actual params to formal params
        // this needs to be done before entering the fn body
        // then in the fn body we can use the formal params in stack
        int I = 0;
        for (Obj *Var = Fn->Params; Var; Var = Var->Next)
        {
            if (Var->Ty->Size == 1)
                println("  sb %s, %d(fp)", ArgReg[I++], Var->Offset);
            else
                println("  sd %s, %d(fp)", ArgReg[I++], Var->Offset);
        }
        // 生成语句链表的代码
        println("# =====%s段主体===============", Fn->Name);
        genStmt(Fn->Body);
        Assert(Depth == 0, "depth = %d", Depth);

        // Epilogue，后语
        // 输出return段标签
        println("# =====%s段结束===============", Fn->Name);
        println(".L.return.%s:", Fn->Name);
        // 将fp的值改写回sp
        println("  mv sp, fp");
        // 将最早fp保存的值弹栈，恢复fp。
        println("  ld fp, 0(sp)");
        // 将ra寄存器弹栈,恢复ra的值
        println("  ld ra, 8(sp)");
        println("  addi sp, sp, 16");
        // 返回
        println("  ret");
    }
}

// 代码生成入口函数，包含代码块的基础信息
void codegen(Obj * Prog, FILE *Out) {
    OutputFile = Out;
    // 为本地变量计算偏移量, 以及决定函数最终的栈大小
    assignLVarOffsets(Prog);
    // 生成数据
    emitData(Prog);
    // 生成代码
    emitText(Prog);
}