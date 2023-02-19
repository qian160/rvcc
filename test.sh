#!/bin/bash
gcc=riscv64-linux-gnu-gcc
qemu=qemu-riscv64

COLOR_RED="\033[1;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NONE="\033[0m"

# 声明一个函数
# assert 期待值 输入值
assert() {
  expected="$1"
  input="$2"

  # 运行程序，传入期待值，将生成结果写入tmp.s汇编文件。
  # 如果运行不成功，则会执行exit退出。成功时会短路exit操作
  ./rvcc "$input" > tmp.s || exit
  # 编译rvcc产生的汇编文件
  # gcc -o tmp tmp.s
  $gcc -static -g -o tmp tmp.s

  # 运行生成出来目标文件
  # ./tmp
  $qemu ./tmp

  # 获取程序返回值，存入 实际值
  actual="$?"

  # 判断实际值，是否为预期值
  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    printf "$COLOR_RED FAIL! $COLOR_NONE\n"
    exit 1
  fi
}

# [1] 返回指定数值
#for ((i=0; i<=1000; i++)); do
#  assert $i $i
#done
assert 11 11
assert 45 45
assert 14 14

# [2] 支持+ -运算符
assert 34 '12-34+56'

# [3] 支持空格
assert 41 ' 12 + 34 - 5 '

# [5] 支持* / ()运算符
assert 47 '5+6*7'
assert 15 '5*(9-6)'
assert 17 '1-8/(2*2)+3*6'

# 如果运行正常未提前退出，程序将显示OK
printf "$COLOR_GREEN PASS! $COLOR_NONE\n"

