#include <stdio.h>
#include <stdlib.h>

int main(int Argc, char **Argv) {
  // 判断传入程序的参数是否为2个，Argv[0]为程序名称，Argv[1]为传入的第一个参数
  if (Argc != 2) {
    // 异常处理，提示参数数量不对。
    // fprintf，格式化文件输出，往文件内写入字符串
    // stderr，异常文件（Linux一切皆文件），用于往屏幕显示异常信息
    // %s，字符串
    fprintf(stderr, "%s: invalid number of arguments\n", Argv[0]);
    // 程序返回值不为0时，表示存在错误
    return 1;
  }

  // P保存着输入的算式的str
  char *P = Argv[1];

  // 声明一个全局main段，同时也是程序入口段
  printf("  .globl main\n");
  // main段标签
  printf("main:\n");
  // li为addi别名指令，加载一个立即数到寄存器中

  // 传入程序的参数为str类型，因为需要转换为需要long类型
  // strtol为“string to long”，
  // 参数为：被转换的str，str除去数字后的剩余部分，进制
  // 传入&P，即char**, 是为了修改P的值

  // 这里我们将算式分解为 num (op num) (op num)...的形式
  // 所以先将第一个num传入a0
  printf("  li a0, %ld\n", strtol(P, &P, 10));

  // 解析 (op num)
  // *P在这里判断P是否为NULL
  while (*P) {
    // 解析op
    if (*P == '+') {
      ++P; // 跳过‘+’
      // 解析num
      // addi rd, rs1, imm 表示 rd = rs1 + imm
      printf("  addi a0, a0, %ld\n", strtol(P, &P, 10));
      continue;
    }

    if (*P == '-') {
      ++P;
      // 解析num
      // addi中imm为有符号立即数，所以减法表示为 rd = rs1 + (-imm)
      printf("  addi a0, a0, -%ld\n", strtol(P, &P, 10));
      continue;
    }

    // 如果存在未解析的字符，则报错
    fprintf(stderr, "unexpected character: '%c'\n", *P);
    return 1;
  }

  // ret为jalr x0, x1, 0别名指令，用于返回子程序
  // 返回的为a0的值
  printf("  ret\n");

  return 0;
}
