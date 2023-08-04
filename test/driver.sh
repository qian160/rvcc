#!/bin/bash

rvcc=$1
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
echo 'int main() {}' | $rvcc -S -o- -xc - | grep -q 'main:'
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
echo 'int main() { return 0; }' | $rvcc -o $tmp/foo -xc -
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
echo "#include \"$tmp/out\"" | $rvcc -E -xc - | grep -q foo
check -E

echo foo > $tmp/out1
echo "#include \"$tmp/out1\"" | $rvcc -E -o $tmp/out2 -xc -
cat $tmp/out2 | grep -q foo
check '-E and -o'

# [185] 支持 -I<Dir> 选项
# -I
mkdir $tmp/dir
echo foo > $tmp/dir/i-option-test
echo "#include \"i-option-test\"" | $rvcc -I$tmp/dir -E -xc - | grep -q foo
check -I

# [208] 支持-D选项
# -D
echo foo | $rvcc -Dfoo -E -xc - | grep -q 1
check -D

# -D
echo foo | $rvcc -Dfoo=bar -E -xc - | grep -q bar
check -D

# [209] 支持-U选项
# -U
echo foo | $rvcc -Dfoo=bar -Ufoo -E -xc - | grep -q foo
check -U

# [216] 忽略多个链接器选项
$rvcc -c -O -Wall -g -std=c11 -ffreestanding -fno-builtin \
    -fno-omit-frame-pointer -fno-stack-protector -fno-strict-aliasing \
    -m64 -mno-red-zone -w -o /dev/null $tmp/empty.c
check 'ignored options'

# [238] 跳过UTF-8 BOM标记
printf '\xef\xbb\xbfxyz\n' | $rvcc -E -o- -xc - | grep -q '^xyz'
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

# [261] 如果没被引用不生成静态内联函数
echo 'static inline void f1() {}' | $rvcc -o- -S -xc - | grep -v -q f1:
check inline

echo 'static inline void f1() {} void foo() { f1(); }' | $rvcc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f1(); }' | $rvcc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f1(); }' | $rvcc -o- -S -xc - | grep -v -q f2:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f2(); }' | $rvcc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f2(); }' | $rvcc -o- -S -xc - | grep -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() {}' | $rvcc -o- -S -xc - | grep -v -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() {}' | $rvcc -o- -S -xc - | grep -v -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f1(); }' | $rvcc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f1(); }' | $rvcc -o- -S -xc - | grep -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f2(); }' | $rvcc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f2(); }' | $rvcc -o- -S -xc - | grep -q f2:
check inline

# -idirafter
# [263] 支持-idirafter选项
mkdir -p $tmp/dir1 $tmp/dir2
echo foo > $tmp/dir1/idirafter
echo bar > $tmp/dir2/idirafter
echo "#include \"idirafter\"" | $rvcc -I$tmp/dir1 -I$tmp/dir2 -E -xc - | grep -q foo
check -idirafter
echo "#include \"idirafter\"" | $rvcc -idirafter $tmp/dir1 -I$tmp/dir2 -E -xc - | grep -q bar
check -idirafter

# [266] 支持-fcommon和-fno-common选项
# -fcommon
echo 'int foo;' | $rvcc -S -o- -xc - | grep -q '\.comm foo'
check '-fcommon (default)'

echo 'int foo;' | $rvcc -fcommon -S -o- -xc - | grep -q '\.comm foo'
check '-fcommon'

# -fno-common
echo 'int foo;' | $rvcc -fno-common -S -o- -xc - | grep -q '^foo:'
check '-fno-common'

# [268] 支持-include选项
# -include
echo foo > $tmp/out.h
echo bar | $rvcc -I$RISCV/sysroot/usr/include/ -include $tmp/out.h -E -o- -xc - | grep -q -z 'foo.*bar'
check -include
echo NULL | $rvcc -I$RISCV/sysroot/usr/include/ -Iinclude -include stdio.h -E -o- -xc - | grep -q 0
check -include

# [269] 支持-x选项
# -x
echo 'int x;' | $rvcc -c -xc -o $tmp/foo.o -
check -xc
echo 'x:' | $rvcc -c -x assembler -o $tmp/foo.o -
check '-x assembler'

echo 'int x;' > $tmp/foo.c
$rvcc -c -x assembler -x none -o $tmp/foo.o $tmp/foo.c
check '-x none'

# [270] 使-E包含-xc
echo foo | $rvcc -E - | grep -q foo
check -E

# [279] 识别.a和.so文件
# .a file
echo 'void foo() {}' | $rvcc -c -xc -o $tmp/foo.o -
echo 'void bar() {}' | $rvcc -c -xc -o $tmp/bar.o -
if [ "$RISCV" = "" ];then
  ar rcs $tmp/foo.a $tmp/foo.o $tmp/bar.o
else
  $RISCV/bin/riscv64-unknown-linux-gnu-ar rcs $tmp/foo.a $tmp/foo.o $tmp/bar.o
fi
echo 'void foo(); void bar(); int main() { foo(); bar(); }' > $tmp/main.c
$rvcc -o $tmp/foo $tmp/main.c $tmp/foo.a
check '.a'

# .so file
if [ "$RISCV" = "" ];then
  echo 'void foo() {}' | cc -fPIC -c -xc -o $tmp/foo.o -
  echo 'void bar() {}' | cc -fPIC -c -xc -o $tmp/bar.o -
  cc -shared -o $tmp/foo.so $tmp/foo.o $tmp/bar.o
else
  echo 'void foo() {}' | $RISCV/bin/riscv64-unknown-linux-gnu-gcc -fPIC -c -xc -o $tmp/foo.o -
  echo 'void bar() {}' | $RISCV/bin/riscv64-unknown-linux-gnu-gcc -fPIC -c -xc -o $tmp/bar.o -
  $RISCV/bin/riscv64-unknown-linux-gnu-gcc -shared -o $tmp/foo.so $tmp/foo.o $tmp/bar.o
fi
echo 'void foo(); void bar(); int main() { foo(); bar(); }' > $tmp/main.c
$rvcc -o $tmp/foo $tmp/main.c $tmp/foo.so
check '.so'

# [285] 支持字符串哈希表
$rvcc -hashmap-test
check 'hashmap'

# [289] 支持-M选项
# -M
echo '#include "out2.h"' > $tmp/out.c
echo '#include "out3.h"' >> $tmp/out.c
touch $tmp/out2.h $tmp/out3.h
$rvcc -M -I$tmp $tmp/out.c | grep -q -z '^out.o: .*/out\.c .*/out2\.h .*/out3\.h'
check -M

# [290] 支持-MF选项
# -MF
$rvcc -MF $tmp/mf -M -I$tmp $tmp/out.c
grep -q -z '^out.o: .*/out\.c .*/out2\.h .*/out3\.h' $tmp/mf
check -MF

# [291] 支持-MP选项
# -MP
$rvcc -MF $tmp/mp -MP -M -I$tmp $tmp/out.c
grep -q '^.*/out2.h:' $tmp/mp
check -MP
grep -q '^.*/out3.h:' $tmp/mp
check -MP

# [292] 支持-MT选项
# -MT
$rvcc -MT foo -M -I$tmp $tmp/out.c | grep -q '^foo:'
check -MT
$rvcc -MT foo -MT bar -M -I$tmp $tmp/out.c | grep -q '^foo bar:'
check -MT

# [293] 支持-MD选项
# -MD
echo '#include "out2.h"' > $tmp/md2.c
echo '#include "out3.h"' > $tmp/md3.c
(cd $tmp; $OLDPWD/$rvcc -c -MD -I. md2.c md3.c)
grep -q -z '^md2.o:.* md2\.c .* ./out2\.h' $tmp/md2.d
check -MD
grep -q -z '^md3.o:.* md3\.c .* ./out3\.h' $tmp/md3.d
check -MD

$rvcc -c -MD -MF $tmp/md-mf.d -I. $tmp/md2.c
grep -q -z '^md2.o:.*md2\.c .*/out2\.h' $tmp/md-mf.d
check -MD

# [294] 支持-MQ选项
# -MQ
$rvcc -MQ '$foo' -M -I$tmp $tmp/out.c | grep -q '^$$foo:'
check -MQ
$rvcc -MQ '$foo' -MQ bar -M -I$tmp $tmp/out.c | grep -q '^$$foo bar:'
check -MQ

# [296] 支持-fpic和-fPIC选项
echo 'extern int bar; int foo() { return bar; }' | $rvcc -fPIC -xc -c -o $tmp/foo.o -
if [ "$RISCV" = "" ];then
  cc -shared -o $tmp/foo.so $tmp/foo.o
else
  $RISCV/bin/riscv64-unknown-linux-gnu-gcc -shared -o $tmp/foo.so $tmp/foo.o
fi

echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/main.c
$rvcc -o $tmp/foo $tmp/main.c $tmp/foo.so
check -fPIC

# [300] [GNU] 支持#include_next
# #include_next
mkdir -p $tmp/next1 $tmp/next2 $tmp/next3
echo '#include "file1.h"' > $tmp/file.c
echo '#include_next "file1.h"' > $tmp/next1/file1.h
echo '#include_next "file2.h"' > $tmp/next2/file1.h
echo 'foo' > $tmp/next3/file2.h
$rvcc -I$tmp/next1 -I$tmp/next2 -I$tmp/next3 -E $tmp/file.c | grep -q foo
check '#include_next'

# [301] 支持-static选项
# -static
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$rvcc -static -o $tmp/foo $tmp/foo.c $tmp/bar.c
check -static
file $tmp/foo | grep -q 'statically linked'
check -static

# [302] 支持-shared选项
# -shared
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$rvcc -fPIC -shared -o $tmp/foo.so $tmp/foo.c $tmp/bar.c
check -shared

# [303] 支持-L选项
# -L
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
$rvcc -fPIC -shared -o $tmp/libfoobar.so $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$rvcc -o $tmp/foo $tmp/bar.c -L$tmp -lfoobar
check -L

# [304] 支持-Wl,选项
# -Wl,
echo 'int foo() {}' | $rvcc -c -o $tmp/foo.o -xc -
echo 'int foo() {}' | $rvcc -c -o $tmp/bar.o -xc -
echo 'int main() {}' | $rvcc -c -o $tmp/baz.o -xc -
if [ "$RISCV" = "" ];then
  cc -Wl,-z,muldefs,--gc-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
else
  $RISCV/bin/riscv64-unknown-linux-gnu-gcc -Wl,-z,muldefs,--gc-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
fi
check -Wl

# [305] 支持-Xlinker选项
# -Xlinker
echo 'int foo() {}' | $rvcc -c -o $tmp/foo.o -xc -
echo 'int foo() {}' | $rvcc -c -o $tmp/bar.o -xc -
echo 'int main() {}' | $rvcc -c -o $tmp/baz.o -xc -
if [ "$RISCV" = "" ];then
  cc -Xlinker -z -Xlinker muldefs -Xlinker --gc-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
else
  $RISCV/bin/riscv64-unknown-linux-gnu-gcc -Xlinker -z -Xlinker muldefs -Xlinker --gc-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
fi
check -Xlinker

printf "$COLOR_GREEN OK $COLOR_NONE\n"