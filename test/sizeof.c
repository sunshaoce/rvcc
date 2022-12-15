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
  // [131] 支持无符号整型
  ASSERT(1, sizeof(unsigned char));
  ASSERT(1, sizeof(unsigned char unsigned));

  // [130] 支持signed关键字
  ASSERT(2, sizeof(short));
  ASSERT(2, sizeof(int short));
  ASSERT(2, sizeof(short int));
  ASSERT(2, sizeof(signed short));
  ASSERT(2, sizeof(int short signed));
  // [131] 支持无符号整型
  ASSERT(2, sizeof(unsigned short));
  ASSERT(2, sizeof(int short unsigned));

  // [130] 支持signed关键字
  ASSERT(4, sizeof(int));
  ASSERT(4, sizeof(signed int));
  ASSERT(4, sizeof(signed));
  ASSERT(4, sizeof(signed signed));
  // [131] 支持无符号整型
  ASSERT(4, sizeof(unsigned int));
  ASSERT(4, sizeof(unsigned));
  ASSERT(4, sizeof(unsigned unsigned));

  // [130] 支持signed关键字
  ASSERT(8, sizeof(long));
  ASSERT(8, sizeof(signed long));
  ASSERT(8, sizeof(signed long int));
  // [131] 支持无符号整型
  ASSERT(8, sizeof(unsigned long));
  ASSERT(8, sizeof(unsigned long int));

  // [130] 支持signed关键字
  ASSERT(8, sizeof(long long));
  ASSERT(8, sizeof(signed long long));
  ASSERT(8, sizeof(signed long long int));
  // [131] 支持无符号整型
  ASSERT(8, sizeof(unsigned long long));
  ASSERT(8, sizeof(unsigned long long int));

  ASSERT(1, sizeof((char)1));
  ASSERT(2, sizeof((short)1));
  ASSERT(4, sizeof((int)1));
  ASSERT(8, sizeof((long)1));

  ASSERT(4, sizeof((char)1 + (char)1));
  ASSERT(4, sizeof((short)1 + (short)1));
  ASSERT(4, sizeof(1?2:3));
  ASSERT(4, sizeof(1?(short)2:(char)3));
  ASSERT(8, sizeof(1?(long)2:(char)3));

  // [133] 在一些表达式中用long或ulong替代int
  ASSERT(1, sizeof(char) << 31 >> 31);
  ASSERT(1, sizeof(char) << 63 >> 63);

  // [140] 支持float和double用于局部变量或类型转换
  ASSERT(4, sizeof(float));
  ASSERT(8, sizeof(double));

  // [142] 支持浮点数的 + - *和/
  ASSERT(4, sizeof(1f + 2));
  ASSERT(8, sizeof(1.0+2));
  ASSERT(4, sizeof(1f-2));
  ASSERT(8, sizeof(1.0-2));
  ASSERT(4, sizeof(1f*2));
  ASSERT(8, sizeof(1.0*2));
  ASSERT(4, sizeof(1f/2));
  ASSERT(8, sizeof(1.0/2));

  printf("[280] 支持long double\n");
  ASSERT(16, sizeof(long double));

  printf("[257] [GNU] 支持sizeof函数类型\n");
  ASSERT(1, sizeof(main));

  printf("OK\n");
  return 0;
}
