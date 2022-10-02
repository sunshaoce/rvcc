#include "test.h"

int main() {
  // [65] 支持对类型进行sizeof
  ASSERT(1, sizeof(char));
  ASSERT(2, sizeof(short));
  ASSERT(2, sizeof(short int));
  ASSERT(2, sizeof(int short));
  ASSERT(4, sizeof(int));
  ASSERT(8, sizeof(long));
  ASSERT(8, sizeof(long int));
  ASSERT(8, sizeof(long int));
  ASSERT(8, sizeof(char *));
  ASSERT(8, sizeof(int *));
  ASSERT(8, sizeof(long *));
  ASSERT(8, sizeof(int **));
  ASSERT(8, sizeof(int(*)[4]));
  ASSERT(32, sizeof(int*[4]));
  ASSERT(16, sizeof(int[4]));
  ASSERT(48, sizeof(int[3][4]));
  ASSERT(8, sizeof(struct {int a; int b;}));

  // [68] 实现常规算术转换
  ASSERT(8, sizeof(-10 + (long)5));
  ASSERT(8, sizeof(-10 - (long)5));
  ASSERT(8, sizeof(-10 * (long)5));
  ASSERT(8, sizeof(-10 / (long)5));
  ASSERT(8, sizeof((long)-10 + 5));
  ASSERT(8, sizeof((long)-10 - 5));
  ASSERT(8, sizeof((long)-10 * 5));
  ASSERT(8, sizeof((long)-10 / 5));

  // [78] 支持前置++和--
  ASSERT(1, ({ char i; sizeof(++i); }));
  // [79] 支持后置++和--
  ASSERT(1, ({ char i; sizeof(i++); }));

  // [86] 增加不完整数组类型的概念
  ASSERT(8, sizeof(int(*)[10]));
  ASSERT(8, sizeof(int(*)[][10]));

  // [112] 支持灵活数组成员
  ASSERT(4, sizeof(struct { int x, y[]; }));

  // [130] 支持signed关键字
  ASSERT(1, sizeof(char));
  ASSERT(1, sizeof(signed char));
  ASSERT(1, sizeof(signed char signed));

  ASSERT(2, sizeof(short));
  ASSERT(2, sizeof(int short));
  ASSERT(2, sizeof(short int));
  ASSERT(2, sizeof(signed short));
  ASSERT(2, sizeof(int short signed));

  ASSERT(4, sizeof(int));
  ASSERT(4, sizeof(signed int));
  ASSERT(4, sizeof(signed));
  ASSERT(4, sizeof(signed signed));

  ASSERT(8, sizeof(long));
  ASSERT(8, sizeof(signed long));
  ASSERT(8, sizeof(signed long int));

  ASSERT(8, sizeof(long long));
  ASSERT(8, sizeof(signed long long));
  ASSERT(8, sizeof(signed long long int));

  printf("OK\n");
  return 0;
}
