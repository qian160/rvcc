#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rvcc.h"

/* ---------- tokenize.c ---------- */
extern Token* tokenize(char* P);

/* ---------- ast.c ---------- */
extern Node *expr(Token **Rest, Token *Tok);
extern void genExpr(Node *Nd);


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

  // 解析终结符流
  Node *Node = expr(&Tok, Tok);

  if (Tok->Kind != TK_EOF)
    error("extra token");

  // 声明一个全局main段，同时也是程序入口段
  println("  .globl main");
  // main段标签
  println("main:");

  // 遍历AST树生成汇编
  genExpr(Node);

  println("  ret");
  return 0;
}

