# C编译器参数：使用C11标准，生成debug信息，禁止将未初始化的全局变量放入到common段
CFLAGS=-std=c11 -g -fno-common -Wall -Wno-switch
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

# Stage 1

# rvcc标签，表示如何构建最终的二进制文件，依赖于所有的.o文件
# $@表示目标文件，此处为rvcc，$^表示依赖文件，此处为$(OBJS)
rvcc: $(OBJS)
# 将多个*.o文件编译为rvcc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 所有的可重定位文件依赖于rvcc.h的头文件
$(OBJS): rvcc.h

# 测试标签，运行测试
test/%.exe: rvcc test/%.c
	./rvcc -Iinclude -Itest -I$(RISCV)/sysroot/usr/include -c -o test/$*.o test/$*.c
	$(CC) -pthread -o $@ test/$*.o -xc test/common
#	$(RISCV)/bin/riscv64-unknown-linux-gnu-gcc -pthread -static -o $@ test/$*.o -xc test/common

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
#	for i in $^; do echo $$i; $(RISCV)/bin/qemu-riscv64 -L $(RISCV)/sysroot ./$$i || exit 1; echo; done
#	for i in $^; do echo $$i; $(RISCV)/bin/spike --isa=rv64gc $(RISCV)/riscv64-unknown-linux-gnu/bin/pk ./$$i || exit 1; echo; done
	test/driver.sh ./rvcc

# 进行全部的测试
test-all: test test-stage2

# Stage 2

# 此时构建的stage2/rvcc是RISC-V版本的，跟平台无关
stage2/rvcc: $(OBJS:%=stage2/%)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 利用stage1的rvcc去将rvcc的源代码编译为stage2的可重定位文件
stage2/%.o: rvcc %.c
	mkdir -p stage2/test
	./rvcc -c -o $(@D)/$*.o $*.c

# 利用stage2的rvcc去进行测试
stage2/test/%.exe: stage2/rvcc test/%.c
	mkdir -p stage2/test
	./stage2/rvcc -Iinclude -Itest -c -o stage2/test/$*.o test/$*.c
	$(CC) -pthread -o $@ stage2/test/$*.o -xc test/common

test-stage2: $(TESTS:test/%=stage2/test/%)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./stage2/rvcc

# 测试第三方程序
test-libpng: rvcc
	./test/thirdparty/libpng.sh

test-sqlite: rvcc
	./test/thirdparty/sqlite.sh

test-tinycc: rvcc
	./test/thirdparty/tinycc.sh

test-lua: rvcc
	./test/thirdparty/lua.sh

test-git: rvcc
	./test/thirdparty/git.sh


# 清理标签，清理所有非源代码文件
clean:
	rm -rf rvcc tmp* $(TESTS) test/*.s test/*.exe stage2/ thirdparty/
	find * -type f '(' -name '*~' -o -name '*.o' -o -name '*.s' ')' -exec rm {} ';'

# 伪目标，没有实际的依赖文件
.PHONY: test clean test-stage2
