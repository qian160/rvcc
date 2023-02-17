#include <stdio.h>
#include <stdlib.h>

int main(int Argc, char **Argv) {
  // 判断传入程序的参数是否为2个，Argv[0]为程序名称，Argv[1]为传入的第一个参数
  if (Argc != 2) {
    // 异常处理，提示参数数量不对。
    fprintf(stderr, "%s: invalid number of arguments\n", Argv[0]);
    // 程序返回值不为0时，表示存在错误
    return 1;
  }

  // 声明一个全局main段，同时也是程序入口段
  printf("  .globl main\n");
  // main段标签
  printf("main:\n");
  // li为addi别名指令，加载一个立即数到寄存器中
  // 传入程序的参数为str类型，因为需要转换为需要int类型，
  printf("  li a0, %d\n", atoi(Argv[1]));
  // ret为jalr x0, x1, 0别名指令，用于返回子程序
  printf("  ret\n");

  return 0;
}

