# C编译器参数：使用C11标准，生成debug信息，禁止将未初始化的全局变量放入到common段
CFLAGS=-std=c11 -g -fno-common -Wall -Wno-switch
# 指定C编译器，来构建项目
CC=gcc
CROSS-CC=riscv64-linux-gnu-gcc

QEMU=qemu-riscv64

# C源代码文件，表示所有的.c结尾的文件
SRCS=$(wildcard *.c)
# C文件编译生成的未链接的可重定位文件，将所有.c文件替换为同名的.o结尾的文件名
OBJS=$(SRCS:.c=.o)
DEPS=$(SRCS:.c=.d)

# test/文件夹的c测试文件
TEST_SRCS=$(wildcard test/*.c)
# test/文件夹的c测试文件编译出的可执行文件
TESTS=$(TEST_SRCS:.c=.out)

# run the specific test
all=""

# rvcc标签，表示如何构建最终的二进制文件，依赖于所有的.o文件
# $@表示目标文件，此处为rvcc，$^表示依赖文件，此处为$(OBJS)
rvcc: $(OBJS)
# 将多个*.o文件编译为rvcc
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo [LD] $@

# 所有的可重定位文件依赖于rvcc.h的头文件
$(OBJS): rvcc.h
	@echo [CC] $(basename $@)
	@$(CC) -c $*.c -g

# 测试标签，运行测试
# 编译测试中的每个.c文件 由于现在的rvcc功能还较弱，所以借助了一些现有编译器的功能
# 做法是先使用系统cc预处理一遍，再把这个预处理结果交给rvcc
# 最后再使用系统cc把刚刚产生的东西和common这个文件链接起来. 参数说明：
# -o-将结果打印出来，-E只进行预处理，-P不输出行号信息，-C预处理时不会删除注释
test/%.out: rvcc test/%.c
	$(CROSS-CC) -o- -E -P -C test/$*.c | ./rvcc -o test/$*.s -
	$(CROSS-CC) -static -o $@ test/$*.s -xc test/common

# usage: make test all=xxx
test: $(TESTS)
# default run all
ifeq ($(all),"")
	@for i in $^; do echo $$i; $(QEMU) ./$$i || exit 1; echo; done
	@test/driver.sh
else
	@$(QEMU) ./test/$(all).out || exit 1; echo done

endif

count:
	@ls | grep "\.[ch]" | xargs cat | wc -l
	@rm *.d

# use system cc to help to link our program to libc
tmp: rvcc
	$(CROSS-CC) -o- -E -P -C -xc a | ./rvcc -o a.s -
	$(CROSS-CC) -static -o tmp a.s -xc test/common
	-$(QEMU) tmp

# 清理标签，清理所有非源代码文件
clean:
	-rm -rf rvcc tmp* *.d $(TESTS) test/*.s test/*.exe stage2/ thirdparty/
	-find * -type f '(' -name '*~' -o -name '*.o' -o -name '*.s' ')' -exec rm {} ';'

# 伪目标，没有实际的依赖文件
.PHONY: test clean count temp

-include $(DEPS)
%.d: %.c
	@set -e; rm -f $@; \
		$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
		sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@    ;\
		rm -f $@.$$$$

# sed here: find the pattern "xxx.o :" first, then
# substitute it with "xxx.o xxx.d :". 1 is a placeholder.
# otherwords just insert the .d file to the lhs of dependancy list
