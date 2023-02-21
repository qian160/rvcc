# C编译器参数：使用C11标准，生成debug信息，禁止将未初始化的全局变量放入到common段
CFLAGS=-std=c11 -g -fno-common -Wall -Wno-switch
# 指定C编译器，来构建项目
CC=gcc
# C源代码文件，表示所有的.c结尾的文件
SRCS=$(wildcard *.c)
# C文件编译生成的未链接的可重定位文件，将所有.c文件替换为同名的.o结尾的文件名
OBJS=$(SRCS:.c=.o)
DEPS=$(SRCS:.c=.d)

# rvcc标签，表示如何构建最终的二进制文件，依赖于所有的.o文件
# $@表示目标文件，此处为rvcc，$^表示依赖文件，此处为$(OBJS)
rvcc: $(OBJS)
# 将多个*.o文件编译为rvcc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 所有的可重定位文件依赖于rvcc.h的头文件
$(OBJS): rvcc.h
	@echo [CC] $(basename $@)
	@$(CC) -c $*.c
test:rvcc
	@./test.sh
count:
	@ls | grep "\.[ch]" | xargs cat | wc -l
	@rm *.d
# 清理标签，清理所有非源代码文件
clean:
	-rm -rf rvcc tmp* *.d $(TESTS) test/*.s test/*.exe stage2/ thirdparty/
	-find * -type f '(' -name '*~' -o -name '*.o' -o -name '*.s' ')' -exec rm {} ';'

# 伪目标，没有实际的依赖文件
.PHONY: test clean count

-include $(DEPS)
%.d: %.c	# add .d file to the dependancy list
	@set -e; rm -f $@; \
		$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
		sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@    ;\
		rm -f $@.$$$$

# sed here: find the pattern "xxx.o :" first, then
# substitute it with "xxx.o xxx.d :". 1 is a placeholder.
