#include "rvcc.h"

int main(int Argc, char **Argv) {
    // 判断传入程序的参数是否为2个，Argv[0]为程序名称，Argv[1]为传入的第一个参数
    if (Argc != 2) {
      // 异常处理，提示参数数量不对。
      error("%s: invalid number of arguments\n", Argv[0]);
    }
    // 解析Argv[1]
    Token *Tok = tokenize(Argv[1]);

    // 解析终结符流
    Obj *Prog = parse(Tok);

    codegen(Prog);

    return 0;
}