#include "rvcc.h"

#define GP_MAX 8
#define FP_MAX 8

//
// some helper functions
//

// 标记是否生成common块
extern bool OptFCommon;
// 记录栈深度
static int Depth;
// 当前的函数
static Obj *CurrentFn;
// 输出文件
static FILE *OutputFile;
// 记录大结构体的深度
static int BSDepth;
// 我们将fs0～fs11两两组对形成6个寄存器对
// 用于long double类型的存储，每次+2
static int LDSP;

/*
void println(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vfprintf(OutputFile, fmt, va);
    fprintf(OutputFile, "\n");
    va_end(va);
    //fprintf(OutputFile, fmt, __builtin_va_arg_pack());
}
*/

// reg1 -> reg2
void memcopy(char *reg1, char *reg2, int offset, int size){
    int I = 0;
    while (I + 8 <= size) {
        println("  ld t1, %d(%s)", I, reg1);
        println("  sd t1, %d(%s)", offset + I, reg2);
        I += 8;
    }
    while (I + 4 <= size) {
        println("  lw t1, %d(%s)", I, reg1);
        println("  sw t1, %d(%s)", offset + I, reg2);
        I += 4;
    }
    while (I + 2 <= size) {
        println("  lh t1, %d(%s)", I, reg1);
        println("  sh t1, %d(%s)", offset + I, reg2);
        I += 2;
    }
    while (I + 1 <= size) {
        println("  lb t1, %d(%s)", I, reg1);
        println("  sb t1, %d(%s)", offset + I, reg2);
        I += 1;
    }
}

// 对齐到Align的整数倍
int alignTo(int N, int Align) {
    // (0,Align]返回Align
    return (N + Align - 1) / Align * Align;
}

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

// 对于long double类型进行压栈
static void pushLD(void) {
    println("  # LD压栈，将a0,a1的值存入LD栈顶");
    println("  fmv.d.x fs%d, a1", LDSP + 1);
    println("  fmv.d.x fs%d, a0", LDSP);
    LDSP += 2;
    if (LDSP > 10)
        error("LDSP can't be larger than 10!");
}

// 对于long double类型进行弹栈
static void popLD(int Reg) {
    LDSP -= 2;
    if (LDSP < 0)
        error("LDSP can't be less than 0!");
    println("  # LD弹栈，将LD栈顶的值存入a%d,a%d", Reg, Reg + 1);
    println("  fmv.x.d a%d, fs%d", Reg + 1, LDSP + 1);
    println("  fmv.x.d a%d, fs%d", Reg, LDSP);
}


static void genExpr(Node *Nd);
static void genStmt(Node *Nd);

// 获取浮点结构体的成员类型
// float type struct: 1 or 2 members, with at least 1 member of float type
void getFloStMemsTy(Type *Ty, Type **RegsTy, int *Idx) {
    switch (Ty->Kind) {
        case TY_STRUCT:
            // 遍历结构体的成员，获取成员类型
            for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next)
                getFloStMemsTy(Mem->Ty, RegsTy, Idx);
            return;
        case TY_UNION:
            // 含有联合体不是浮点结构体
            *Idx += 2;
            return;
        case TY_ARRAY:
            // 遍历数组的成员，计算是否为浮点结构体
            for (int I = 0; I < Ty->ArrayLen; ++I)
            getFloStMemsTy(Ty->Base, RegsTy, Idx);
            return;
        case TY_LDOUBLE:
            // long double不是浮点结构体
            *Idx += 2;
            return;
        default:
            // 若为基础类型，且存在可用寄存器时，填充成员的类型
            if (*Idx < 2)
                RegsTy[*Idx] = Ty;
                *Idx += 1;
            return;
        }
}

// 是否为一或两个含浮点成员变量的结构体
void setFloStMemsTy(Type **Ty, int GP, int FP) {
    Type *T = *Ty;
    T->FSReg1Ty = TyVoid;
    T->FSReg2Ty = TyVoid;

    // 联合体不通过浮点寄存器传递
    if (T->Kind == TY_UNION)
        return;

    // RTy：RegsType，结构体的第一、二个寄存器的类型
    Type *RTy[2] = {TyVoid, TyVoid};
    // 记录可以使用的寄存器的索引值
    int RegsTyIdx = 0;
    // 获取浮点结构体的寄存器类型，如果不是则为TyVoid
    getFloStMemsTy(T, RTy, &RegsTyIdx);

    // 不是浮点结构体，直接退出
    if (RegsTyIdx > 2)
        return;

    if ( // 只有一个浮点成员的结构体，使用1个FP
        (isSFloNum(RTy[0]) && RTy[1] == TyVoid && FP < FP_MAX) ||
        // 一个浮点成员和一个整型成员的结构体，使用1个FP和1个GP
        (isSFloNum(RTy[0]) && isInteger(RTy[1]) && FP < FP_MAX && GP < GP_MAX) ||
        (isInteger(RTy[0]) && isSFloNum(RTy[1]) && FP < FP_MAX && GP < GP_MAX) ||
        // 两个浮点成员的结构体，使用2个FP
        (isSFloNum(RTy[0]) && isSFloNum(RTy[1]) && FP + 1 < FP_MAX))
    {
            T->FSReg1Ty = RTy[0];
            T->FSReg2Ty = RTy[1];
    }
}

// 为大结构体开辟空间
static int createBSSpace(Node *Args) {
    int BSStack = 0;
    for (Node *Arg = Args; Arg; Arg = Arg->Next) {
        Type *Ty = Arg->Ty;
        // 大于16字节的结构体
        if (Ty->Size > 16 && Ty->Kind == TY_STRUCT) {
            println("  # 大于16字节的结构体，先开辟相应的栈空间");
            int Sz = alignTo(Ty->Size, 8);
            println("  addi sp, sp, -%d", Sz);
            // t6指向了最终的 大结构体空间的起始位置
            println("  mv t6, sp");
            Depth += Sz / 8;
            BSStack += Sz / 8;
            BSDepth += Sz / 8;
        }
    }
    return BSStack;
}

// 传递结构体的指针
static void pushStruct(Type *Ty) {
    // 大于16字节的结构体
    // 将结构体复制一份到栈中，然后通过寄存器或栈传递被复制结构体的地址
    // ---------------------------------
    //             大结构体      ←
    // --------------------------------- <- t6
    //      栈传递的   其他变量
    // ---------------------------------
    //            大结构体的指针  ↑
    // --------------------------------- <- sp
    if (Ty->Size > 16) {
        // 计算大结构体的偏移量
        int Sz = alignTo(Ty->Size, 8);
        // BSDepth记录了剩余 大结构体的字节数
        BSDepth -= Sz / 8;
        // t6存储了，大结构体空间的起始位置
        int BSOffset = BSDepth * 8;

        println("  # 复制%d字节的大结构体到%d(t6)的位置", Sz, BSOffset);
        memcopy("a0", "t6", BSOffset, Sz);

        println("  # 大于16字节的结构体，对该结构体地址压栈");
        println("  addi a0, t6, %d", BSOffset);
        push();
        return;
    }

    // 含有两个成员（含浮点）的结构体
    // 展开到栈内的两个8字节的空间
    if ((isSFloNum(Ty->FSReg1Ty) && Ty->FSReg2Ty != TyVoid) ||
        isSFloNum(Ty->FSReg2Ty)) {
        println("  # 对含有两个成员（含浮点）结构体进行压栈");
        println("  addi sp, sp, -16");
        Depth += 2;

        println("  ld t0, 0(a0)");
        println("  sd t0, 0(sp)");

        // 计算第二部分在结构体中的偏移量，为两个成员间的最大尺寸
        int Off = MAX(Ty->FSReg1Ty->Size, Ty->FSReg2Ty->Size);
        println("  ld t0, %d(a0)", Off);
        println("  sd t0, 8(sp)");

        return;
    }
    // 处理只有一个浮点成员的结构体
    // 或者是小于16字节的结构体
    char *Str = isSFloNum(Ty->FSReg1Ty) ? "只有一个浮点" : "小于16字节";
    int Sz = alignTo(Ty->Size, 8);
    println("  # 为%s的结构体开辟%d字节的空间，", Str, Sz);
    println("  addi sp, sp, -%d", Sz);
    Depth += Sz / 8;

    println("  # 开辟%d字节的空间，复制%s的内存", Sz, Str);
    memcopy("a0", "sp", 0, Ty->Size);
    return;
}

// 存储结构体到栈内开辟的空间
static void storeStruct(int Reg, int Offset, int Size) {
    // t0是结构体的地址，复制t0指向的结构体到栈相应的位置中
    for (int I = 0; I < Size; I++) {
        println("  lb t0, %d(a%d)", I, Reg);

        println("  li t1, %d", Offset + I);
        println("  add t1, fp, t1");
        println("  sb t0, 0(t1)");
    }
    return;
}


// 将函数实参计算后压入栈中
static void pushArgs2(Node *Args, bool FirstPass) {
    // 参数为空直接返回
    if (!Args)
        return;

    // 递归到最后一个实参进行
    pushArgs2(Args->Next, FirstPass);

    // 第一遍对栈传递的变量进行压栈
    // 第二遍对寄存器传递的变量进行压栈
    if ((FirstPass && !Args->PassByStack) ||
        (!FirstPass && Args->PassByStack))
        return;

    println("\n  # ↓对表达式进行计算，然后压栈↓");
    // 计算出表达式
    genExpr(Args);
    // 根据表达式结果的类型进行压栈
    switch(Args->Ty->Kind) {
        case TY_STRUCT:
        case TY_UNION:
            pushStruct(Args->Ty);
            break;
        case TY_FLOAT:
        case TY_DOUBLE:
            pushF();
            break;
        case TY_LDOUBLE:
            println("  # 对long double参数表达式进行计算后压栈");
            LDSP -= 2;
            println("  addi sp, sp, -16");
            println("  fsd fs%d, 8(sp)", LDSP + 1);
            println("  fsd fs%d, 0(sp)", LDSP);
            Depth += 2;
            break;
        default:
            push();
            break;
    }
    println("  # ↑结束压栈↑");
}

// 处理参数后进行压栈
static int pushArgs(Node *Nd) {
    int Stack = 0, GP = 0, FP = 0;
    // 如果是超过16字节的结构体，则通过第一个寄存器传递结构体的指针
    if (Nd->RetBuffer && Nd->Ty->Size > 16)
        GP++;

    // 遍历所有参数，优先使用寄存器传递，然后是栈传递
    Type *CurArg = Nd->FuncType->Params;
    for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
        // 如果是可变参数的参数，只使用整型寄存器和栈传递
        if (Nd->FuncType->IsVariadic && CurArg == NULL) {
            int64_t Val = Arg->Val ? Arg->Val : Arg->FVal;
            if (GP < GP_MAX) {
                println("  # 可变参数%ld值通过a%d传递", Val, GP);
                GP++;
            } else {
                println("  # 可变参数%ld值通过栈传递", Val);
                Arg->PassByStack = true;
                Stack++;
            }
            continue;
        }

        // 遍历相应的实参，用于检查是不是到了可变参数
        CurArg = CurArg->Next;

        // 读取实参的类型
        Type *Ty = Arg->Ty;
        switch (Ty->Kind) {
            case TY_STRUCT:
            case TY_UNION: {
                // 判断结构体的类型
                setFloStMemsTy(&Ty, GP, FP);
                // 处理一或两个浮点成员变量的结构体
                if (isSFloNum(Ty->FSReg1Ty) || isSFloNum(Ty->FSReg2Ty)) {
                    Type *Regs[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
                    for (int I = 0; I < 2; ++I) {
                    if (isSFloNum(Regs[I]))
                        FP++;
                    if (isInteger(Regs[I]))
                        GP++;
                    }
                    break;
                }

                // 9~16字节整型结构体用两个寄存器，其他字节结构体用一个寄存器
                int Regs = (8 < Ty->Size && Ty->Size <= 16) ? 2 : 1;
                for (int I = 1; I <= Regs; ++I) {
                    if (GP < GP_MAX) {
                        GP++;
                    }
                    else {
                        Arg->PassByStack = true;
                        Stack++;
                    }
                }
                break;
            }
            case TY_FLOAT:
            case TY_DOUBLE:
                // 浮点优先使用FP，而后是GP，最后是栈传递
                if (FP < FP_MAX) {
                    println("  # 浮点%Lf值通过fa%d传递", Arg->FVal, FP);
                    FP++;
                } else if (GP < GP_MAX) {
                    println("  # 浮点%Lf值通过a%d传递", Arg->FVal, GP);
                    GP++;
                } else {
                    println("  # 浮点%Lf值通过栈传递", Arg->FVal);
                    Arg->PassByStack = true;
                    Stack++;
                }
                break;
            case TY_LDOUBLE:
                for (int I = 1; I <= 2; ++I) {
                    if (GP < GP_MAX) {
                        println("  # LD的第%d部分%Lf值通过a%d传递", I, Arg->FVal, GP);
                        GP++;
                    } else {
                        println("  # LD的第%d部分%Lf值通过栈传递", I, Arg->FVal);
                        Stack++;
                    }
                }
                break;
            default:
                // 整型优先使用GP，最后是栈传递
                if (GP < GP_MAX) {
                    println("  # 整型%ld值通过a%d传递", Arg->Val, GP);
                    GP++;
                } else {
                    println("  # 整型%ld值通过栈传递", Arg->Val);
                    Arg->PassByStack = true;
                    Stack++;
                }
                break;
        }
    }

        // 对齐栈边界
        if ((Depth + Stack) % 2 == 1) {
            println("  # 对齐栈边界到16字节");
            println("  addi sp, sp, -8");
            Depth++;
            Stack++;
        }

    // 进行压栈
    // 开辟大于16字节的结构体的栈空间
    int BSStack = createBSSpace(Nd->Args);
    // 第一遍对栈传递的变量进行压栈
    pushArgs2(Nd->Args, true);
    // 第二遍对寄存器传递的变量进行压栈
    pushArgs2(Nd->Args, false);
    if (Nd->RetBuffer && Nd->Ty->Size > 16) {
        println("  # 返回类型是大于16字节的结构体，指向其的指针，压入栈顶");
        println("  li t0, %d", Nd->RetBuffer->Offset);
        println("  add a0, fp, t0");
        push();
    }

    // 返回栈传递参数的个数
    return Stack + BSStack;
}

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
            error("bad size: %d. unreachable\n", Size);
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
    case TY_VLA:
        return;
    case TY_FLOAT:
        // 访问a0中存放的地址，取得的值存入fa0
        println("  flw fa0, 0(a0)");
        return;
    case TY_DOUBLE:
        // 访问a0中存放的地址，取得的值存入fa0"
        println("  fld fa0, 0(a0)");
        return;
    case TY_LDOUBLE:
        println("  # 访问a0中存放的地址，取得的值存入LD栈当中");
        println("  fld fs%d, 8(a0)", LDSP + 1);
        println("  fld fs%d, 0(a0)", LDSP);
        LDSP += 2;
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

// 将a0存入栈顶值(为一个地址).
static void store(Type *Ty) {
    pop(1);
    switch(Ty->Kind){
        case TY_STRUCT:
        case TY_UNION:{
            // copy all the bytes from one struct to another
            memcopy("a0", "a1", 0, Ty->Size);
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
        case TY_LDOUBLE:
            println("  # 将LD栈顶值，写入到a1中存放地址");
            LDSP -= 2;
            println("  fsd fs%d, 8(a1)", LDSP + 1);
            println("  fsd fs%d, 0(a1)", LDSP);
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
        case TY_LDOUBLE:
            println("  # 判断fa1是否不为0，为0置0，非0置1");
            popLD(0);
            println("  mv a2, zero");
            println("  mv a3, zero");
            println("  call __netf2@plt");
            println("  snez a0, a0");
            return;
        default:
            return;
    }
}


// 类型枚举
// note: don't modify their order. these are used as index in castTable
enum { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, F128 };

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
    case TY_LDOUBLE:
        return F128;
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
static char i32f128[] = "  # i32转换为f128类型\n"
                        "  call __floatsitf@plt";

static char i64f32[] =  "  # i64转换为f32类型\n"
                        "  fcvt.s.l fa0, a0";
static char i64f64[] =  "  # i64转换为f64类型\n"
                        "  fcvt.d.l fa0, a0";
static char i64f128[] = "  # i64转换为f128类型\n"
                        "  call __floatditf@plt";

static char u32f32[] =  "  # u32转换为f32类型\n"
                        "  fcvt.s.wu fa0, a0";
static char u32f64[] =  "  # u32转换为f64类型\n"
                        "  fcvt.d.wu fa0, a0";
static char u32f128[] = "  # u32转换为f128类型\n"
                        "  call __floatunsitf@plt";

static char u64f32[] =  "  # u64转换为f32类型\n"
                        "  fcvt.s.lu fa0, a0";
static char u64f64[] =  "  # u64转换为f64类型\n"
                        "  fcvt.d.lu fa0, a0";
static char u64f128[] = "  # u64转换为f128类型\n"
                        "  call __floatunditf@plt";

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
static char f32f128[] = "  # f32转换为f128类型\n"
                        "  call __extendsftf2@plt";

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
static char f64f128[] = "  # f64转换为f128类型\n"
                        "  call __extenddftf2@plt";


// long double转换
static char f128i8[] =  "  # f128转换为i8类型\n"
                        "  call __fixtfsi@plt\n"
                        "  slli a0, a0, 56\n"
                        "  srai a0, a0, 56";

static char f128i16[] = "  # f128转换为i16类型\n"
                        "  call __fixtfsi@plt\n"
                        "  slli a0, a0, 48\n"
                        "  srai a0, a0, 48";

static char f128i32[] = "  # f128转换为i32类型\n"
                        "  call __fixtfsi@plt\n"
                        "  slli a0, a0, 32\n"
                        "  srai a0, a0, 32";

static char f128i64[] = "  # f128转换为i64类型\n"
                        "  call __fixtfdi@plt";

static char f128u8[] =  "  # f128转换为u8类型\n"
                        "  call __fixunstfsi@plt\n"
                        "  slli a0, a0, 56\n"
                        "  srli a0, a0, 56";

static char f128u16[] = "  # f128转换为u16类型\n"
                        "  call __fixunstfsi@plt\n"
                        "  slli a0, a0, 48\n"
                        "  srli a0, a0, 48";

static char f128u32[] = "  # f128转换为u32类型\n"
                        "  call __fixunstfsi@plt\n"
                        "  slli a0, a0, 32\n"
                        "  srai a0, a0, 32";

static char f128u64[] = "  # f128转换为u64类型\n"
                        "  call __fixunstfdi@plt";

static char f128f32[] = "  # f128转换为f32类型\n"
                        "  call __trunctfsf2@plt";

static char f128f64[] = "  # f128转换为f64类型\n"
                        "  call __trunctfdf2@plt";


// 所有类型转换表
static char *castTable[11][11] = {
    // 被映射到
    // {i8,  i16,     i32,     i64,     u8,     u16,     u32,     u64,     f32,     f64,     f128}
    {NULL,   NULL,    NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64,  i32f128}, // 从i8转换
    {i64i8,  NULL,    NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64,  i32f128}, // 从i16转换
    {i64i8,  i64i16,  NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64,  i32f128}, // 从i32转换
    {i64i8,  i64i16,  i64i32,  NULL,    i64u8,  i64u16,  i64u32,  NULL,    i64f32,  i64f64,  i64f128}, // 从i64转换

    {i64i8,  NULL,    NULL,    NULL,    NULL,   NULL,    NULL,    NULL,    u32f32,  u32f64,  u32f128}, // 从u8转换
    {i64i8,  i64i16,  NULL,    NULL,    i64u8,  NULL,    NULL,    NULL,    u32f32,  u32f64,  u32f128}, // 从u16转换
    {i64i8,  i64i16,  i64i32,  u32i64,  i64u8,  i64u16,  NULL,    u32i64,  u32f32,  u32f64,  u32f128}, // 从u32转换
    {i64i8,  i64i16,  i64i32,  NULL,    i64u8,  i64u16,  i64u32,  NULL,    u64f32,  u64f64,  u64f128}, // 从u64转换

    {f32i8,  f32i16,  f32i32,  f32i64,  f32u8,  f32u16,  f32u32,  f32u64,  NULL,    f32f64,  f32f128}, // 从f32转换
    {f64i8,  f64i16,  f64i32,  f64i64,  f64u8,  f64u16,  f64u32,  f64u64,  f64f32,  NULL,    f64f128}, // 从f64转换
    {f128i8, f128i16, f128i32, f128i64, f128u8, f128u16, f128u32, f128u64, f128f32, f128f64, NULL},    // 从f128转换
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

    if (castTable[T1][T2]){
        if (T1 == F128) popLD(0);
        println("%s", castTable[T1][T2]);
        if (T2 == F128) pushLD();
    }
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
            // VLA可变长度数组是局部变量
            if (Nd->Var->Ty->Kind == TY_VLA) {
                println("  # 为VLA生成局部变量");
                println("  li t0, %d", Nd->Var->Offset);
                println("  add t0, t0, fp");
                println("  ld a0, 0(t0)");
                return;
            }

            // 线程局部变量
            if (Nd->Var->IsTLS) {
                // 计算TLS高20位地址
                println("  lui a0, %%tprel_hi(%s)", Nd->Var->Name);
                // 计算TLS低12位地址
                println("  addi a0, a0, %%tprel_lo(%s)", Nd->Var->Name);
                return;
            }

            // 生成位置无关代码
            if (OptFPIC) {
                int C = count();
                println(".Lpcrel_hi%d:", C);
                // 线程局部变量
                if (Nd->Var->IsTLS) {
                    println("  # 获取PIC中TLS%s的地址", Nd->Var->Name);
                    // 计算TLS高20位地址
                    println("  auipc a0, %%tls_gd_pcrel_hi(%s)", Nd->Var->Name);
                    // 计算TLS低12位地址
                    println("  addi a0, a0, %%pcrel_lo(.Lpcrel_hi%d)", C);
                    // 获取地址
                    println("  call __tls_get_addr@plt");
                    return;
                }
                // 函数或者全局变量
                println("  # 获取PIC中%s%s的地址",
                    Nd->Ty->Kind == TY_FUNC ? "函数" : "全局变量", Nd->Var->Name);
                // 高20位地址，存到a0中
                println("  auipc a0, %%got_pcrel_hi(%s)", Nd->Var->Name);
                // 低12位地址，加到a0中
                println("  ld a0, %%pcrel_lo(.Lpcrel_hi%d)(a0)", C);
                return;
            }

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
        // 函数调用
        case ND_FUNCALL:
            // 如果存在返回值缓冲区
            if (Nd->RetBuffer) {
                genExpr(Nd);
                return;
            }
            break;
        case ND_VLA_PTR:
            // VLA的指针
            println("  # 生成VLA的指针");
            println("  li t0, %d", Nd->Var->Offset);
            println("  add a0, t0, fp");
            return;
        case ND_ASSIGN:
        case ND_COND:
            // 使结构体成员可以通过=或?:访问
            if (Nd->Ty->Kind == TY_STRUCT || Nd->Ty->Kind == TY_UNION) {
                genExpr(Nd);
                return;
            }
            break;
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
        if(Fn->Ty->Kind != TY_FUNC)
            continue;
        // 反向偏移量
        int ReOffset = 16;

        // 被调用函数将自己的ra、fp也压入栈了，
        // 所以fp+16才是上一级函数的sp顶
        // /             栈保存的N个变量            / N*8
        // /---------------本级函数----------------/ sp
        // /                 ra                  / sp-8
        // /                fp（上一级）           / fp = sp-16

        // 寄存器传递
        int GP = 0, FP = 0;
        // 寄存器传递的参数
        for (Obj *Var = Fn->Params; Var; Var = Var->Next) {
            Type *Ty = Var->Ty;
            switch (Ty->Kind){
                case TY_STRUCT:
                case TY_UNION:
                    setFloStMemsTy(&Ty, GP, FP);
                    // 计算浮点结构体所使用的寄存器
                    // 这里一定寄存器可用，所以不判定是否超过寄存器最大值
                    if (isSFloNum(Ty->FSReg1Ty) || isSFloNum(Ty->FSReg2Ty)) {
                        Type *Regs[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
                        for (int I = 0; I < 2; ++I) {
                            if (isSFloNum(Regs[I]))
                                FP++;
                            if (isInteger(Regs[I]))
                                GP++;
                        }
                        continue;
                    }

                    // 9～16字节的结构体要用两个寄存器
                    if (8 < Ty->Size && Ty->Size <= 16) {
                    // 如果只剩一个寄存器，那么剩余一半通过栈传递
                    if (GP == GP_MAX - 1)
                        Var->IsHalfByStack = true;
                    if (GP < GP_MAX)
                        GP++;
                    }
                    // 所有字节的结构体都在至少使用了一个寄存器（如果可用）
                    if (GP < GP_MAX) {
                        GP++;
                        continue;
                    }
                    // 没使用寄存器的需要栈传递
                    break;
                case TY_FLOAT:
                case TY_DOUBLE:
                    if (FP < FP_MAX) {
                        println(" #  FP%d传递浮点变量%s", FP, Var->Name);
                        FP++;
                        continue;
                    } 
                    else if (GP < GP_MAX) {
                        println(" #  GP%d传递浮点变量%s", GP, Var->Name);
                        GP++;
                        continue;
                    }
                    break;
                case TY_LDOUBLE:
                    if (GP == GP_MAX - 1) {
                        println(" #  GP%d传递一半浮点变量%s，另一半栈传递", GP, Var->Name);
                        Var->IsHalfByStack = true;
                        GP++;
                        break;
                    }
                    if (GP < GP_MAX - 1) {
                        println(" #  GP%d传递浮点变量%s", GP, Var->Name);
                        GP++;
                        GP++;
                        continue;
                    }
                    break;
                default:
                    if (GP < GP_MAX) {
                        println(" #  GP%d传递整型变量%s", GP, Var->Name);
                        GP++;
                        continue;
                    }
                    break;
            }
            // 栈传递
            // 对齐变量
            ReOffset = alignTo(ReOffset, 8);
            // 为栈传递变量赋一个偏移量，或者说是反向栈地址
            Var->Offset = ReOffset;
            // 栈传递变量计算反向偏移量
            ReOffset += Var->IsHalfByStack ? Var->Ty->Size - 8 : Var->Ty->Size;
            println(" #  栈传递变量%s偏移量%d", Var->Name, Var->Offset);

        }

        // 可变参数函数VaArea的偏移量
        if (Fn->VaArea) {
            ReOffset = alignTo(ReOffset, 8);
            Fn->VaArea->Offset = ReOffset;
        }

        int Offset = 0;
        // 读取所有变量
        for (Obj *Var = Fn->Locals; Var; Var = Var->Next) {
            // 栈传递的变量的直接跳过
            if (Var->Offset && !Var->IsHalfByStack)
                continue;

            // 数组超过16字节时，对齐值至少为16字节
            int Align = (Var->Ty->Kind == TY_ARRAY && Var->Ty->Size >= 16)
                            ? MAX(16, Var->Align)
                            : Var->Align;

            // the offset here is relevent to fp, which is at top of stack
            // 每个变量分配空间
            Offset += Var->Ty->Size;
            Offset = alignTo(Offset, Align);
            // 为每个变量赋一个偏移量，或者说是栈中地址
            Var->Offset = -Offset;
            println(" #  寄存器传递变量%s偏移量%d", Var->Name, Var->Offset);
//            println(" # %s, offset = %d", Var->Name, Var->Offset);
        }
        // 将栈对齐到16字节
        Fn->StackSize = alignTo(Offset, 16);
    }
}
// 复制结构体返回值到缓冲区中
static void copyRetBuffer(Obj *Var) {
    Type *Ty = Var->Ty;
    int GP = 0, FP = 0;

    setFloStMemsTy(&Ty, GP, FP);

    println("  # 拷贝到返回缓冲区");
    println("  # 加载struct地址到t0");
    println("  li t0, %d", Var->Offset);
    println("  add t1, fp, t0");

    // 处理浮点结构体的情况
    if (isSFloNum(Ty->FSReg1Ty) || isSFloNum(Ty->FSReg2Ty)) {
        int Off = 0;
        Type *RTys[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
        for (int I = 0; I < 2; ++I) {
            switch (RTys[I]->Kind) {
                case TY_FLOAT:
                    println("  fsw fa%d, %d(t1)", FP++, Off);
                    Off = 4;
                    break;
                case TY_DOUBLE:
                    println("  fsd fa%d, %d(t1)", FP++, Off);
                    Off = 8;
                    break;
                case TY_VOID:
                    break;
                default:
                    println("  sd a%d, %d(t1)", GP++, Off);
                    Off = 8;
                    break;
            }
        }
        return;
    }

    println("  # 复制整型结构体返回值到缓冲区中");
    for (int Off = 0; Off < Ty->Size; Off += 8) {
        switch (Ty->Size - Off) {
            case 1:
                println("  sb a%d, %d(t1)", GP++, Off);
                break;
            case 2:
                println("  sh a%d, %d(t1)", GP++, Off);
                break;
            case 3:
            case 4:
                println("  sw a%d, %d(t1)", GP++, Off);
                break;
            default:
                println("  sd a%d, %d(t1)", GP++, Off);
                break;
        }
    }
}

// 拷贝结构体的寄存器
static void copyStructReg(void) {
    Type *Ty = CurrentFn->Ty->ReturnTy;
    int GP = 0, FP = 0;

    println("  # 复制结构体寄存器");
    println("  # 读取寄存器，写入存有struct地址的0(t1)中");
    println("  mv t1, a0");

    setFloStMemsTy(&Ty, GP, FP);

    if (isSFloNum(Ty->FSReg1Ty) || isSFloNum(Ty->FSReg2Ty)) {
        int Off = 0;
        Type *RTys[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
        for (int I = 0; I < 2; ++I) {
            switch (RTys[I]->Kind) {
                case TY_FLOAT:
                    println("  flw fa%d, %d(t1)", FP++, Off);
                    Off = 4;
                    break;
                case TY_DOUBLE:
                    println("  fld fa%d, %d(t1)", FP++, Off);
                    Off = 8;
                    break;
                case TY_VOID:
                    break;
                default:
                    println("  ld a%d, %d(t1)", GP++, Off);
                    Off = 8;
                    break;
            }
        }
        return;
    }

    println("  # 复制返回的整型结构体的值");
    for (int Off = 0; Off < Ty->Size; Off += 8) {
        switch (Ty->Size - Off) {
            case 1:
                println("  lb a%d, %d(t1)", GP++, Off);
                break;
            case 2:
                println("  lh a%d, %d(t1)", GP++, Off);
            break;
            case 3:
            case 4:
                println("  lw a%d, %d(t1)", GP++, Off);
                break;
            default:
                println("  ld a%d, %d(t1)", GP++, Off);
                break;
        }
    }
}

// 大于16字节的结构体返回值，需要拷贝内存
static void copyStructMem(void) {
    Type *Ty = CurrentFn->Ty->ReturnTy;
    // 第一个参数，调用者的缓冲区指针
    Obj *Var = CurrentFn->Params;

    println("  # 复制大于16字节结构体内存");
    println("  # 将栈内struct地址存入t1，调用者的结构体的地址");
    println("  li t0, %d", Var->Offset);
    println("  add t0, fp, t0");
    println("  ld t1, 0(t0)");

    println("  # 遍历结构体并从a0位置复制所有字节到t1");
    for (int I = 0; I < Ty->Size; I++) {
        println("  lb t0, %d(a0)", I);
        println("  sb t0, %d(t1)", I);
    }
}

// 开辟Alloca空间
static void builtinAlloca(void) {
    // 对齐需要的空间t1到16字节
    //
    // 加上15，然后去除最低位的十六进制数
    println("  addi t1, t1, 15");
    println("  andi t1, t1, -16");

    // 注意t2与t1大小不定，仅为示例
    // ----------------------------- 旧sp（AllocaBottom所存的sp）
    // - - - - - - - - - - - - - - -
    //  需要在此开辟大小为t1的Alloca区域
    // - - - - - - - - - - - - - - -
    //            ↑
    //    t2（旧sp和新sp间的距离）
    //            ↓
    // ----------------------------- 新sp ← sp

    // 根据t1的值，提升临时区域
    //
    // 加载 旧sp 到t2中
    println("  li t0, %d", CurrentFn->AllocaBottom->Offset);
    println("  add t0, fp, t0");
    println("  ld t2, 0(t0)");
    // t2=旧sp-新sp，将二者的距离存入t2
    println("  sub t2, t2, sp");

    // 保存 新sp 存入a0
    println("  mv a0, sp");
    // 新sp 开辟（减去）所需要的空间数，结果存入 sp
    // 并将 新sp开辟空间后的栈顶 同时存入t3
    println("  sub sp, sp, t1");
    println("  mv t3, sp");

    // 注意t2与t1大小不定，仅为示例
    // ----------------------------- 旧sp（AllocaBottom所存的sp）
    //              ↑
    //      t2（旧sp和新sp间的距离）
    //              ↓
    // ----------------------------- 新sp  ← a0
    //              ↑
    //     t1（Alloca所需要的空间数）
    //              ↓
    // ----------------------------- 新新sp ← sp,t3

    // 将 新sp内（底部和顶部间的）数据，复制到 新sp的顶部之上
    println("1:");
    // t2为0时跳转到标签2，结束复制
    println("beqz t2, 2f");
    // 将 新sp底部 内容复制到 新sp顶部之上
    println("  lb t0, 0(a0)");
    println("  sb t0, 0(t3)");
    println("  addi a0, a0, 1");
    println("  addi t3, t3, 1");
    println("  addi t2, t2, -1");
    println("  j 1b");
    println("2:");

    // 注意t2与t1大小不定，仅为示例
    // ------------------------------ 旧sp   a0
    //             ↑                         ↓
    //       t1（Alloca区域）
    //             ↓
    // ------------------------------ 新sp ← a0
    //             ↑
    //  t2（旧sp和新sp间的内容，复制到此）
    //             ↓
    // ------------------------------ 新新sp ← sp

    // 移动alloca_bottom指针
    //
    // 加载 旧sp 到 a0
    println("  li t0, %d", CurrentFn->AllocaBottom->Offset);
    println("  add t0, fp, t0");
    println("  ld a0, 0(t0)");
    // 旧sp 减去开辟的空间 t1
    println("  sub a0, a0, t1");
    // 存储a0到alloca底部地址
    println("sd a0, 0(t0)");
}


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
                case TY_LDOUBLE:
                    println("  li t0, -1");
                    println("  slli t0, t0, 63");
                    println("  xor a%d, a%d, t0", LDSP + 1, LDSP + 1);
                    return;
                default:
                    // neg a0, a0是sub a0, x0, a0的别名, 即a0=0-a0
                    println("  neg%s a0, a0", Nd->Ty->Size <= 4 ? "w" : "");
                    return;
            }
        case ND_NUM: {
            switch (Nd->Ty->Kind) {
                case TY_FLOAT:{
                    // 将a0转换到float类型值为%f的fa0中
                    // can't do the cast directly like (uint32_t)Nd->FVal.
                    // if so, something like 0.999 will be truncated to 0.
                    // and we wish things like (_Bool)0.1l to be "true".
                    // we need to reinterpret the bits here
                    float f = Nd->FVal;
                    println("  li a0, %u  # float %Lf", *(uint32_t*)&f, Nd->FVal);
                    println("  fmv.w.x fa0, a0");
                    return;
                }
                case TY_DOUBLE:{
                    // 将a0转换到double类型值为%f的fa0中
                    double d = Nd->FVal;
                    println("  li a0, %lu  # double %Lf", *(uint64_t*)&d, Nd->FVal);
                    println("  fmv.d.x fa0, a0");
                    return;
                }
                case TY_LDOUBLE: {
#ifdef __riscv
                    union {
                        long double F128;
                        uint64_t U64[2];
                    } U;
                    memset(&U, 0, sizeof(U));
                    U.F128 = Nd->FVal;
                    println("  # 将long double类型的%Lf值，压入LD栈中", Nd->FVal);
                    println("  li a0, 0x%016lx  # long double %Lf", U.U64[0], Nd->FVal);
                    println("  fmv.d.x fs%d, a0", LDSP);

                    println("  li a0, 0x%016lx", U.U64[1]);
                    println("  fmv.d.x fs%d, a0", LDSP + 1);
                    LDSP += 2;
                    return;
#endif // __riscv
#ifdef __x86_64
                    // 【注意】交叉环境当中，x86_64的long double是f80而非f128
                    // 因而此处的支持仅供交叉测试，存在f80->f64的精度的丢失！
                    union {
                        double F64;
                        uint64_t U64;
                    } U = {Nd->FVal};
                    println("  # 【注意】此处存在f80->f64的精度丢失！！！");
                    println("  # 将long double类型的%Lf值，压入LD栈中", Nd->FVal);
                    println("  li a0, %lu  # double %Lf", U.U64, Nd->FVal);
                    println("  fmv.d.x fa0, a0");
                    println("  call __extenddftf2@plt");
                    pushLD();
                    return;
#endif // __x86_64
                }
                default:
                    println("  li a0, %ld", Nd->Val);
                    return;
            }
        }   // ND_NUM
        // 变量
        case ND_VAR:
            // 计算出变量的地址, 存入a0
            genAddr(Nd);
            // load a value from the generated address
            load(Nd->Ty);
            return;
        case ND_LABEL_VAL:
            println("  # 加载标签%s的值到a0中", Nd->UniqueLabel);
            println("  la a0, %s", Nd->UniqueLabel);
            return;
        // 成员变量
        case ND_MEMBER:{
            // 计算出成员变量的地址，然后存入a0
            genAddr(Nd);
            load(Nd->Ty);
            Member *Mem = Nd->Mem;
            if (Mem->IsBitfield) {
                println("  # 清除位域的成员变量（%d字节）未用到的位", Mem->BitWidth);
                // 清除位域成员变量未用到的高位
                println("  slli a0, a0, %d", 64 - Mem->BitWidth - Mem->BitOffset);
                // 清除位域成员变量未用到的低位
                if (Mem->Ty->IsUnsigned)
                    println("  srli a0, a0, %d", 64 - Mem->BitWidth);
                else
                    println("  srai a0, a0, %d", 64 - Mem->BitWidth);
            }
            return;
        }
        // 赋值
        case ND_ASSIGN:
            // 左部是左值，保存值到的地址
            genAddr(Nd->LHS);
            push();
            // 右部是右值，为表达式的值
            genExpr(Nd->RHS);

            // 如果是位域成员变量，需要先从内存中读取当前值，然后合并到新值中
            if (Nd->LHS->Kind == ND_MEMBER && Nd->LHS->Mem->IsBitfield) {
                println("\n  # 位域成员变量进行赋值↓");
                println("  # 备份需要赋的a0值");
                println("  mv t2, a0");

                println("  # 计算位域成员变量的新值：");
                Member *Mem = Nd->LHS->Mem;
                // 将需要赋的值a0存入t1
                println("  mv t1, a0");
                // 构造一个和位域成员长度相同，全为1的二进制数
                println("  li t0, %ld", (1L << Mem->BitWidth) - 1);
                // 取交之后，位域长度的低位，存储了我们需要的值，其他位都为0
                println("  and t1, t1, t0");
                // 然后将该值左移，相应的位偏移量中
                // 此时我们所需要的位域数值已经处于正确的位置，且其他位置都为0
                println("  slli t1, t1, %d", Mem->BitOffset);

                println("  # 读取位域当前值：");
                // 将位域值保存的地址加载进来
                println("  ld a0, 0(sp)");
                // 读取该地址的值
                load(Mem->Ty);

                println("  # 写入成员变量新值到位域当前值中：");
                // 位域值对应的掩码，即t1需要写入的位置
                // 掩码位都为1，其余位为0
                long Mask = ((1L << Mem->BitWidth) - 1) << Mem->BitOffset;
                // 对掩码取反，此时，其余位都为1，掩码位都为0
                println("  li t0, %ld", ~Mask);
                // 取交，保留除掩码位外所有的位
                println("  and a0, a0, t0");
                // 取或，将成员变量的新值写入到掩码位
                println("  or a0, a0, t1");

                store(Nd->Ty);
                println("  # 恢复需要赋的a0值作为返回值");
                println("  mv a0, t2");
                println("  # 完成位域成员变量的赋值↑\n");
                return;
            }
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
            // 对alloca函数进行处理
            if (Nd->LHS->Kind == ND_VAR && !strcmp(Nd->LHS->Var->Name, "alloca")) {
                // 解析alloca函数的参数，确定开辟空间的字节数
                genExpr(Nd->Args);
                // 将需要的字节数存入t1
                println("  mv t1, a0");
                // 生成Alloca函数汇编
                builtinAlloca();
                return;
            }
            // 计算所有参数的值，正向压栈
            // 此处获取到栈传递参数的数量
            int StackArgs = pushArgs(Nd);
            // 将a0的值(fn address)存入t5
            // LHS is an ident(ND_VAR), genExpr
            // will get that ident's address
            genExpr(Nd->LHS);
            println("  mv t5, a0");
            // 反向弹栈，a0->参数1，a1->参数2...
            int GP = 0, FP = 0;
            if (Nd->RetBuffer && Nd->Ty->Size > 16) {
                println("  # 返回结构体大于16字节，那么第一个参数指向返回缓冲区");
                pop(GP++);
            }

            // 读取函数形参中的参数类型
            Type *CurArg = Nd->FuncType->Params;
            for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
                // 如果是可变参数函数
                // 匹配到空参数（最后一个）的时候，将剩余的整型寄存器弹栈
                if (Nd->FuncType->IsVariadic && CurArg == NULL) {
                    if (GP < GP_MAX) {
                        if (Arg->Ty->Kind == TY_LDOUBLE) {
                            // 在可变参数函数的调用中
                            // LD的第一个寄存器必须是偶数下标，即a0,a2,a4,a6
                            if (GP % 2 == 1)
                                GP++;
                            println("  # long double通过a%d,a%d传递可变实参", GP, GP + 1);
                            pop(GP++);
                            if (GP < GP_MAX)
                                pop(GP++);
                        } else {
                            println("  # a%d传递可变实参", GP);
                            pop(GP++);
                        }
                    }
                    continue;
                }

                CurArg = CurArg->Next;
                Type *Ty = Arg->Ty;
                switch (Ty->Kind) {
                    case TY_STRUCT:
                    case TY_UNION: {
                        // 判断结构体的类型
                        // 结构体的大小
                        int Sz = Ty->Size;
                        // 处理一或两个浮点成员变量的结构体
                        if (isSFloNum(Ty->FSReg1Ty) || isSFloNum(Ty->FSReg2Ty)) {
                            Type *Regs[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
                            for (int I = 0; I < 2; ++I) {
                                if (Regs[I]->Kind == TY_FLOAT) {
                                    println("  # %d字节float结构体%d通过fa%d传递", Sz, I, FP);
                                    println("  # 弹栈，将栈顶的值存入fa%d", FP);
                                    println("  flw fa%d, 0(sp)", FP++);
                                    println("  addi sp, sp, 8");
                                    Depth--;
                                }
                                if (Regs[I]->Kind == TY_DOUBLE) {
                                    println("  # %d字节double结构体%d通过fa%d传递", Sz, I, FP);
                                    popF(FP++);
                                }
                                if (isInteger(Regs[I])) {
                                    println("  # %d字节浮点结构体%d通过a%d传递", Sz, I, GP);
                                    pop(GP++);
                                }
                            }
                            break;
                        }

                        // 其他整型结构体或多字节结构体
                        // 9~16字节整型结构体用两个寄存器，其他字节结构体用一个结构体
                        int Regs = (8 < Sz && Sz <= 16) ? 2 : 1;
                        for (int I = 1; I <= Regs; ++I) {
                            if (GP < GP_MAX) {
                                println("  # %d字节的整型结构体%d通过a%d传递", Sz, I, GP);
                                pop(GP++);
                            }
                        }
                        break;
                    }
                    case TY_FLOAT:
                    case TY_DOUBLE:
                        if (FP < FP_MAX) {
                            println("  # fa%d传递浮点参数", FP);
                            popF(FP++);
                        } else if (GP < GP_MAX) {
                            println("  # a%d传递浮点参数", GP);
                            pop(GP++);
                        }
                        break;
                    case TY_LDOUBLE:
                        if (GP == GP_MAX - 1) {
                            println("  # a%d传递LD一半参数", GP);
                            pop(GP++);
                        }
                        if (GP < GP_MAX - 1) {
                            println("  # a%d传递long double第%d部分参数", GP, 1);
                            pop(GP++);
                            pop(GP++);
                        }
                        break;
                    default:
                        if (GP < GP_MAX) {
                            println("  # a%d传递整型参数", GP);
                            pop(GP++);
                        }
                        break;
                }
            }
            // 调用函数
            println("  # 调用函数");
            println("  jalr t5");

            if (Nd->Ty->Kind == TY_LDOUBLE) {
                println("  # 保存Long double类型函数的返回值");
                pushLD();
            }

            // 回收为栈传递的变量开辟的栈空间
            if (StackArgs) {
                // 栈的深度减去栈传递参数的字节数
                Depth -= StackArgs;
                println("  # 回收栈传递参数的%d个字节", StackArgs * 8);
                println("  addi sp, sp, %d", StackArgs * 8);
                // 清除记录的大结构体的数量
                BSDepth = 0;
            }
            // 清除寄存器中高位无关的数据
            switch (Nd->Ty->Kind) {
                case TY_BOOL:
                    println("  # 清除bool类型的高位");
                    println("  slli a0, a0, 63");
                    println("  srli a0, a0, 63");
                    return;
                case TY_CHAR:
                    println("  # 清除char类型的高位");
                    if (Nd->Ty->IsUnsigned) {
                        println("  slli a0, a0, 56");
                        println("  srli a0, a0, 56");
                    } else {
                        println("  slli a0, a0, 56");
                        println("  srai a0, a0, 56");
                    }
                    return;
                case TY_SHORT:
                    println("  # 清除short类型的高位");
                    if (Nd->Ty->IsUnsigned) {
                        println("  slli a0, a0, 48");
                        println("  srli a0, a0, 48");
                    } else {
                        println("  slli a0, a0, 48");
                        println("  srai a0, a0, 48");
                    }
                    return;
                default:    break;
            }
            // 如果返回的结构体小于16字节，直接使用寄存器返回
            if (Nd->RetBuffer && Nd->Ty->Size <= 16) {
                copyRetBuffer(Nd->RetBuffer);
                println("  li t0, %d", Nd->RetBuffer->Offset);
                println("  add a0, fp, t0");
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
        case ND_CAS: {
            println("# =====原子比较交换===============");
            // 当 t1地址中的值t0 与 t3旧值 相同时，将 t1地址中的值 替换为 t4新值
            // 若不同时，将 地址中的值t0 替换掉 旧值
            genExpr(Nd->CasAddr);
            println("  mv t1, a0"); // t1地址
            genExpr(Nd->CasOld);
            println("  mv t2, a0"); // t2旧值地址
            load(Nd->CasOld->Ty->Base);
            println("  mv t3, a0"); // t3旧值
            genExpr(Nd->CasNew);
            println("  mv t4, a0"); // t4新值

            // fence用于控制设备、内存的读写顺序
            // iorw：之前的设备输入输出、内存读写指令，不能晚于fence指令
            // ow：之后的设备输出、内存写的指令，不能早于fence指令
            println("  fence iorw, ow");
            println("1:");
            // 加载地址中的值到t0
            // lr（Load-Reserved）：加载并保留对该内存地址的控制权
            // aq（acquisition）：若设置了aq位，
            // 则此硬件线程中在AMO（原子内存操作）之后的任何内存操作，都不会在AMO之前发生
            println("  lr.w.aq t0, (t1)");
            // 地址的值和旧值比较，若不等则退出
            println("  bne t0, t3, 2f");
            // 写入新值到地址
            // sc（Store-Conditional）：将寄存器中的值写入指定内存地址。
            // 写入操作只有在该内存地址仍然被处理器保留时才会生效。
            println("  sc.w.aq a0, t4, (t1)");
            // 不为0时，写入失败，重新写入
            println("  bnez a0, 1b");

            println("2:");
            // t0地址中的值 减去 t3旧值，将 差值 存入 t3
            println("  subw t3, t0, t3");
            // 判断差值t3，t3为0时 返回值a0为1，t3不为0时 返回值a0为0
            println("  seqz a0, t3");
            // 判断差值t3，t3为0时跳转到最后
            println("  beqz t3, 3f");
            // 若不同时，将 地址中的值t0 写入 t2旧值的地址，替换掉 旧值
            println("  sw t0, (t2)");
            println("3:");
            return;
        }
        case ND_EXCH: {
            genExpr(Nd->LHS);
            push();
            genExpr(Nd->RHS);
            pop(1);

            int Sz = Nd->LHS->Ty->Base->Size;
            char *S = (Sz <= 4) ? "w" : "d";
            println("  # 原子交换");
            println("  amoswap.%s.aq a0, a0, (a1)", S);
            return;
        }

        default:
            break;
    }

    // 处理浮点类型
    switch (Nd->LHS->Ty->Kind) {
        case TY_FLOAT:
        case TY_DOUBLE: {
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
        case TY_LDOUBLE: {
            genExpr(Nd->LHS);
            genExpr(Nd->RHS);

            popLD(2);
            popLD(0);
            switch (Nd->Kind) {
                case ND_ADD:
                    println("  # long double加法，从栈顶读取32个字节");
                    println("  call __addtf3@plt");
                    pushLD();
                    return;
                case ND_SUB:
                    println("  # long double减法，从栈顶读取32个字节");
                    println("  call __subtf3@plt");
                    pushLD();
                    return;
                case ND_MUL:
                    println("  # long double乘法，从栈顶读取32个字节");
                    println("  call __multf3@plt");
                    pushLD();
                    return;
                case ND_DIV:
                    println("  # long double除法，从栈顶读取32个字节");
                    println("  call __divtf3@plt");
                    pushLD();
                    return;
                case ND_EQ:
                    println("  # long double相等，从栈顶读取32个字节");
                    println("  call __eqtf2@plt");
                    println("  seqz a0, a0");
                    return;
                case ND_NE:
                    println("  # long double不等，从栈顶读取32个字节");
                    println("  call __netf2@plt");
                    println("  snez a0, a0");
                    return;
                case ND_LT:
                    println("  # long double小于，从栈顶读取32个字节");
                    println("  call __lttf2@plt");
                    println("  slti a0, a0, 0");
                    return;
                case ND_LE:
                    println("  # long double小于等于，从栈顶读取32个字节");
                    println("  call __letf2@plt");
                    println("  slti a0, a0, 1");
                    return;
                default:
                    errorTok(Nd->Tok, "invalid expression");
            }
        }
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
        case ND_ASM:
            println("  # 插入的ASM代码片段");
            println("  %s", Nd->AsmStr);
            return;
        case ND_RETURN:
            if(Nd->LHS){
                genExpr(Nd->LHS);
                Type *Ty = Nd->LHS->Ty;
                // 处理结构体作为返回值的情况
                if (Ty->Kind == TY_STRUCT || Ty->Kind == TY_UNION) {
                    if (Ty->Size <= 16)
                        // 小于16字节拷贝寄存器
                        copyStructReg();
                    else
                        // 大于16字节拷贝内存
                        copyStructMem();
                }
                if (Ty->Kind == TY_LDOUBLE) {
                    println("  # LD类型作为返回值时，需要将LD栈顶元素拷贝到a0，a1中");
                    popLD(0);
                }
            }
            // 无条件跳转语句，跳转到.L.return.%s段
            // j offset是 jal x0, offset的别名指令
            println("  j .L.return.%s", CurrentFn->Name);
            return;
        case ND_GOTO_EXPR:
            println("  # GOTO跳转到存储标签的地址");
            genExpr(Nd->LHS);
            println("  jr a0");
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
                // 常规case，case范围前后一致
                if (N->Begin == N->End) {
                    println("  li t0, %ld", N->Begin);
                    println("  beq a0, t0, %s", N->Label);
                    continue;
                }

                println("  # 处理case范围值：%ld...%ld", N->Begin, N->End);
                // a0为当前switch中的值
                println("  mv t1, a0");
                println("  li t0, %ld", N->Begin);
                // t1存储了a0-Begin的值
                println("  sub t1, t1, t0");
                // t2存储了End-Begin的值
                println("  li t2, %ld", N->End - N->Begin);
                // 如果0<=t1<=t2，那么就说明在范围内
                println("  bleu t1, t2, %s", N->Label);
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

        // 数组超过16字节时，对齐值至少为16字节
        int Align = (Var->Ty->Kind == TY_ARRAY && Var->Ty->Size >= 16)
                        ? MAX(16, Var->Align)
                        : Var->Align;

        // 为试探性的全局变量生成指示
        if (OptFCommon && Var->IsTentative) {
            println("  .comm %s, %d, %d", Var->Name, Var->Ty->Size, Align);
            continue;
        }

        // .data 或 .tdata 段
        if (Var -> InitData){
            if (Var->IsTLS) {
                // a：可加载执行
                // w：可写
                // T：线程局部的
                // progbits：包含程序数据
                println("  .section .tdata,\"awT\",@progbits");
            } else {
                println("  .data");
            }

            println("  .type %s, @object", Var->Name);
            println("  .size %s, %d", Var->Name, Var->Ty->Size);
            println("  .align %d", simpleLog2(Align));

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
                    println("  .quad %s%+ld", *Rel->Label, Rel->Addend);
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
            // .bss 或 .tbss 段
            if (Var->IsTLS) {
                // nobits：不含数据
                println("\n  # TLS未初始化的全局变量");
                println("  .section .tbss,\"awT\",@nobits");
            } else {
                println("\n  # 未初始化的全局变量");
                println("  .bss");
            }

            println("  .align %d", simpleLog2(Align));
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

        // 如果未存活，就不生成static inline函数
        if (!Fn->IsLive)
        continue;

        char *visibility = Fn->IsStatic? ".local": ".global";
        println("  %s %s", visibility, Fn->Name);

        println("  .text");
        println("# =====%s段开始===============", Fn->Name);
        println("  .type %s, @function", Fn->Name);
        println("%s:", Fn->Name);
        CurrentFn = Fn;

        // 栈布局
        // ------------------------------//
        //        上一级函数的栈传递参数
        // ==============================// sp（本级函数）
        //         VaArea(寄存器可用时)
        // ------------------------------// sp = sp（本级函数）-VaArea
        //              ra
        //-------------------------------// ra = sp-8
        //              fp
        //-------------------------------// fp = sp-16
        //             变量
        //-------------------------------// sp = sp-16-StackSize
        //           表达式计算
        //-------------------------------//

        // Prologue, 前言
        // 为剩余的整型寄存器开辟空间，用于存储可变参数
        int VaSize = 0;
        if (Fn->VaArea) {
            // 遍历正常参数所使用的浮点、整型寄存器
            int GPs = 0, FPs = 0;
            // 可变参数函数，非可变的参数使用寄存器
            for (Obj *Var = Fn->Params; Var; Var = Var->Next) {
                // 计算所有类型所使用的寄存器数量
                Type *Ty = Var->Ty;
                switch (Ty->Kind) {
                    case TY_STRUCT:
                    case TY_UNION:
                        // 对寄存器传递的参数
                        if (isSFloNum(Ty->FSReg1Ty) || isSFloNum(Ty->FSReg2Ty)) {
                            // 浮点结构体的第一部分
                            isSFloNum(Ty->FSReg1Ty) ? FPs++ : GPs++;
                            // 浮点结构体的第二部分
                            if (Ty->FSReg2Ty->Kind != TY_VOID)
                                isSFloNum(Ty->FSReg2Ty) ? FPs++ : GPs++;
                            break;
                        }

                        // 小于8字节的结构体、大于16字节的结构体
                        // 一半寄存器，一半栈传递的结构体
                        if (Ty->Size < 8 || Ty->Size > 16 || Var->IsHalfByStack)
                            GPs++;
                        // 处理大于8字节，小于16字节的结构体
                        else
                            GPs += 2;
                        break;
                    case TY_FLOAT:
                    case TY_DOUBLE:
                        // 可变参数函数中的浮点参数
                        FPs < FP_MAX ? FPs++ : GPs++;
                        break;
                    default:
                        // 可变参数函数中的整型参数
                        GPs++;
                        break;
                }
            }

            // 需确保使用到了整形寄存器，否则不开辟此空间
            if (GPs < GP_MAX) {
                VaSize = (8 - GPs) * 8;
                println("  # VaArea的区域，大小为%d", VaSize);
                println("  addi sp, sp, -%d", VaSize);
            }
        }
        // 将ra寄存器压栈,保存ra的值
        println("  addi sp, sp, -16");
        println("  sd ra, 8(sp)");
        // 将fp压入栈中，保存fp的值
        println("  sd fp, 0(sp)");
        // 将sp写入fp
        println("  mv fp, sp");

        println("  # 保存所有的fs0~fs11寄存器");
        for (int I = 0; I <= 11; ++I)
            println("  fsgnj.d ft%d, fs%d, fs%d", I, I, I);

        // 偏移量为实际变量所用的栈大小
        if(isLegalImmI(Fn->StackSize))
            println("  addi sp, sp, -%d", Fn->StackSize);
        else{
            println("  li t0, -%d", Fn->StackSize);
            println("  add sp, sp, t0");
        }

        // Alloca函数
        println("  # 将当前的sp值，存入到Alloca区域的底部");
        println("  li t0, %d", Fn->AllocaBottom->Offset);
        println("  add t0, t0, fp");
        println("  sd sp, 0(t0)");
        // map (actual params) -> (formal params)
        // this needs to be done before entering the fn body
        // then in the fn body we can use its formal params
        // in stack as if they were passed from outside

        // 记录整型寄存器，浮点寄存器使用的数量
        int GP = 0, FP = 0;
        for (Obj *Var = Fn->Params; Var; Var = Var->Next) {
            // 不处理栈传递的形参，栈传递一半的结构体除外
            if (Var->Offset > 0 && !Var->IsHalfByStack)
                continue;
            Type *Ty = Var->Ty;

            // 正常传递的形参
            switch (Ty->Kind) {
                case TY_STRUCT:
                case TY_UNION:
                    println("  # 对寄存器传递的结构体进行压栈");
                    // 处理浮点结构体
                    if (isSFloNum(Ty->FSReg1Ty) || isSFloNum(Ty->FSReg2Ty)) {
                        println("  # 浮点结构体的第一部分进行压栈");
                        // 浮点结构体的第一部分，偏移量为0
                        int Sz1 = Var->Ty->FSReg1Ty->Size;
                        if (isSFloNum(Ty->FSReg1Ty))
                            storeFloat(FP++, Var->Offset, Sz1);
                        else
                            storeGeneral(GP++, Var->Offset, Sz1);

                        // 浮点结构体的第二部分
                        if (Ty->FSReg2Ty->Kind != TY_VOID) {
                            println("  # 浮点结构体的第二部分进行压栈");
                            int Sz2 = Ty->FSReg2Ty->Size;
                            // 结构体内偏移量为两个成员间的最大尺寸
                            int Off = MAX(Sz1, Sz2);

                            if (isSFloNum(Ty->FSReg2Ty))
                                storeFloat(FP++, Var->Offset + Off, Sz2);
                            else
                                storeGeneral(GP++, Var->Offset + Off, Sz2);
                        }
                        break;
                    }

                    // 大于16字节的结构体参数，通过访问它的地址，
                    // 将原来位置的结构体复制到栈中
                    if (Ty->Size > 16) {
                        println("  # 大于16字节的结构体进行压栈");
                        storeStruct(GP++, Var->Offset, Ty->Size);
                        break;
                    }

                    // 一半寄存器、一半栈传递的结构体
                    if (Var->IsHalfByStack) {
                        println("  # 一半寄存器、一半栈传递结构体进行压栈");
                        storeGeneral(GP++, Var->Offset, 8);
                        // 拷贝栈传递的一半结构体到当前栈中
                        for (int I = 0; I != Var->Ty->Size - 8; ++I) {
                            println("  lb t0, %d(fp)", 16 + I);
                            println("  li t1, %d", Var->Offset + 8 + I);
                            println("  add t1, fp, t1");
                            println("  sb t0, 0(t1)");
                        }
                        break;
                    }
                    // 处理小于16字节的结构体
                    if (Ty->Size <= 16)
                        storeGeneral(GP++, Var->Offset, MIN(8, Ty->Size));
                    if (Ty->Size > 8)
                        storeGeneral(GP++, Var->Offset + 8, Ty->Size - 8);
                    break;

                case TY_FLOAT:
                case TY_DOUBLE:
                    // 正常传递的浮点形参
                    if (FP < FP_MAX) {
                        println("  # 将浮点形参%s的寄存器fa%d的值压栈", Var->Name, FP);
                        storeFloat(FP++, Var->Offset, Var->Ty->Size);
                    } else {
                        println("  # 将浮点形参%s的寄存器a%d的值压栈", Var->Name, GP);
                        storeGeneral(GP++, Var->Offset, Var->Ty->Size);
                    }
                    break;
                case TY_LDOUBLE:
                    if (Var->IsHalfByStack) {
                        println("  # 将LD形参%s的第一部分a%d的值压栈", Var->Name, GP);
                        println("  ld t0, 16(fp)");
                        println("  sd t0, %d(fp)", Var->Offset + 8);
                        break;
                    }
                    if (GP < GP_MAX - 1) {
                        println("  # 将LD形参%s的第一部分a%d的值压栈", Var->Name, GP);
                        storeGeneral(GP++, Var->Offset, 8);
                        println("  # 将LD形参%s的第二部分a%d的值压栈", Var->Name, GP);
                        storeGeneral(GP++, Var->Offset + 8, 8);
                    }
                    break;
                default:
                    // 正常传递的整型形参
                    println("  # 将整型形参%s的寄存器a%d的值压栈", Var->Name, GP);
                    storeGeneral(GP++, Var->Offset, Var->Ty->Size);
                    break;
            }
        }
        // 可变参数
        if (Fn->VaArea) {
            // 可变参数位置位于本函数的最上方，即sp的位置，也就是fp+16
            int Offset = Fn->VaArea->Offset;
            println("  # 可变参数VaArea的偏移量为%d", Fn->VaArea->Offset);
            while (GP < 8) {
                storeGeneral(GP++, Offset, 8);
                Offset += 8;
            }
        }

        // 生成语句链表的代码
        println("# =====%s段主体===============", Fn->Name);
        genStmt(Fn->Body);
        Assert(Depth == 0, "depth = %d", Depth);

        // main默认返回0
        if (strcmp(Fn->Name, "main") == 0)
            println("  li a0, 0");

        // Epilogue，后语
        // 输出return段标签
        println("# =====%s段结束===============", Fn->Name);
        println(".L.return.%s:", Fn->Name);

        println("  # 恢复所有的fs0~fs11寄存器");
        for (int I = 0; I <= 11; ++I)
            println("  fsgnj.d fs%d, ft%d, ft%d", I, I, I);

        // 将fp的值改写回sp
        println("  mv sp, fp");
        // 将最早fp保存的值弹栈，恢复fp。
        println("  ld fp, 0(sp)");
        // 将ra寄存器弹栈,恢复ra的值
        println("  ld ra, 8(sp)");
        println("  addi sp, sp, 16");



        // 归还可变参数寄存器压栈的那一部分
        if (Fn->VaArea && VaSize > 0) {
            println("  # 归还VaArea的区域，大小为%d", VaSize);
            println("  addi sp, sp, %d", VaSize);
        }

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