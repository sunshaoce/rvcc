# C编译器参数：使用C11标准，生成debug信息，禁止将未初始化的全局变量放入到common段
CFLAGS=-std=c11 -g -fno-common
# 指定C编译器，来构建项目
CC=gcc
# C源代码文件，表示所有的.c结尾的文件
SRCS=$(wildcard *.c)
# C文件编译生成的未链接的可重定位文件，将所有.c文件替换为同名的.o结尾的文件名
OBJS=$(SRCS:.c=.o)
# test/文件夹的c测试文件
TEST_SRCS=$(wildcard test/*.c)
# test/文件夹的c测试文件编译出的可执行文件
TESTS=$(TEST_SRCS:.c=.exe)

# rvcc标签，表示如何构建最终的二进制文件，依赖于所有的.o文件
# $@表示目标文件，此处为rvcc，$^表示依赖文件，此处为$(OBJS)
rvcc: $(OBJS)
# 将多个*.o文件编译为rvcc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 所有的可重定位文件依赖于rvcc.h的头文件
$(OBJS): rvcc.h

# 测试标签，运行测试
test/%.exe: rvcc test/%.c
	$(CC) -o- -E -P -C test/$*.c | ./rvcc -o test/$*.s -
#	$(RISCV)/bin/riscv64-unknown-linux-gnu-gcc -o- -E -P -C test/$*.c | ./rvcc -o test/$*.s -
	$(CC) -o $@ test/$*.s -xc test/common
#	$(RISCV)/bin/riscv64-unknown-linux-gnu-gcc -static -o $@ test/$*.s -xc test/common

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
#	for i in $^; do echo $$i; $(RISCV)/bin/qemu-riscv64 -L $(RISCV)/sysroot ./$$i || exit 1; echo; done
#	for i in $^; do echo $$i; $(RISCV)/bin/spike --isa=rv64gc $(RISCV)/riscv64-unknown-linux-gnu/bin/pk ./$$i || exit 1; echo; done
	test/driver.sh

# 清理标签，清理所有非源代码文件
clean:
	rm -rf rvcc tmp* $(TESTS) test/*.s test/*.exe
	find * -type f '(' -name '*~' -o -name '*.o' -o -name '*.s' ')' -exec rm {} ';'

# 伪目标，没有实际的依赖文件
.PHONY: test clean
