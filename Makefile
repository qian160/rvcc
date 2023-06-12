# C编译器参数：使用C11标准，生成debug信息，禁止将未初始化的全局变量放入到common段
CFLAGS=-std=c11 -g -fno-common -Wall -Wno-switch
CC=gcc
RISCV=/home/s081/riscv
CROSS-CC=$(RISCV)/bin/riscv64-unknown-linux-gnu-gcc
#riscv64-linux-gnu-gcc
DST_DIR=target
RVCC=$(DST_DIR)/rvcc
QEMU=$(RISCV)/bin/qemu-riscv64
#QEMU=qemu-riscv64

SRCS=$(wildcard *.c)
objs=$(SRCS:.c=.o)
OBJS=$(addprefix $(DST_DIR)/, $(objs))
DEPS=$(addprefix $(DST_DIR)/, $(SRCS:.c=.d))
LOGO=$(DST_DIR)/logo.o
$(shell mkdir -p $(DST_DIR))

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.out)

# run the specific test
all=""

# $@表示目标文件，此处为rvcc，$^表示依赖文件，此处为$(OBJS)
$(DST_DIR)/rvcc: $(OBJS) $(LOGO)
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo [LD] $@

$(LOGO): logo.S
	@as $< -o $@

(OBJS): rvcc.h
	@echo [CC] $(basename $@)
	@$(CC) -c $*.c -g -o $@

$(DST_DIR)/%.o: %.c rvcc.h
	@echo [CC] $(basename $@)
	@$(CC) -c $*.c -g -o $@

# 编译测试中的每个.c文件 由于现在的rvcc功能还较弱，所以借助了一些现有编译器的功能
# 做法是先使用系统cc预处理一遍，再把这个预处理结果交给rvcc
# 最后再使用系统cc把刚刚产生的东西和common这个文件链接起来. 参数说明：
# -o-将结果打印出来，-E只进行预处理，-P不输出行号信息，-C预处理时不会删除注释
test/%.out: $(DST_DIR)/rvcc test/%.c
#	$(CROSS-CC) -o- -E -P -C test/$*.c | $(RVCC) -c -o test/$*.o -
#	$(CROSS-CC) -static -o $@ test/$*.o -xc test/common
	$(RVCC) -c test/$*.c -o test/$*.o
	$(CROSS-CC) -o $@ test/$*.o -xc test/common

# usage: make test -jx all=xx
test: $(TESTS)
# default run all
ifeq ($(all),"")
	@for i in $^; do echo $$i; $(QEMU) -L $(RISCV)/sysroot ./$$i || exit 1; echo; done
	@test/driver.sh
else
	@$(QEMU) -L $(RISCV)/sysroot ./test/$(all).out || exit 1; echo done
endif

# 进行全部的测试
# 暂时无法编译自己, for macros and linkage problems
test-all: test test-stage2

# Stage 2

# 此时构建的stage2/rvcc是RISC-V版本的，跟平台无关
stage2/rvcc: $(objs:%=stage2/%)
	$(CROSS-CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 利用stage1的rvcc去将rvcc的源代码编译为stage2的汇编文件
stage2/%.o: $(DST_DIR)/rvcc self.py %.c
	mkdir -p stage2/test
	./self.py rvcc.h $*.c > stage2/$*.c
	$(RVCC) -c -o stage2/$*.o stage2/$*.c

# stage2的汇编编译为可重定位文件
stage2/%.o: stage2/%.s
	$(CROSS-CC) -c stage2/$*.s -o stage2/$*.o

# 利用stage2的rvcc去进行测试
stage2/test/%.out: stage2/rvcc test/%.c
	mkdir -p stage2/test
#	$(CROSS-CC) -o- -E -P -C test/$*.c | ./stage2/rvcc -c -o stage2/test/$*.o -
	./stage2/rvcc -c -o stage2/test/$*.o test/$*.c
	$(CROSS-CC) -o $@ stage2/test/$*.o -xc test/common

test-stage2: $(TESTS:test/%=stage2/test/%)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./stage2/rvcc


count:
	@ls | grep "\.[ch]" | xargs cat | wc -l

msg=""
git:
	make clean
	git add -A
	git commit -m "$(msg)"
#	@git push rvcc master

# 清理所有非源代码文件
clean:
	-rm -rf tmp* *.d $(TESTS) test/*.s test/*.out stage2/ thirdparty/ target/
	-find * -type f '(' -name '*~' -o -name '*.o' -o -name '*.s' ')' -exec rm {} ';'

# 伪目标，没有实际的依赖文件
.PHONY: test clean count test-stage2

-include $(DEPS)
$(DST_DIR)/%.d: %.c
	@set -e; rm -f $@;\
		$(CC) -MM $< > $@.$$$$; \
		sed 's,\($*\)\.o[ :]*,target/\1 $@ : ,g' < $@.$$$$ > $@    ;\
		rm -f $@.$$$$

# sed here: find the pattern "xxx.o :" first, then
# substitute it with "xxx.o xxx.d :". 1 is a placeholder.
# otherwords just insert the .d file to the lhs of dependancy list
#
# qemu-riscv64 -L ~/riscv/sysroot a.out
