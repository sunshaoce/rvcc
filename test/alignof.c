#include "test.h"

// [118] 支持_Alignof和_Alignas
int _Alignas(512) g1;
int _Alignas(512) g2;
char g3;
int g4;
long g5;
char g6;

int main() {
  // [118] 支持_Alignof和_Alignas
  ASSERT(1, _Alignof(char));
  ASSERT(2, _Alignof(short));
  ASSERT(4, _Alignof(int));
  ASSERT(8, _Alignof(long));
  ASSERT(8, _Alignof(long long));
  ASSERT(1, _Alignof(char[3]));
  ASSERT(4, _Alignof(int[3]));
  ASSERT(1, _Alignof(struct {char a; char b;}[2]));
  ASSERT(8, _Alignof(struct {char a; long b;}[2]));

  ASSERT(1, ({ _Alignas(char) char x, y; &y-&x; }));
  ASSERT(8, ({ _Alignas(long) char x, y; &y-&x; }));
  ASSERT(32, ({ _Alignas(32) char x, y; &y-&x; }));
  ASSERT(32, ({ _Alignas(32) int *x, *y; ((char *)&y)-((char *)&x); }));
  ASSERT(16, ({ struct { _Alignas(16) char x, y; } a; &a.y-&a.x; }));
  ASSERT(8, ({ struct T { _Alignas(8) char a; }; _Alignof(struct T); }));

  ASSERT(0, (long)(char *)&g1 % 512);
  ASSERT(0, (long)(char *)&g2 % 512);
  ASSERT(0, (long)(char *)&g4 % 4);
  ASSERT(0, (long)(char *)&g5 % 8);

  // [119] 支持对变量使用_Alignof
  ASSERT(1, ({ char x; _Alignof(x); }));
  ASSERT(4, ({ int x; _Alignof(x); }));
  ASSERT(1, ({ char x; _Alignof x; }));
  ASSERT(4, ({ int x; _Alignof x; }));

  // [133] 在一些表达式中用long或ulong替代int
  ASSERT(1, _Alignof(char) << 31 >> 31);
  ASSERT(1, _Alignof(char) << 63 >> 63);
  ASSERT(1, ({ char x; _Alignof(x) << 63 >> 63; }));

  printf("[218] 数组超过16字节时，对齐值至少为16字节\n");
  ASSERT(0, ({ char x[16]; (unsigned long)&x % 16; }));
  ASSERT(0, ({ char x[17]; (unsigned long)&x % 16; }));
  ASSERT(0, ({ char x[100]; (unsigned long)&x % 16; }));
  ASSERT(0, ({ char x[101]; (unsigned long)&x % 16; }));

  printf("OK\n");
  return 0;
}
