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

  printf("OK\n");
  return 0;
}
