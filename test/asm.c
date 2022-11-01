#include "test.h"

// [259] 支持asm语句
char *asm_fn1(void) {
  asm("li a0, 50\n\t");
}

char *asm_fn2(void) {
  asm inline volatile("li a0, 55\n\t");
}

int main() {
  printf("[259] 支持asm语句\n");
  ASSERT(50, asm_fn1());
  ASSERT(55, asm_fn2());

  printf("OK\n");
  return 0;
}
