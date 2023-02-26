#!/bin/bash

COLOR_RED="\033[1;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NONE="\033[0m"

# 创建一个临时文件夹，XXXXXX会被替换为随机字符串
tmp=`mktemp -d /tmp/rvcc-test-XXXXXX`
# 清理工作
# 在接收到 中断（ctrl+c），终止，挂起（ssh掉线，用户退出），退出 信号时
# 执行rm命令，删除掉新建的临时文件夹
trap 'rm -rf $tmp' INT TERM HUP EXIT
# 在临时文件夹内，新建一个空文件，名为empty.c
echo > $tmp/empty.c

print1() {
    printf "$COLOR_GREEN $1 $COLOR_NONE\n"
}

print2() {
    printf "$COLOR_RED $1 $COLOR_NONE\n"
}

# 判断返回值是否为0来判断程序是否成功执行
check() {
    if [ $? -eq 0 ]; then
        print1 "testing '$1' passed"
    else
        print2 "testing '$1' failed!"
        exit 1
    fi
}

# -o
# 清理掉$tmp中的out文件
rm -f $tmp/# 编译生成out文件
./rvcc -o $tmp/out $tmp/empty.c
# 条件判断，是否存在out文件
[ -f $tmp/out ]
# 将-o传入check函数
check -o

# --help
# 将--help的结果传入到grep进行 行过滤
# -q不输出，是否匹配到存在rvcc字符串的行结果
# redirect fd 2(stderr) to 1
# don't use 2 > 1, this will redirect the output to a file named "1"
./rvcc --help 2>&1 | grep -q rvcc
# 将--help传入check函数
check --help

print1 OK
