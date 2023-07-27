#!/bin/bash

rvcc=./target/rvcc
RISCV="/home/s081/riscv"
# 创建一个临时文件夹，XXXXXX会被替换为随机字符串
tmp=`mktemp -d /tmp/rvcc-test-XXXXXX`
# 清理工作
# 在接收到 中断（ctrl+c），终止，挂起（ssh掉线，用户退出），退出 信号时
# 执行rm命令，删除掉新建的临时文件夹
trap 'rm -rf $tmp' INT TERM HUP EXIT
# 在临时文件夹内，新建一个空文件，名为empty.c
echo > $tmp/empty.c

COLOR_RED="\033[1;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NONE="\033[0m"
# 判断返回值是否为0来判断程序是否成功执行
check() {
  if [ $? -eq 0 ]; then
    printf "testing $1 ...$COLOR_GREEN passed 😀$COLOR_NONE\n"
  else
    printf "testing $1 ...$COLOR_RED failed 😵$COLOR_NONE\n"
    exit 1
  fi
}

# -o
# 清理掉$tmp中的out文件
rm -f $tmp/out
# 编译生成out文件
$rvcc -c -o $tmp/out $tmp/empty.c
# 条件判断，是否存在out文件
[ -f $tmp/out ]
# 将-o传入check函数
check -o

# --help
# 将--help的结果传入到grep进行 行过滤
# -q不输出，是否匹配到存在rvcc字符串的行结果
$rvcc --help 2>&1 | grep -q rvcc
# 将--help传入check函数
check --help

# -S
echo 'int main() {}' | $rvcc -S -o - - | grep -q 'main:'
check -S

# 默认输出的文件
rm -f $tmp/out.o $tmp/out.s
echo 'int main() {}' > $tmp/out.c
($rvcc -c $tmp/out.c > $tmp/out.o )
[ -f $tmp/out.o ]
check 'default output file'

($rvcc -c -S $tmp/out.c > $tmp/out.s)
[ -f $tmp/out.s ]
check 'default output file'

# [156] 接受多个输入文件
rm -f $tmp/foo.o $tmp/bar.o
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$rvcc -c $tmp/foo.c $tmp/bar.c)
[ -f $tmp/foo.o ] && [ -f $tmp/bar.o ]
check 'multiple input files'

rm -f $tmp/foo.s $tmp/bar.s
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$rvcc -c -S $tmp/foo.c $tmp/bar.c)
[ -f $tmp/foo.s ] && [ -f $tmp/bar.s ]
check 'multiple input files'

# [157] 无-c时调用ld
# 调用链接器
rm -f $tmp/foo
echo 'int main() { return 0; }' | $rvcc -o $tmp/foo -
if [ "$RISCV" = "" ];then
  $tmp/foo
else
  $RISCV/bin/qemu-riscv64 -L $RISCV/sysroot $tmp/foo
fi
check linker

rm -f $tmp/foo
echo 'int bar(); int main() { return bar(); }' > $tmp/foo.c
echo 'int bar() { return 42; }' > $tmp/bar.c
$rvcc -o $tmp/foo $tmp/foo.c $tmp/bar.c
if [ "$RISCV" = "" ];then
  $tmp/foo
else
  $RISCV/bin/qemu-riscv64 -L $RISCV/sysroot $tmp/foo
fi
[ "$?" = 42 ]
check linker

# 生成a.out
rm -f $tmp/a.out
echo 'int main() {}' > $tmp/foo.c
(cd $tmp; $OLDPWD/$rvcc foo.c)
[ -f $tmp/a.out ]
check a.out

# -E
# [162] 支持-E选项
echo foo > $tmp/out
echo "#include \"$tmp/out\"" | $rvcc -E - | grep -q foo
check -E

echo foo > $tmp/out1
echo "#include \"$tmp/out1\"" | $rvcc -E -o $tmp/out2 -
cat $tmp/out2 | grep -q foo
check '-E and -o'

# [185] 支持 -I<Dir> 选项
# -I
mkdir $tmp/dir
echo foo > $tmp/dir/i-option-test
echo "#include \"i-option-test\"" | $rvcc -I$tmp/dir -E - | grep -q foo
check -I

# [208] 支持-D选项
# -D
echo foo | $rvcc -Dfoo -E - | grep -q 1
check -D

# -D
echo foo | $rvcc -Dfoo=bar -E - | grep -q bar
check -D

# [209] 支持-U选项
# -U
echo foo | $rvcc -Dfoo=bar -Ufoo -E - | grep -q foo
check -U

# [216] 忽略多个链接器选项
$rvcc -c -O -Wall -g -std=c11 -ffreestanding -fno-builtin \
    -fno-omit-frame-pointer -fno-stack-protector -fno-strict-aliasing \
    -m64 -mno-red-zone -w -o /dev/null $tmp/empty.c
check 'ignored options'

# [238] 跳过UTF-8 BOM标记
printf '\xef\xbb\xbfxyz\n' | $rvcc -E -o- - | grep -q '^xyz'
check 'BOM marker'

# Inline functions
# [260] 将inline函数作为static函数
echo 'inline void foo() {}' > $tmp/inline1.c
echo 'inline void foo() {}' > $tmp/inline2.c
echo 'int main() { return 0; }' > $tmp/inline3.c
$rvcc -o /dev/null $tmp/inline1.c $tmp/inline2.c $tmp/inline3.c
check inline

echo 'extern inline void foo() {}' > $tmp/inline1.c
echo 'int foo(); int main() { foo(); }' > $tmp/inline2.c
$rvcc -o /dev/null $tmp/inline1.c $tmp/inline2.c
check inline


printf "$COLOR_GREEN OK $COLOR_NONE\n"