#include "test.h"

int main() {
  // [62] 修正解析复杂类型声明
  ASSERT(1, ({ char x; sizeof(x); }));
  ASSERT(2, ({ short int x; sizeof(x); }));
  ASSERT(2, ({ int short x; sizeof(x); }));
  ASSERT(4, ({ int x; sizeof(x); }));
  ASSERT(8, ({ long int x; sizeof(x); }));
  ASSERT(8, ({ int long x; sizeof(x); }));

  // [63] 支持long long
  ASSERT(8, ({ long long x; sizeof(x); }));
  printf("OK\n");
  return 0;
}
