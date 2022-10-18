#include "test.h"

_Noreturn noreturn_fn(int restrict x) {
  exit(0);
}

int main() {
  // [136] 忽略const volatile auto register restrict _Noreturn
  { volatile x; }
  { int volatile x; }
  { volatile int x; }
  { volatile int volatile volatile x; }
  { int volatile * volatile volatile x; }
  { auto ** restrict __restrict __restrict__ const volatile *x; }

  printf("OK\n");
  return 0;
}
