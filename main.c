#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rvcc.h"

extern int getNumber(Token* tok);
extern Token* skip(Token* tok, char * str);
extern bool equal(Token* tok, char* str);
extern Token* tokenize(char* P);
extern Token* newToken(TokenKind Kind, char *start, char *end);

int main(int Argc, char **Argv) {
  // 判断传入程序的参数是否为2个，Argv[0]为程序名称，Argv[1]为传入的第一个参数
  if (Argc != 2) {
    // 异常处理，提示参数数量不对。
    fprintf(stderr, "%s: invalid number of arguments\n", Argv[0]);
    // 程序返回值不为0时，表示存在错误
    return 1;
  }
  // 解析Argv[1]
  Token *Tok = tokenize(Argv[1]);
  // 声明一个全局main段，同时也是程序入口段
  printf("  .globl main\n");
  // main段标签
  printf("main:\n");
  // strtol. 参数为：被转换的str，str除去数字后的剩余部分，进制
  // 传入&P，即char**, 是为了修改P的值

  // 这里我们将算式分解为 num (op num) (op num)...的形式
  // 所以先将第一个num传入a0
  printf("  li a0, %d\n", getNumber(Tok));
  Tok = Tok -> Next;
  // 解析 (op num)
  // 解析 (op num)
  while (Tok->Kind != TK_EOF) {
    if (equal(Tok, "+")) {
      Tok = Tok->Next;
      printf("  addi a0, a0, %d\n", getNumber(Tok));
      Tok = Tok->Next;
      continue;
    }
    // 不是+，则判断-
    // 没有subi指令，但是addi支持有符号数，所以直接对num取反
    Tok = skip(Tok, "-");
    printf("  addi a0, a0, -%d\n", getNumber(Tok));
    Tok = Tok->Next;
  }
  printf("  ret\n");
  return 0;
}

