# C编译器参数：使用C11标准，生成debug信息，禁止将未初始化的全局变量放入到common段
CFLAGS=-std=c11 -g -fno-common
# 指定C编译器，来构建项目
CC=clang

# rvcc标签，表示如何构建最终的二进制文件，依赖于main.o文件
rvcc: main.o
# 将多个*.o文件编译为rvcc
	$(CC) -o rvcc $(CFLAGS) main.o

# 测试标签，运行测试脚本
test: rvcc
	./test.sh

# 清理标签，清理所有非源代码文件
clean:
	rm -f rvcc *.o *.s tmp* a.out

# 伪目标，没有实际的依赖文件
.PHONY: test clean
