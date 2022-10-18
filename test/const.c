#include "test.h"

int main() {
  // [136] 忽略const volatile auto register restrict _Noreturn
  { const x; }
  { int const x; }
  { const int x; }
  { const int const const x; }
  ASSERT(5, ({ const x = 5; x; }));
  ASSERT(8, ({ const x = 8; int *const y=&x; *y; }));
  ASSERT(6, ({ const x = 6; *(const * const)&x; }));

  printf("OK\n");
  return 0;
}
