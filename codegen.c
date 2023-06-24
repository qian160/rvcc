#include "rvcc.h"

//
// some helper functions
//

// 记录栈深度
static int Depth;

static Obj *CurrentFn;

// 输出文件
static FILE *OutputFile;

/*
void println(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vfprintf(OutputFile, fmt, va);
    fprintf(OutputFile, "\n");
    va_end(va);
}
*/

// 代码段计数
static int count(void) {
    static int I = 1;
    return I++;
}


// 压栈，将结果临时(a0)压入栈中备用.
static void push(void) {
    println("  addi sp, sp, -8");
    println("  sd a0, 0(sp)");
    Depth++;
}

// 弹栈，将sp指向的地址的值，弹出到a1
static void pop(int Reg) {
    println("  ld a%d, 0(sp)", Reg);
    println("  addi sp, sp, 8");
    Depth--;
}

// 对于浮点类型进行压栈
static void pushF(void) {
    println("  addi sp, sp, -8");
    println("  fsd fa0, 0(sp)");
    Depth++;
}

// 对于浮点类型进行弹栈
static void popF(int Reg) {
    println("  fld fa%d, 0(sp)", Reg);
    println("  addi sp, sp, 8");
    Depth--;
}

static void genExpr(Node *Nd);
static void genStmt(Node *Nd);

// 将函数实参计算后压入栈中
static void pushArgs(Node *Args) {
    // 参数为空直接返回
    if (!Args)
        return;

    // 递归到最后一个实参进行
    pushArgs(Args->Next);

    println("\n  # ↓对%s表达式进行计算，然后压栈↓",
            isFloNum(Args->Ty) ? "浮点" : "整型");
    // 计算出表达式
    genExpr(Args);
    // 根据表达式结果的类型进行压栈
    if (isFloNum(Args->Ty)) {
        pushF();
    } else {
        push();
    }
    println("  # ↑结束压栈↑");
}

// stage2 can't deal with "inline" now

// ImmI: 12 bits. [-2048, 2047]
// addi, load
static /*inline*/ bool isLegalImmI(int i){
    return (i >= -0x800 && i <= 0x7ff);
}

// ImmS: 12 bits. also [-2048, 2047], just bits' location different
// store
static /*inline*/ bool isLegalImmS(int i){
    return isLegalImmI(i);
}

// 将整形寄存器的值存入栈中
static void storeGeneral(int Reg, int Offset, int Size) {
    // 将%s寄存器的值存入%d(fp)的栈地址
    switch (Size) {
        case 1:
            if(isLegalImmS(Offset))
                println("  sb a%d, %d(fp)", Reg, Offset);
            else {
                println("  li t0, %d", Offset);
                println("  add t0, fp, t0");
                println("  sb a%d, 0(t0)", Reg);
            }
            return;
        case 2:
            if(isLegalImmS(Offset))
                println("  sh a%d, %d(fp)", Reg, Offset);
            else {
                println("  li t0, %d", Offset);
                println("  add t0, fp, t0");
                println("  sh a%d, 0(t0)", Reg);
            }
            return;
        case 4:
            if(isLegalImmS(Offset))
                println("  sw a%d, %d(fp)", Reg, Offset);
            else {
                println("  li t0, %d", Offset);
                println("  add t0, fp, t0");
                println("  sw a%d, 0(t0)", Reg);
            }
            return;
        case 8:
            if(isLegalImmS(Offset))
                println("  sd a%d, %d(fp)", Reg, Offset);
            else {
                println("  li t0, %d", Offset);
                println("  add t0, fp, t0");
                println("  sd a%d, 0(t0)", Reg);
            }
            return;
        default:
            error("unreachable");
    }
}

// 将浮点寄存器的值存入栈中
static void storeFloat(int Reg, int Offset, int Sz) {
    if(isLegalImmS(Offset)){
        switch (Sz) {
            case 4:
                println("  fsw fa%d, %d(fp)", Reg, Offset);
                return;
            case 8:
                println("  fsd fa%d, %d(fp)", Reg, Offset);
                return;
            default:
                error("unreachable");
        }
    }

    println("  li t0, %d", Offset);
    println("  add t0, fp, t0");

    switch (Sz) {
        case 4:
            println("  fsw fa%d, 0(t0)", Reg);
            return;
        case 8:
            println("  fsd fa%d, 0(t0)", Reg);
            return;
        default:
            error("unreachable");
    }
}


// 加载a0(为一个地址)指向的值
static void load(Type *Ty) {
    // load will choose sext or zext according to the suffix
    // not necessary for double word, since it needs no extension
    char *Suffix = Ty->IsUnsigned ? "u" : "";

    switch (Ty->Kind) {
    case TY_ARRAY:
    case TY_STRUCT:
    case TY_UNION:
    case TY_FUNC:
        return;
    case TY_FLOAT:
        // 访问a0中存放的地址，取得的值存入fa0
        println("  flw fa0, 0(a0)");
        return;
    case TY_DOUBLE:
        // 访问a0中存放的地址，取得的值存入fa0"
        println("  fld fa0, 0(a0)");
        return;
    default:
        break;
    }

    switch (Ty->Size)
    {
        case 1:
            println("  lb%s a0, 0(a0)", Suffix);
            break;
        case 2:
            println("  lh%s a0, 0(a0)", Suffix);
            break;
        case 4:
            println("  lw%s a0, 0(a0)", Suffix);
            break;
        case 8:
            println("  ld a0, 0(a0)");  
            break;
        default:
            error("wtf");
    }
}

// 将a0存入栈顶值(为一个地址). used in assign, and lhs value's address was pushed to stack already
static void store(Type *Ty) {
    pop(1);
    switch(Ty->Kind){
        case TY_STRUCT:
        case TY_UNION:{
            // copy all the bytes from one struct to another
            int I = 0;
            while (I + 8 <= Ty->Size) {
                println("  ld t1, %d(a0)", I);
                println("  sd t1, %d(a1)", I);
                I += 8;
            }
            while (I + 4 <= Ty->Size) {
                println("  lw t1, %d(a0)", I);
                println("  sw t1, %d(a1)", I);
                I += 4;
            }
            while (I + 2 <= Ty->Size) {
                println("  lh t1, %d(a0)", I);
                println("  sh t1, %d(a1)", I);
                I += 2;
            }
            while (I + 1 <= Ty->Size) {
                println("  lb t1, %d(a0)", I);
                println("  sb t1, %d(a1)", I);
                I += 1;
            }
            return;
        }
        case TY_FLOAT:
            // 将fa0的值，写入到a1中存放的地址
            println("  fsw fa0, 0(a1)");
            return;
        case TY_DOUBLE:
            // 将fa0的值，写入到a1中存放的地址
            println("  fsd fa0, 0(a1)");
            return;
        default:
            break;
    }

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
}

// 与0进行比较，不等于0则置1
// call this before a cond branch to deal with float values
static void notZero(Type *Ty) {
    switch (Ty->Kind) {
        case TY_FLOAT:
            println("  fmv.s.x fa1, zero");
            println("  feq.s a0, fa0, fa1");
            println("  xori a0, a0, 1");
            return;
        case TY_DOUBLE:
            println("  fmv.d.x fa1, zero");
            println("  feq.d a0, fa0, fa1");
            println("  xori a0, a0, 1");
            return;
        default:
            return;
    }
}


// 类型枚举
// note: don't modify their order. these are used as index in castTable
enum { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64 };

// 获取类型对应的枚举值
static int getTypeId(Type *Ty) {
    switch (Ty->Kind) {
    case TY_CHAR:
        return Ty->IsUnsigned ? U8 : I8;
    case TY_SHORT:
        return Ty->IsUnsigned ? U16 : I16;
    case TY_INT:
        return Ty->IsUnsigned ? U32 : I32;
    case TY_LONG:
        return Ty->IsUnsigned ? U64 : I64;
    case TY_FLOAT:
        return F32;
    case TY_DOUBLE:
        return F64;
    default:
        return U64;
    }
}

// 类型映射表
static char i64i8[] =   "  # 转换为i8类型\n"
                        "  slli a0, a0, 56\n"
                        "  srai a0, a0, 56";

static char i64i16[] =  "  # 转换为i16类型\n"
                        "  slli a0, a0, 48\n"
                        "  srai a0, a0, 48";

static char i64i32[] =  "  # 转换为i32类型\n"
                        "  slli a0, a0, 32\n"
                        "  srai a0, a0, 32";


static char i64u8[] =   "  # 转换为u8类型\n"
                        "  slli a0, a0, 56\n"
                        "  srli a0, a0, 56";
static char i64u16[] =  "  # 转换为u16类型\n"
                        "  slli a0, a0, 48\n"
                        "  srli a0, a0, 48";
static char i64u32[] =  "  # 转换为u32类型\n"
                        "  slli a0, a0, 32\n"
                        "  srli a0, a0, 32";

static char u32i64[] =  "  # u32转换为i64类型\n"
                        "  slli a0, a0, 32\n"
                        "  srli a0, a0, 32";

static char i32f32[] =  "  # i32转换为f32类型\n"
                        "  fcvt.s.w fa0, a0";
static char i32f64[] =  "  # i32转换为f64类型\n"
                        "  fcvt.d.w fa0, a0";

static char i64f32[] =  "  # i64转换为f32类型\n"
                        "  fcvt.s.l fa0, a0";
static char i64f64[] =  "  # i64转换为f64类型\n"
                        "  fcvt.d.l fa0, a0";

static char u32f32[] =  "  # u32转换为f32类型\n"
                        "  fcvt.s.wu fa0, a0";
static char u32f64[] =  "  # u32转换为f64类型\n"
                        "  fcvt.d.wu fa0, a0";

static char u64f32[] =  "  # u64转换为f32类型\n"
                        "  fcvt.s.lu fa0, a0";
static char u64f64[] =  "  # u64转换为f64类型\n"
                        "  fcvt.d.lu fa0, a0";

// 单精度浮点数转换为整型
static char f32i8[] =   "  # f32转换为i8类型\n"
                        "  fcvt.w.s a0, fa0, rtz\n"
                        "  slli a0, a0, 56\n"
                        "  srai a0, a0, 56";
static char f32i16[] =  "  # f32转换为i16类型\n"
                        "  fcvt.w.s a0, fa0, rtz\n"
                        "  slli a0, a0, 48\n"
                        "  srai a0, a0, 48";
static char f32i32[] =  "  # f32转换为i32类型\n"
                        "  fcvt.w.s a0, fa0, rtz\n"
                        "  slli a0, a0, 32\n"
                        "  srai a0, a0, 32";
static char f32i64[] =  "  # f32转换为i64类型\n"
                        "  fcvt.l.s a0, fa0, rtz";

static char f32u8[] =   "  # f32转换为u8类型\n"
                        "  fcvt.wu.s a0, fa0, rtz\n"
                        "  slli a0, a0, 56\n"
                        "  srli a0, a0, 56";
static char f32u16[] =  "  # f32转换为u16类型\n"
                        "  fcvt.wu.s a0, fa0, rtz\n"
                        "  slli a0, a0, 48\n"
                        "  srli a0, a0, 48\n";
static char f32u32[] =  "  # f32转换为u32类型\n"
                        "  fcvt.wu.s a0, fa0, rtz\n"
                        "  slli a0, a0, 32\n"
                        "  srai a0, a0, 32";
static char f32u64[] =  "  # f32转换为u64类型\n"
                        "  fcvt.lu.s a0, fa0, rtz";

static char f32f64[] =  "  # f32转换为f64类型\n"
                        "  fcvt.d.s fa0, fa0";

static char f64i8[] =   "  # f64转换为i8类型\n"
                        "  fcvt.w.d a0, fa0, rtz\n"
                        "  slli a0, a0, 56\n"
                        "  srai a0, a0, 56";
static char f64i16[] =  "  # f64转换为i16类型\n"
                        "  fcvt.w.d a0, fa0, rtz\n"
                        "  slli a0, a0, 48\n"
                        "  srai a0, a0, 48";
static char f64i32[] =  "  # f64转换为i32类型\n"
                        "  fcvt.w.d a0, fa0, rtz\n"
                        "  slli a0, a0, 32\n"
                        "  srai a0, a0, 32";
static char f64i64[] =  "  # f64转换为i64类型\n"
                        "  fcvt.l.d a0, fa0, rtz";

static char f64u8[] =   "  # f64转换为u8类型\n"
                        "  fcvt.wu.d a0, fa0, rtz\n"
                        "  slli a0, a0, 56\n"
                        "  srli a0, a0, 56";
static char f64u16[] =  "  # f64转换为u16类型\n"
                        "  fcvt.wu.d a0, fa0, rtz\n"
                        "  slli a0, a0, 48\n"
                        "  srli a0, a0, 48";
static char f64u32[] =  "  # f64转换为u32类型\n"
                        "  fcvt.wu.d a0, fa0, rtz\n"
                        "  slli a0, a0, 32\n"
                        "  srai a0, a0, 32";
static char f64u64[] =  "  # f64转换为u64类型\n"
                        "  fcvt.lu.d a0, fa0, rtz";

static char f64f32[] =  "  # f64转换为f32类型\n"
                        "  fcvt.s.d fa0, fa0";



// 所有类型转换表
static char *castTable[11][11] = {
    // 被映射到
    // {i8,  i16,     i32,     i64,     u8,     u16,     u32,     u64,     f32,     f64}
    {NULL,   NULL,    NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i8转换
    {i64i8,  NULL,    NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i16转换
    {i64i8,  i64i16,  NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i32转换
    {i64i8,  i64i16,  i64i32,  NULL,    i64u8,  i64u16,  i64u32,  NULL,    i64f32,  i64f64}, // 从i64转换

    {i64i8,  NULL,    NULL,    NULL,    NULL,   NULL,    NULL,    NULL,    u32f32,  u32f64}, // 从u8转换
    {i64i8,  i64i16,  NULL,    NULL,    i64u8,  NULL,    NULL,    NULL,    u32f32,  u32f64}, // 从u16转换
    {i64i8,  i64i16,  i64i32,  u32i64,  i64u8,  i64u16,  NULL,    u32i64,  u32f32,  u32f64}, // 从u32转换
    {i64i8,  i64i16,  i64i32,  NULL,    i64u8,  i64u16,  i64u32,  NULL,    u64f32,  u64f64}, // 从u64转换

    {f32i8,  f32i16,  f32i32,  f32i64,  f32u8,  f32u16,  f32u32,  f32u64,  NULL,    f32f64}, // 从f32转换
    {f64i8,  f64i16,  f64i32,  f64i64,  f64u8,  f64u16,  f64u32,  f64u64,  f64f32,  NULL},   // 从f64转换
};


// 类型转换
static void cast(Type *From, Type *To) {
    if (To->Kind == TY_VOID)
        return;
    if (To->Kind == TY_BOOL) {
        notZero(From);
        println("  snez a0, a0");
        return;
    }

    // 获取类型的枚举值
    int T1 = getTypeId(From);
    int T2 = getTypeId(To);
    if (castTable[T1][T2])
        println("%s", castTable[T1][T2]);
}


//
// codeGen
//

// 计算给定节点的绝对地址, 并打印
// 如果报错，说明节点不在内存中
static void genAddr(Node *Nd) {
    switch (Nd->Kind){
        // 变量
        case ND_VAR:
            // 局部变量的偏移量是相对于fp的, 栈内
            if (Nd->Var->IsLocal) {
                // li is pseudo inst for sequence of lui/addi, which
                // can represent an arbitrary 32-bit integer
                // which can present larger range than single addi
                int Offset = Nd->Var->Offset;
                if(isLegalImmI(Offset)){
                    println("  addi a0, fp, %d", Offset);
                }
                else{
                    println("  li t0, %d", Offset);
                    println("  add a0, fp, t0");
                }
            }
            else {
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
        // 结构体成员
        case ND_MEMBER:
            // base addr = a0, offset = t1
            genAddr(Nd->LHS);   // that struct variable's address
            // 计算成员变量的地址偏移量
            println("  li t0, %d", Nd->Mem->Offset);
            println("  add a0, a0, t0");
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
        // 读取所有变量
        for (Obj *Var = Fn->Locals; Var; Var = Var->Next) {
            // the offset here is relevent to fp, which is at top of stack
            // 每个变量分配空间
            Offset += Var->Ty->Size;
            Offset = alignTo(Offset, Var->Align);
            // 为每个变量赋一个偏移量，或者说是栈中地址
            Var->Offset = -Offset;
//            println(" # %s, offset = %d", Var->Name, Var->Offset);
        }
        // 将栈对齐到16字节
        Fn->StackSize = alignTo(Offset, 16);
    }
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
// 生成表达式. after expr is generated its value will be put to a0
static void genExpr(Node *Nd) {
    if(!Nd) return;
    // .loc 文件编号 行号. debug use
    println("  .loc %d %d", Nd->Tok->File->FileNo, Nd->Tok->LineNo);

    // 生成各个根节点
    switch (Nd->Kind) {
    // bitwise op
        case ND_BITNOT:
            genExpr(Nd->LHS);
            // 这里的 not a0, a0 为 xori a0, a0, -1 的伪码
            println("  not a0, a0");
            return;
    // logical op
        case ND_NOT:
            genExpr(Nd->LHS);
            notZero(Nd->LHS->Ty);
            println("  seqz a0, a0");
            return;
        // 逻辑与
        case ND_LOGAND: {
            int C = count();
            genExpr(Nd->LHS);
            notZero(Nd->LHS->Ty);
            // 左部短路操作判断，为0则跳转
            println("  beqz a0, .L.false.%d", C);
            genExpr(Nd->RHS);
            notZero(Nd->RHS->Ty);
            // 右部判断，为0则跳转
            println("  beqz a0, .L.false.%d", C);
            println("  li a0, 1");
            println("  j .L.end.%d", C);
            println(".L.false.%d:", C);
            println("  li a0, 0");
            println(".L.end.%d:", C);
            return;
        }
        // 逻辑或
        case ND_LOGOR: {
            int C = count();
            genExpr(Nd->LHS);
            notZero(Nd->LHS->Ty);
            // 左部短路操作判断，不为0则跳转
            println("  bnez a0, .L.true.%d", C);
            genExpr(Nd->RHS);
            notZero(Nd->RHS->Ty);
            // 右部判断，不为0则跳转
            println("  bnez a0, .L.true.%d", C);
            println("  li a0, 0");
            println("  j .L.end.%d", C);
            println(".L.true.%d:", C);
            println("  li a0, 1");
            println(".L.end.%d:", C);
            return;
        }
        // 对寄存器取反
        case ND_NEG:
            genExpr(Nd->LHS);
            switch (Nd->Ty->Kind) {
                case TY_FLOAT:
                    println("  fneg.s fa0, fa0");
                    return;
                case TY_DOUBLE:
                    println("  fneg.d fa0, fa0");
                    return;
                default:
                    // neg a0, a0是sub a0, x0, a0的别名, 即a0=0-a0
                    println("  neg%s a0, a0", Nd->Ty->Size <= 4 ? "w" : "");
                    return;
            }
    // others. general op
        case ND_NUM: {
            switch (Nd->Ty->Kind) {
                case TY_FLOAT:{
                    // 将a0转换到float类型值为%f的fa0中
                    // can't do the cast directly like (uint32_t)Nd->FVal.
                    // if so, something like 0.999 will be truncated to 0.
                    // and we wish things like (_Bool)0.1l to be "true".
                    // we need to reinterpret the bits here
                    float f = Nd->FVal;
                    println("  li a0, %u  # float %f", *(uint32_t*)&f, Nd->FVal);
                    println("  fmv.w.x fa0, a0");
                    return;
                }
                case TY_DOUBLE:
                    // 将a0转换到double类型值为%f的fa0中
                    println("  li a0, %lu  # double %f", *(uint64_t*)&Nd->FVal, Nd->FVal);
                    println("  fmv.d.x fa0, a0");
                    return;
                default:
                    println("  li a0, %ld", Nd->Val);
                    return;
            }
        }

        // 变量. note: array also has VAR type
        case ND_VAR:
        case ND_MEMBER:
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
            // 计算所有参数的值，正向压栈
            pushArgs(Nd->Args);
            // 将a0的值(fn address)存入t5
            // LHS is an ident(ND_VAR), genExpr
            // will get that ident's address
            genExpr(Nd->LHS);
            println("  mv t5, a0");
            // 反向弹栈，a0->参数1，a1->参数2...
            int GP = 0, FP = 0;
            // 读取函数形参中的参数类型
            Type *CurArg = Nd->FuncType->Params;
            for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
                // 如果是可变参数函数
                // 匹配到空参数（最后一个）的时候，将剩余的整型寄存器弹栈
                if (Nd->FuncType->IsVariadic && CurArg == NULL) {
                    if (GP < 8) {
                        println("  # a%d传递可变实参", GP);
                        pop(GP++);
                    }
                    continue;
                }

                CurArg = CurArg->Next;
                if (isFloNum(Arg->Ty)) {
                    if (FP < 8) {
                        println("  # fa%d传递浮点参数", FP);
                        popF(FP++);
                    } else if (GP < 8) {
                        println("  # a%d传递浮点参数", GP);
                        pop(GP++);
                    }
                } else {
                    if (GP < 8) {
                        println("  # a%d传递整型参数", GP);
                        pop(GP++);
                    }
                }
            }
            // 调用函数
            // the contents of the function is generated by test.sh, not by rvccl
            if (Depth % 2 == 0) {
                // 偶数深度，sp已经对齐16字节
                println("  jalr t5  # %s", Nd->FuncName);
            } else {
                // 对齐sp到16字节的边界
                println("  addi sp, sp, -8");
                println("  jalr t5  # %s", Nd->FuncName);
                println("  addi sp, sp, 8");
            }
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
        // 类型转换
        case ND_CAST:
            genExpr(Nd->LHS);
            cast(Nd->LHS->Ty, Nd->Ty);
            return;
        // 条件运算符
        case ND_COND: {
            int C = count();
            genExpr(Nd->Cond);
            notZero(Nd->Cond->Ty);
            println("  beqz a0, .L.else.%d", C);
            genExpr(Nd->Then);
            println("  j .L.end.%d", C);
            println(".L.else.%d:", C);
            genExpr(Nd->Els);
            println(".L.end.%d:", C);
            return;
        }
        // 内存清零
        case ND_MEMZERO: {
            int Offset = Nd->Var->Offset;
            int Size = Nd->Var->Ty->Size;
            println("  # 对%s的内存%d(fp)清零%d位", Nd->Var->Name, Offset, Size);
            // 对栈内变量所占用的每个字节都进行清零
            int I = 0;
            if(isLegalImmS(Offset)){
                while( I + 8 <= Size){
                    println("  sd zero, %d(fp)", Offset+I);
                    I += 8;
                }
                while( I + 4 <= Size){
                    println("  sw zero, %d(fp)", Offset+I);
                    I += 4;
                }
                while( I + 2 <= Size){
                    println("  sh zero, %d(fp)", Offset+I);
                    I += 2;
                }
                while( I + 1 <= Size){
                    println("  sb zero, %d(fp)", Offset+I);
                    I += 1;
                }
            }
            else{
                while( I + 8 <= Size){
                    println("  li t0, %d", Offset + I);
                    println("  add t0, fp, t0");
                    println("  sb zero, 0(t0)");
                    I += 8;
                }
                while( I + 4 <= Size){
                    println("  li t0, %d", Offset + I);
                    println("  add t0, fp, t0");
                    println("  sw zero, 0(t0)");
                    I += 4;
                }
                while( I + 2 <= Size){
                    println("  li t0, %d", Offset + I);
                    println("  add t0, fp, t0");
                    println("  sh zero, 0(t0)");
                    I += 2;
                }
                while( I + 1 <= Size){
                    println("  li t0, %d", Offset + I);
                    println("  add t0, fp, t0");
                    println("  sb zero, 0(t0)");
                    I += 1;
                }
            }
            return;
        }
        // 空表达式
        case ND_NULL_EXPR:
            return;

        default:
            break;
    }


    // 处理浮点类型
    if (isFloNum(Nd->LHS->Ty)) {
        // 递归到最右节点
        genExpr(Nd->RHS);
        // 将结果压入栈
        pushF();
        // 递归到左节点
        genExpr(Nd->LHS);
        // 将结果弹栈到fa1
        popF(1);

        // 生成各个二叉树节点
        // float对应s(single)后缀，double对应d(double)后缀
        char *Suffix = (Nd->LHS->Ty->Kind == TY_FLOAT) ? "s" : "d";

        switch (Nd->Kind) {
            case ND_ADD:
                println("  fadd.%s fa0, fa0, fa1", Suffix);
                return;
            case ND_SUB:
                println("  fsub.%s fa0, fa0, fa1", Suffix);
                return;
            case ND_MUL:
                println("  fmul.%s fa0, fa0, fa1", Suffix);
                return;
            case ND_DIV:
                println("  fdiv.%s fa0, fa0, fa1", Suffix);
                return;
            case ND_EQ:
                println("  feq.%s a0, fa0, fa1", Suffix);
                return;
            case ND_NE:
                println("  feq.%s a0, fa0, fa1", Suffix);
                println("  seqz a0, a0");
                return;
            case ND_LT:
                println("  flt.%s a0, fa0, fa1", Suffix);
                return;
            case ND_LE:
                println("  fle.%s a0, fa0, fa1", Suffix);
                return;
            default:
                errorTok(Nd->Tok, "invalid expression");
        }
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
    pop(1);

    // a0: lhs value. a1: rhs value
    // 生成各个二叉树节点
    // ptr and long still use RV64, other data types can use RV32
    char *Suffix = Nd->LHS->Ty->Kind == TY_LONG || Nd->LHS->Ty->Base? "" : "w";
    switch (Nd->Kind) {
        case ND_ADD: // + a0=a0+a1
            println("  add%s a0, a0, a1", Suffix);
            return;
        case ND_SUB: // - a0=a0-a1
            println("  sub%s a0, a0, a1", Suffix);
            return;
        case ND_MUL: // * a0=a0*a1
            println("  mul%s a0, a0, a1", Suffix);
            return;
        case ND_DIV: // / a0=a0/a1
            println("  div%s%s a0, a0, a1", Nd->Ty->IsUnsigned? "u": "", Suffix);
            return;
        case ND_MOD: // % a0=a0%a1
            println("  rem%s%s a0, a0, a1", Nd->Ty->IsUnsigned? "u": "", Suffix);
            return;
        case ND_SHL:
            println("  sll%s a0, a0, a1", Suffix);
            return;
        case ND_SHR:
            if (Nd->Ty->IsUnsigned)
                println("  srl%s a0, a0, a1", Suffix);
            else
                println("  sra%s a0, a0, a1", Suffix);
            return;
        case ND_NE:
        case ND_EQ:
            if (Nd->LHS->Ty->IsUnsigned && Nd->LHS->Ty->Kind == TY_INT) {
                println("  # 左部是U32类型，需要截断");
                println("slli a0, a0, 32");
                println("srli a0, a0, 32");
            };
            if (Nd->RHS->Ty->IsUnsigned && Nd->RHS->Ty->Kind == TY_INT) {
                println("  # 右部是U32类型，需要截断");
                println("slli a1, a1, 32");
                println("srli a1, a1, 32");
            };
            println("  xor a0, a0, a1");
        if(Nd->Kind ==  ND_EQ)
            // if a0 == a1, then a0 ^ a1 should be 0
            println("  seqz a0, a0");
        else
            // if a0 != a1, then a0 ^ a1 should not be 0
            println("  snez a0, a0");
            return;
        case ND_LE: // a0 <= a1
            // a0 <= a1 -> !(a0 > a1)
            // note: '!' here means 0 -> 1, 1-> 0. 
            // which is different from the 'neg' inst
            println("  slt%s a0, a1, a0", Nd->LHS->Ty->IsUnsigned? "u": "");
            println("  xori a0, a0, 1");
            return;
        case ND_LT: // a0 < a1
            println("  slt%s a0, a0, a1", Nd->LHS->Ty->IsUnsigned? "u": "");
            return;
        case ND_BITAND: // & a0=a0&a1
            println("  and a0, a0, a1");
            return;
        case ND_BITOR: // | a0=a0|a1
            println("  or a0, a0, a1");
            return;
        case ND_BITXOR: // ^ a0=a0^a1
            println("  xor a0, a0, a1");
            return;
        default:
            break;
    }

    error("%s: invalid expression", Nd -> Tok -> Loc);
}

// 生成语句
static void genStmt(Node *Nd) {
    // .loc 文件编号 行号, debug use
    println("  .loc %d %d", Nd->Tok->File->FileNo, Nd->Tok->LineNo);

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
            notZero(Nd->Cond->Ty);
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
                notZero(Nd->Cond->Ty);
                // 判断结果是否为0，为0则跳转到结束部分
                println("  beqz a0, %s", Nd->BrkLabel);
            }
            // 生成循环体语句
            genStmt(Nd->Then);
            // continue标签语句
            println("%s:", Nd->ContLabel);
            // 处理循环递增语句
            if (Nd -> Inc){
                println("\n# Inc语句%d", C);
                // 生成循环递增语句
                genExpr(Nd->Inc);
            }
            // 跳转到循环头部
            println("  j .L.begin.%d", C);
            // 输出循环尾部标签
            println("%s:", Nd->BrkLabel);
            return;
        }
        // goto语句
        case ND_GOTO:
            println("  j %s", Nd->UniqueLabel);
            return;
        // 和while语句的区别：do先执行语句，while先判断cond
        case ND_DO: {
            int C = count();
            println("\n# =====do while语句%d============", C);
            println(".L.begin.%d:", C);

            println("\n# Then语句%d", C);
            genStmt(Nd->Then);

            println("\n# Cond语句%d", C);
            println("%s:", Nd->ContLabel);
            genExpr(Nd->Cond);
            notZero(Nd->Cond->Ty);
            println("  bnez a0, .L.begin.%d", C);

            println("%s:", Nd->BrkLabel);
            return;
        }

        // 标签语句, print the label
        case ND_LABEL:
            println("%s:", Nd->UniqueLabel);
            genStmt(Nd->LHS);
            return;
        case ND_SWITCH:
            println("\n# =====switch语句===============");
            genExpr(Nd->Cond);

            println("  # 遍历跳转到值等于a0的case标签");
            for (Node *N = Nd->CaseNext; N; N = N->CaseNext) {
                println("  li t0, %ld", N->Val);
                println("  beq a0, t0, %s", N->Label);
            }

            if (Nd->DefaultCase) {
                println("  # 跳转到default标签");
                println("  j %s", Nd->DefaultCase->Label);
            }

            println("  # 结束switch，跳转break标签");
            println("  j %s", Nd->BrkLabel);
            // 生成case标签的语句
            genStmt(Nd->Then);
            println("# switch的break标签，结束switch");
            println("%s:", Nd->BrkLabel);
            return;
        case ND_CASE:
            println("# case标签，值为%ld", Nd->Val);
            println("%s:", Nd->Label);
            genStmt(Nd->LHS);
            return;

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
        if (Var->Ty->Kind == TY_FUNC || !Var->IsDefinition)
            continue;

        char *visibility = Var->IsStatic? ".local": ".global";
        println("  %s %s", visibility, Var->Name);

        if (!Var->Align)
            error("Align can not be 0!");
        println("  .align %d", simpleLog2(Var->Align));

        if (Var -> InitData){
            println("  .data");
            println("%s:", Var->Name);
            Relocation *Rel = Var->Rel;
            int Pos = 0;
            while (Pos < Var->Ty->Size) {
                // char g1[] = "123456";
                // char g2[] = "aaaaaa";
                // char *g3[] = {g1+0, g1+1, g1+2}; // {123456, 23456, 3456}
                // rel->addend = 1, 2, 3(newAdd). offset is for struct members
                if (Rel && Rel->Offset == Pos) {
                    // 使用其他变量进行初始化
                    println("  # %s全局变量", Var->Name);
                    println("  .quad %s%+ld", Rel->Label, Rel->Addend);
                    Rel = Rel->Next;
                    Pos += 8;
                } else {
                    // 打印出字符串的内容，包括转义字符
                    char C = Var->InitData[Pos++];
                    if (isprint(C))
                        println("  .byte %d\t# 字符：%c", C, C);
                    else
                    println("  .byte %d", C);
                }
            }
        }
        else{
            // bss段未给数据分配空间，只记录数据所需空间的大小
            println("  # 未初始化的全局变量");
            println("  .bss");
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
        // not a function, or just a function defination without body.
        if (!Fn->Body)
            continue;

        if (Fn->IsStatic)
            println("  .local %s", Fn->Name);
        else
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
        if(isLegalImmI(Fn->StackSize))
            println("  addi sp, sp, -%d", Fn->StackSize);
        else{
            println("  li t0, -%d", Fn->StackSize);
        println("  add sp, sp, t0");
    }
    // map (actual params) -> (formal params)
    // this needs to be done before entering the fn body
    // then in the fn body we can use its formal params
    // in stack as if they were passed from outside

    // 记录整型寄存器，浮点寄存器使用的数量
    int GP = 0, FP = 0;
    for (Obj *Var = Fn->Params; Var; Var = Var->Next) {
        if (isFloNum(Var->Ty)) {
            // 正常传递的浮点形参
            if (FP < 8)
                storeFloat(FP++, Var->Offset, Var->Ty->Size);
            else
                storeGeneral(GP++, Var->Offset, Var->Ty->Size);
        }
        else
            // 正常传递的整型形参
            storeGeneral(GP++, Var->Offset, Var->Ty->Size);
        }

        // 可变参数
        if (Fn->VaArea) {
            // 可变参数存入__va_area__，注意最多为7个
            int Offset = Fn->VaArea->Offset;
            while (GP < 8) {
                storeGeneral(GP++, Offset, 8);
                Offset += 8;
            }
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

    // 获取所有的输入文件，并输出.file指示
    File **Files = getInputFiles();
    for (int I = 0; Files[I]; I++)
        println("  .file %d \"%s\"", Files[I]->FileNo, Files[I]->Name);

    // 为本地变量计算偏移量, 以及决定函数最终的栈大小
    assignLVarOffsets(Prog);
    // 生成数据
    emitData(Prog);
    // 生成代码
    emitText(Prog);
}