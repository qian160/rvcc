#include "rvcc.h"
// 注意 ~ 应替换为具体的 /home/用户名 的路径
static char *RVPath = "/home/s081/riscv";
// 目标文件的路径
static char *OptO;
// 输入文件名
char *BaseFile;
// 输出文件名
static char *OutputFile;
// 输入文件区, default = "-"
static StringArray InputPaths;
// 引入路径区
StringArray IncludePaths;
// cc1选项
static bool OptCC1;
// ###选项
// print link info
static bool OptHashHashHash;
// -S选项
static bool OptS;
// -c选项
static bool OptC;
// -E选项
static bool OptE;
// -v选项
static bool OptV;
// -W选项
bool OptW;
// common块默认生成
bool OptFCommon = true;

extern const char logo[];   // don't use char *

// 临时文件区
static StringArray TmpFiles;

// 输出程序的使用说明
static void usage(int Status) {
    fprintf(stderr, "\33[1;98m" "usage: rvcc [ -o <path> ] <file> 🙂\n" "\33[3m");

    fprintf(stderr, "\33[1;92m\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "--help Display this information.\n");
    fprintf(stderr, "-E     Expand only.\n");
    fprintf(stderr, "-W     Print warning info(with some bugs maybe...).\n");
    fprintf(stderr, "-I     Add include path.\n");
    fprintf(stderr, "-c     Compile and assemble(.o).\n");
    fprintf(stderr, "-S     Compile only(.S).\n");
    fprintf(stderr, "-o     Specify the output file's name, default a.out.\n");
    fprintf(stderr, "-D     Define a macro.\n");
    fprintf(stderr, "-U     Undefine a macro.\n");
    fprintf(stderr, "-v     Display the programs invoked by the compiler.(not supported yet...)\n");
    fprintf(stderr, "-###   Like -v but options quoted and commands not executed.\n");
    fprintf(stderr, "\33[0m");

    exit(Status);
}

static void version() {
    char *str = format("\33[1;38m" "[ %s - %s] rvcc v1.14514 " "\33[0m \n", __DATE__, __TIME__);
/*
    char *hello = format(
        "%s%s%s%s%s%s%s%s%s%s",
        color_text("H", 31),
        color_text("e", 32),
        color_text("l", 33),
        color_text("l", 34),
        color_text("o ",35),
        color_text("W", 36),
        color_text("o", 37),
        color_text("r", 90),
        color_text("l", 91),
        color_text("d", 92)
    );
    fprintf(stderr, "%s\n", hello);
*/
    fprintf(stderr, "%s", logo);
    fprintf(stderr, "%s", str);
    exit(0);
}

// 增加默认引入路径
static void addDefaultIncludePaths(char *Argv0) {
    // rvcc特定的引入文件被安装到了argv[0]的./include位置
    strArrayPush(&IncludePaths, format("%s/include", dirname(strdup(Argv0))));

    // 支持标准的引入路径
    strArrayPush(&IncludePaths, "/usr/local/include");
    strArrayPush(&IncludePaths, "/usr/include/riscv64-linux-gnu");
    strArrayPush(&IncludePaths, "/usr/include");
}

// 判断需要一个参数的选项，是否具有一个参数
static bool takeArg(char *Arg) {
    char *X[] = {"-o", "-I", "-idirafter"};

    for (int I = 0; I < sizeof(X) / sizeof(*X); I++)
        if (!strcmp(Arg, X[I]))
            return true;
    return false;
}

// 解析传入程序的参数
static void parseArgs(int Argc, char **Argv) {
    // 确保需要一个参数的选项，存在一个参数
    for (int I = 1; I < Argc; I++)
        // 如果需要一个参数
        if (takeArg(Argv[I]))
            // 如果不存在一个参数，则打印出使用说明
            if (!Argv[++I])
                usage(1);

    // 存储-idirafter的路径参数
    StringArray Idirafter = {};
    // 遍历所有传入程序的参数
    for (int I = 1; I < Argc; I++) {
        // 如果存在help，则直接显示用法说明
        if (!strcmp(Argv[I], "--help"))
            usage(0);

        if (!strcmp(Argv[I], "--version"))
            version();

        // 解析-o XXX的参数
        if (!strcmp(Argv[I], "-o")) {
            OptO = Argv[++I];
            continue;
        }

        // 解析-oXXX的参数
        if (!strncmp(Argv[I], "-o", 2)) {
            // 目标文件的路径
            OptO = Argv[I] + 2;
            continue;
        }

        // 解析-###
        if (!strcmp(Argv[I], "-###")) {
            OptHashHashHash = true;
            continue;
        }

        // 解析-cc1
        if (!strcmp(Argv[I], "-cc1")) {
            OptCC1 = true;
            continue;
        }

        // 解析-S
        if (!strcmp(Argv[I], "-S")) {
            OptS = true;
            continue;
        }

        // 解析-S
        if (!strcmp(Argv[I], "-v")) {
            OptV = true;
            continue;
        }

        // 解析-c
        if (!strcmp(Argv[I], "-c")) {
            OptC = true;
            continue;
        }

        // 解析-E
        if (!strcmp(Argv[I], "-E")) {
            OptE = true;
            continue;
        }

        // 解析-W
        if (!strcmp(Argv[I], "-W")) {
            OptW = true;
            continue;
        }

        // 解析-cc1-input
        if (!strcmp(Argv[I], "-cc1-input")) {
            BaseFile = Argv[++I];
            continue;
        }

        // 解析-cc1-output
        if (!strcmp(Argv[I], "-cc1-output")) {
            OutputFile = Argv[++I];
            continue;
        }

        // 解析-Dxxx
        if (!strcmp(Argv[I], "-D")) {
            define(Argv[++I]);
            continue;
        }

        // 解析-D xxx
        if (!strncmp(Argv[I], "-D", 2)) {
            define(Argv[I] + 2);
            continue;
        }

        // 解析-U
        if (!strcmp(Argv[I], "-U")) {
            undefine(Argv[++I]);
            continue;
        }

        // 解析-U
        if (!strncmp(Argv[I], "-U", 2)) {
            undefine(Argv[I] + 2);
            continue;
        }

        // 解析-I
        if (!strncmp(Argv[I], "-I", 2)) {
            strArrayPush(&IncludePaths, Argv[I] + 2);
            continue;
        }

        // 解析-idirafter
        // 将参数存入Idirafter
        if (!strcmp(Argv[I], "-idirafter")) {
            strArrayPush(&Idirafter, Argv[I++]);
            continue;
        }

        // // 解析-fcommon
        if (!strcmp(Argv[I], "-fcommon")) {
            OptFCommon = true;
            continue;
        }

        // 解析-fno-common
        if (!strcmp(Argv[I], "-fno-common")) {
            OptFCommon = false;
            continue;
        }

        // 忽略多个选项
        if (!strncmp(Argv[I], "-O", 2) || !strncmp(Argv[I], "-W", 2) ||
            !strncmp(Argv[I], "-g", 2) || !strncmp(Argv[I], "-std=", 5) ||
            !strcmp(Argv[I], "-ffreestanding") ||
            !strcmp(Argv[I], "-fno-builtin") ||
            !strcmp(Argv[I], "-fno-omit-frame-pointer") ||
            !strcmp(Argv[I], "-fno-stack-protector") ||
            !strcmp(Argv[I], "-fno-strict-aliasing") || !strcmp(Argv[I], "-m64") ||
            !strcmp(Argv[I], "-mno-red-zone") || !strcmp(Argv[I], "-w") ||
            !strcmp(Argv[I], "-march=native"))
        continue;

        // 解析为-的参数
        if (Argv[I][0] == '-' && Argv[I][1] != '\0')
            error("unknown argument: %s", Argv[I]);

        // 其他情况则匹配为输入文件
        strArrayPush(&InputPaths, Argv[I]);
    }

    // 将所用Idirafter内的路径，都存入引用路径区中
    for (int I = 0; I < Idirafter.Len; I++)
        strArrayPush(&IncludePaths, Idirafter.Data[I]);

    // 不存在输入文件时报错
    if (InputPaths.Len == 0 && !BaseFile)
        error("no input files");
}

// 打开需要写入的文件
static FILE *openFile(char *Path) {
    if (!Path || strcmp(Path, "-") == 0)
        return stdout;

    // 以写入模式打开文件
    FILE *Out = fopen(Path, "w");
    if (!Out)
        error("cannot open output file: %s: %s", Path, strerror(errno));
    return Out;
}

// 当指定-E选项时，打印出所有终结符
// for debug use, I remove the "static" keyword
void printTokens(Token *Tok) {
    // 输出文件，默认为stdout
    FILE *Out = openFile(OptO ? OptO : "-");

    // 记录行数
    int Line = 1;
    // 遍历读取终结符
    for (; Tok->Kind != TK_EOF; Tok = Tok->Next) {
        // 位于行首打印出换行符
        if (Line > 1 && Tok->AtBOL)
            fprintf(Out, "\n");
        // 打印出需要空格的位置
        if (Tok->HasSpace && !Tok->AtBOL)
            fprintf(Out, " ");
        // 打印出终结符
        fprintf(Out, "%.*s", Tok->Len, Tok->Loc);
        Line++;
    }
    // 文件以换行符结尾
    fprintf(Out, "\n");
}


// 查找文件
static char *findFile(char *Pattern) {
    char *Path = NULL;
    // Linux文件系统中路径名称的模式匹配
    glob_t Buf = {};
    // 参数：用来模式匹配的路径，标记（例如是否排序结果），错误处理函数，结果存放缓冲区
    glob(Pattern, 0, NULL, &Buf);
    // gl_pathc匹配到的路径计数
    // 复制最后的一条匹配结果到Path中
    if (Buf.gl_pathc > 0)
        Path = strdup(Buf.gl_pathv[Buf.gl_pathc - 1]);
    // 释放内存
    globfree(&Buf);
    return Path;
}


// 文件存在时，为真
bool fileExists(char *Path) {
    struct stat St;
    return !stat(Path, &St);
}

// 查找库路径
static char *findLibPath(void) {
    // crt = C runtime. i stands for initialization
    if (fileExists("/usr/lib/riscv64-linux-gnu/crti.o"))
        return "/usr/lib/riscv64-linux-gnu";
    if (fileExists("/usr/lib64/crti.o"))
        return "/usr/lib64";
    if (fileExists(format("%s/sysroot/usr/lib/crti.o", RVPath)))
        return format("%s/sysroot/usr/lib/", RVPath);

    error("library path is not found");
    return NULL;
}

// 查找gcc库路径
static char *findGCCLibPath(void) {
    char *paths[] = {
        "/usr/lib/gcc/riscv64-linux-gnu/*/crtbegin.o",
        // Gentoo
        "/usr/lib/gcc/riscv64-pc-linux-gnu/*/crtbegin.o",
        // Fedora
        "/usr/lib/gcc/riscv64-redhat-linux/*/crtbegin.o",
        // 交叉编译
        format("%s/lib/gcc/riscv64-unknown-linux-gnu/*/crtbegin.o", RVPath),
    };

    // 遍历以查找gcc库的路径
    for (int I = 0; I < sizeof(paths) / sizeof(*paths); I++) {
        char *path = findFile(paths[I]);
        if (path)
            return dirname(path);
    }

    error("gcc library path is not found");
    return NULL;
}

// 替换文件的后缀名
static char *replaceExtn(char *Tmpl, char *Extn) {
    // 去除路径，返回基础文件名
    char *Filename = basename(strdup(Tmpl));
    // 最后一次字符出现的位置
    char *Dot = strrchr(Filename, '.');
    // 如果存在'.'，清除此后的内容
    if (Dot)
        *Dot = '\0';
    // 将新后缀写入文件名中
    return format("%s%s", Filename, Extn);
}

// 清理临时文件区
static void cleanup(void) {
    // 遍历删除临时文件
    for (int I = 0; I < TmpFiles.Len; I++)
        unlink(TmpFiles.Data[I]);
}

// 创建临时文件
static char *createTmpFile(void) {
    // 临时文件的路径格式
    char *Path = strdup("/tmp/rvcc-XXXXXX");
    // 创建临时文件
    int FD = mkstemp(Path);
    // 临时文件创建失败
    if (FD == -1)
        error("mkstemp failed: %s", strerror(errno));
    // 关闭文件
    close(FD);

    // 将文件路径存入临时文件区中
    strArrayPush(&TmpFiles, Path);
    return Path;
}

static void cc1(void);
// 开辟子进程
// stage2 has some problems in running sub process yet...
static void runSubprocess(char **Argv) {
    // 打印出子进程所有的命令行参数
    if (OptHashHashHash) {
        // 程序名
        fprintf(stderr, "%s", Argv[0]);
        // 程序参数
        for (int I = 1; Argv[I]; I++)
            fprintf(stderr, " %s", Argv[I]);
        // 换行
        fprintf(stderr, "\n");
    }

    // Fork–exec
    if (fork() == 0) {
        // 执行文件rvcc，没有斜杠时搜索环境变量，此时会替换子进程

// it seems that the fork-exec model doesn't apply to stage2/rvcc, maybe it will kill qemu
// and try to run stage2/rvcc as a new process directly on our x86 machine , which then print some wierd messages

/* small test:
    // test.c                               // foo.c
    int main(int argc, char **argv)         int main() 
    {                                       {
        execvp(argv[1], argv);                  printf("ok\n");
        printf("unreachable");              }
    }
    
    test steps:
        riscv64-linux-gnu-gcc test.c -o test.out
        riscv64-linux-gnu-gcc foo.c -o foo.out
        qemu-riscv64 test.out ./a.out
    then:
./a.out: 1: ELF��@�8: not found
./a.out: 1: {�iLNU: not found
./a.out: 2: : not found
./a.out: 3: Syntax error: "(" unexpected

        */
#ifdef _STAGE2_
        error("todo");
        Token *Tok = tokenizeFile(BaseFile);    // basefile = null...
        printTokens(Tok);
        exit(0);
        char *name = basename(Argv[0]);
        StringArray arr = {};
        strArrayPush(&arr, format("%s/qemu-riscv64", RVPath));
        strArrayPush(&arr, "-L /home/s081/riscv/sysroot");
        strArrayPush(&arr, "stage2/rvcc");
        int i = 0;
        if (strncmp(name, "rvcc", 4) == 0)
            i = i + 1;
        while(Argv && Argv[i]){
            strArrayPush(&arr, Argv[i++]);
        }
        strArrayPush(&arr, NULL);
        trace("exec %s\n", arr.Data[0]);
        if (strncmp(name, "rvcc", 4) == 0){
            for(int i = 0; i < arr.Len; i++)
                trace("%s", arr.Data[i]);
            execvp(arr.Data[0], arr.Data);
        }

#endif
        execvp(Argv[0], Argv);
        // 如果exec函数返回，表明没有正常执行命令
        fprintf(stderr, "exec failed: %s: %s\n", Argv[0], strerror(errno));
        _exit(1);
    }

    // 父进程， 等待子进程结束
    int Status;
    while (wait(&Status) > 0);
    // 处理子进程返回值
    if (Status != 0)
        exit(1);
}

// 执行调用cc1程序
// 因为rvcc自身就是cc1程序
// 所以调用自身，并传入-cc1参数作为子进程
static void runCC1(int Argc, char **Argv, char *Input, char *Output) {
    // 多开辟10个字符串的位置，用于传递需要新传入的参数
    char **Args = calloc(Argc + 10, sizeof(char *));
    // 将传入程序的参数全部写入Args
    memcpy(Args, Argv, Argc * sizeof(char *));
    // 在选项最后新加入"-cc1"选项
    Args[Argc++] = "-cc1";

    // 存入输入文件的参数
    if (Input){
        Args[Argc++] = "-cc1-input";
        Args[Argc++] = Input;
    }
    // 存入输出文件的参数
    if (Output) {
        Args[Argc++] = "-cc1-output";
        Args[Argc++] = Output;
    }

    // 运行自身作为子进程，同时传入选项
    runSubprocess(Args);
}

// 编译C文件到汇编文件
static void cc1(void) {
    // 解析文件，生成终结符流
    Token *Tok = tokenizeFile(BaseFile);
    // 终结符流生成失败，对应文件报错
    if (!Tok)
        error("%s: %s", BaseFile, strerror(errno));
    // 预处理
    Tok = preprocess(Tok);

    // 如果指定了-E那么打印出预处理过的C代码
    if (OptE) {
        printTokens(Tok);
        return;
    }

    // 解析终结符流
    Obj *Prog = parse(Tok);

    // 生成代码
    // 防止编译器在编译途中退出，而只生成了部分的文件
    // 开启临时输出缓冲区
    char *Buf;
    size_t BufLen;
    FILE *OutputBuf = open_memstream(&Buf, &BufLen);

    // 输出汇编到缓冲区中
    codegen(Prog, OutputBuf);
    fclose(OutputBuf);

    // 从缓冲区中写入到文件中
    FILE *Out = openFile(OutputFile);
    fwrite(Buf, BufLen, 1, Out);
    fclose(Out);
}

// 调用汇编器(.s -> .o)
static void assemble(char *Input, char *Output) {
    // 选择对应环境内的汇编器
    char *As = strlen(RVPath)
                    ? format("%s/bin/riscv64-unknown-linux-gnu-as", RVPath)
                    : "as";
    char *Cmd[] = {As, "-c", Input, "-o", Output, NULL};
    runSubprocess(Cmd);
}

// 运行链接器ld
static void runLinker(StringArray *Inputs, char *Output) {
    // 需要传递ld子进程的参数
    StringArray Arr = {};

    // 链接器
    char *Ld = strlen(RVPath)
                    ? format("%s/bin/riscv64-unknown-linux-gnu-ld", RVPath)
                    : "ld";
    strArrayPush(&Arr, Ld);

    // 输出文件
    strArrayPush(&Arr, "-o");
    strArrayPush(&Arr, Output);
    strArrayPush(&Arr, "-m");
    strArrayPush(&Arr, "elf64lriscv");
    strArrayPush(&Arr, "-dynamic-linker");

    char *LP64D =
        strlen(RVPath)
            ? format("%s/sysroot/lib/ld-linux-riscv64-lp64d.so.1", RVPath)
            : "/lib/ld-linux-riscv64-lp64d.so.1";
    strArrayPush(&Arr, LP64D);

    char *LibPath = findLibPath();
    char *GccLibPath = findGCCLibPath();

    strArrayPush(&Arr, format("%s/crt1.o", LibPath));
    strArrayPush(&Arr, format("%s/crti.o", LibPath));
    strArrayPush(&Arr, format("%s/crtbegin.o", GccLibPath));
    strArrayPush(&Arr, format("-L%s", GccLibPath));
    strArrayPush(&Arr, format("-L%s", LibPath));
    strArrayPush(&Arr, format("-L%s/..", LibPath));
    if (strlen(RVPath)) {
        strArrayPush(&Arr, format("-L%s/sysroot/usr/lib64", RVPath));
        strArrayPush(&Arr, format("-L%s/sysroot/lib64", RVPath));
        strArrayPush(&Arr,
                    format("-L%s/sysroot/usr/lib/riscv64-linux-gnu", RVPath));
        strArrayPush(&Arr,
                    format("-L%s/sysroot/usr/lib/riscv64-pc-linux-gnu", RVPath));
        strArrayPush(&Arr,
                    format("-L%s/sysroot/usr/lib/riscv64-redhat-linux", RVPath));
        strArrayPush(&Arr, format("-L%s/sysroot/usr/lib", RVPath));
        strArrayPush(&Arr, format("-L%s/sysroot/lib", RVPath));
    } else {
        strArrayPush(&Arr, "-L/usr/lib64");
        strArrayPush(&Arr, "-L/lib64");
        strArrayPush(&Arr, "-L/usr/lib/riscv64-linux-gnu");
        strArrayPush(&Arr, "-L/usr/lib/riscv64-pc-linux-gnu");
        strArrayPush(&Arr, "-L/usr/lib/riscv64-redhat-linux");
        strArrayPush(&Arr, "-L/usr/lib");
        strArrayPush(&Arr, "-L/lib");
    }

    // 输入文件，存入到链接器参数中
    for (int I = 0; I < Inputs->Len; I++)
        strArrayPush(&Arr, Inputs->Data[I]);

    strArrayPush(&Arr, "-lc");
    strArrayPush(&Arr, "-lm");  // temp
    strArrayPush(&Arr, "-lgcc");
    strArrayPush(&Arr, "--as-needed");
    strArrayPush(&Arr, "-lgcc_s");
    strArrayPush(&Arr, "--no-as-needed");
    strArrayPush(&Arr, format("%s/crtend.o", GccLibPath));
    strArrayPush(&Arr, format("%s/crtn.o", LibPath));
    strArrayPush(&Arr, NULL);

    // 开辟的链接器子进程
    runSubprocess(Arr.Data);
}

int main(int Argc, char **Argv) {
    // 在程序退出时，执行cleanup函数
    atexit(cleanup);
    // 解析传入程序的参数
    parseArgs(Argc, Argv);
    // 如果指定了-cc1选项
    // 直接编译C文件到汇编文件
    if (OptCC1) {
        addDefaultIncludePaths(Argv[0]);
        cc1();
        return 0;
    }

    // 当前不能指定-c、-S、-E后，将多个输入文件，输出到一个文件中
    if (InputPaths.Len > 1 && OptO && (OptC || OptS || OptE))
        error("cannot specify '-o' with '-c', '-S' or '-E' with multiple files");

    StringArray LdArgs = {};

    // 遍历每个输入文件
    for (int I = 0; I < InputPaths.Len; I++) {
        char *Input = InputPaths.Data[I];
        // 输出文件
        char *Output;
        // 如果指定了输出文件，则直接使用
        if (OptO)
            Output = OptO;
        // 若未指定输出的汇编文件名，则输出到后缀为.s的同名文件中
        else if (OptS)
            Output = replaceExtn(Input, ".s");
        // 若未指定输出的可重定位文件名，则输出到后缀为.o的同名文件中
        else
            Output = replaceExtn(Input, ".o");

        // 处理.o文件
        if (endsWith(Input, ".o")) {
            // 存入链接器选项中
            strArrayPush(&LdArgs, Input);
            continue;
        }

        // 处理.s文件
        if (endsWith(Input, ".s")) {
            // 如果没有指定-S，那么需要进行汇编
            if (!OptS)
                assemble(Input, Output);
            continue;
        }

        // 处理.c文件
//        if (!endsWith(Input, ".c") && strcmp(Input, "-"))
//            error("unknown file extension: %s", Input);

        // 只进行解析
        if (OptE) {
            runCC1(Argc, Argv, Input, NULL);
            continue;
        }

        // 如果有-S选项，那么执行调用cc1程序
        if (OptS) {
            runCC1(Argc, Argv, Input, Output);
            continue;
        }

        // 编译并汇编
        if (OptC) {
            // 临时文件Tmp作为cc1输出的汇编文件
            char *Tmp = createTmpFile();
            // cc1，编译C文件为汇编文件
            runCC1(Argc, Argv, Input, Tmp);
            // as，编译汇编文件为可重定位文件
            assemble(Tmp, Output);
            continue;
        }

        // 否则运行cc1和as
        // 临时文件Tmp1作为cc1输出的汇编文件
        // 临时文件Tmp2作为as输出的可重定位文件
        char *Tmp1 = createTmpFile();
        char *Tmp2 = createTmpFile();
        // cc1，编译C文件为汇编文件
        runCC1(Argc, Argv, Input, Tmp1);
        assemble(Tmp1, Tmp2);
        // 将Tmp2存入链接器选项
        strArrayPush(&LdArgs, Tmp2);
        continue;
    }

    // 需要链接的情况
    // 未指定文件名时，默认为a.out
    if (LdArgs.Len > 0)
        runLinker(&LdArgs, OptO ? OptO : "a.out");

    return 0;
}
