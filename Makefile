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
STAGE2_LOGO=stage2/logo.o
$(shell mkdir -p $(DST_DIR))

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.out)

# run the specific test
all=""

# $@表示目标文件，此处为rvcc，$^表示依赖文件，此处为$(OBJS)
$(DST_DIR)/rvcc: $(OBJS) $(LOGO)
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo [LD] $@

$(LOGO): logo.S logo.txt usage.txt
	@as $< -o $@

$(STAGE2_LOGO): logo.S
	@riscv64-unknown-elf-as $< -o $@

(OBJS): rvcc.h
	@echo [CC] $(basename $@)
	@$(CC) -c $*.c -g -o $@

$(DST_DIR)/%.o: %.c rvcc.h
	@echo [CC] $(basename $@)
	@$(CC) -c $*.c -g -o $@

test/%.out: $(DST_DIR)/rvcc test/%.c
	$(RVCC) -Itest -Iinclude -I$(RISCV)/sysroot/usr/include -c test/$*.c -o test/$*.o
	$(CROSS-CC) -pthread -o $@ test/$*.o -xc test/common

# usage: make test -jx all=xx
test: $(TESTS)
# default run all
ifeq ($(all),"")
	@for i in $^; do echo $$i; $(QEMU) -L $(RISCV)/sysroot ./$$i || exit 1; echo; done
	@test/driver.sh target/rvcc
else
	@$(QEMU) -L $(RISCV)/sysroot ./test/$(all).out || exit 1; echo done
endif

# 进行全部的测试
test-all: test test-stage2

# Stage 2

# 利用stage1的rvcc去将rvcc的源代码编译为stage2的可重定位文件
stage2/%.o: $(DST_DIR)/rvcc %.c
	@mkdir -p stage2/test
	@echo [RVCC] $*
	@$(RVCC) -Itest -Iinclude -c -o $(@D)/$*.o $*.c -D_STAGE2_

# 此时构建的stage2/rvcc是RISC-V版本的，跟平台无关
# to exec stage2/rvcc we will have to use qemu
stage2/rvcc: $(objs:%=stage2/%) $(STAGE2_LOGO)
	@$(CROSS-CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -D_STAGE2_
	@echo [LD] $@

# 利用stage2的rvcc去进行测试. bug...
stage2/test/%.out: stage2/rvcc test/%.c
	mkdir -p stage2/test
	$(QEMU) -L $(RISCV)/sysroot stage2/rvcc -Itest -Iinclude -c -o stage2/test/$*.o test/$*.c
	$(CROSS-CC) -pthread -o $@ stage2/test/$*.o -xc test/common

test-stage2: $(TESTS:test/%=stage2/test/%)
	for i in $^; do echo $$i; $(QEMU) -L $(RISCV)/sysroot ./$$i || exit 1; echo; done
	test/driver.sh stage2/rvcc $(QEMU) -L $(RISCV)/sysroot ./stage2/rvcc

count:
	@ls | grep "\.[ch]" | xargs cat | wc -l

msg=""
git:
	@make clean
	@git add -A
	@git commit -m "$(msg)"
#	@git push rvcc master

# 测试第三方程序
test-libpng: $(DST_DIR)/rvcc
	./test/thirdparty/libpng.sh

test-sqlite: $(DST_DIR)/rvcc
	./test/thirdparty/sqlite.sh

test-tinycc: $(DST_DIR)/rvcc
	./test/thirdparty/tinycc.sh

test-lua: $(DST_DIR)/rvcc
	./test/thirdparty/lua.sh

test-git: $(DST_DIR)/rvcc
	./test/thirdparty/git.sh

# 清理所有非源代码文件
clean:
	-rm -rf tmp* *.d $(TESTS) test/*.s test/*.out stage2/ thirdparty/ target/
	-find * -type f '(' -name '*~' -o -name '*.o' -o -name '*.s' ')' -exec rm {} ';'

# 伪目标，没有实际的依赖文件
.PHONY: test clean count test-stage2 test-all git

-include $(DEPS)
$(DST_DIR)/%.d: %.c
	@set -e; rm -f $@;\
		$(CC) -MM $< > $@.$$$$; \
		sed 's,\($*\)\.o[ :]*,target/\1 $@ : ,g' < $@.$$$$ > $@    ;\
		rm -f $@.$$$$

# sed here: find the pattern "xxx.o :" first, then
# substitute it with "xxx.o xxx.d :".
# \1 is just a placeholder.
# otherwords, just insert the .d file into the lhs of dependancy list
#
# qemu-riscv64 -L ~/riscv/sysroot a.out
