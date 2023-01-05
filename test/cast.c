#include "test.h"

int main() {
  // [67] 支持类型转换
  ASSERT(131585, (int)8590066177);
  ASSERT(513, (short)8590066177);
  ASSERT(1, (char)8590066177);
  ASSERT(1, (long)1);
  ASSERT(0, (long)&*(int *)0);
  ASSERT(513, ({ int x=512; *(char *)&x=1; x; }));
  ASSERT(5, ({ int x=5; long y=(long)&x; *(int*)y; }));

  (void)1;

  printf("[131] 支持无符号整型");
  ASSERT(255, (char)255);
  ASSERT(-1, (signed char)255);
  ASSERT(255, (unsigned char)255);
  ASSERT(-1, (short)65535);
  ASSERT(65535, (unsigned short)65535);
  ASSERT(-1, (int)0xffffffff);
  ASSERT(0xffffffff, (unsigned)0xffffffff);
  ASSERT(1, 0 + (unsigned)-1 == -1);

  ASSERT(1, -1<1);
  ASSERT(0, -1<(unsigned)1);
  ASSERT(254, (char)127+(char)127);
  ASSERT(65534, (short)32767+(short)32767);
  ASSERT(-1, -1>>1);
  ASSERT(1, (unsigned)-1==4294967295);
  ASSERT(-1, (unsigned long)-1);
  ASSERT(2147483647, ((unsigned)-1)>>1);
  ASSERT(-50, (-100)/2);
  ASSERT(2147483598, ((unsigned)-100)/2);
  ASSERT(9223372036854775758, ((unsigned long)-100)/2);
  ASSERT(0, ((long)-1)/(unsigned)100);
  ASSERT(-2, (-100)%7);
  ASSERT(2, ((unsigned)-100)%7);
  ASSERT(6, ((unsigned long)-100)%9);

  ASSERT(65535, (int)(unsigned short)65535);
  ASSERT(65535, ({ unsigned short x = 65535; x; }));
  ASSERT(65535, ({ unsigned short x = 65535; (int)x; }));

  ASSERT(-1, ({ typedef short T; T x = 65535; (int)x; }));
  ASSERT(65535, ({ typedef unsigned short T; T x = 65535; (int)x; }));
  ASSERT(0, (unsigned)18446744073709551615UL < (signed char)(-2L));
  ASSERT(1, 115 >= -(unsigned)(4294967286UL));

  printf("OK\n");
  return 0;
}
