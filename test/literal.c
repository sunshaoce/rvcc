#include "test.h"

int main() {
  // [73] 支持字符字面量
  ASSERT(97, 'a');
  ASSERT(10, '\n');
  ASSERT(127, '\x7f');

  // [80] 支持16，8，2进制的数字字面量
  ASSERT(511, 0777);
  ASSERT(0, 0x0);
  ASSERT(10, 0xa);
  ASSERT(10, 0XA);
  ASSERT(48879, 0xbeef);
  ASSERT(48879, 0xBEEF);
  ASSERT(48879, 0XBEEF);
  ASSERT(0, 0b0);
  ASSERT(1, 0b1);
  ASSERT(47, 0b101111);
  ASSERT(47, 0B101111);

  printf("OK\n");
  return 0;
}
