#include "test.h"
#include <stddef.h>

// [264] 支持offsetof
typedef struct {
  int a;
  char b;
  int c;
  double d;
} T;

int main() {
  printf("[264] 支持offsetof\n");
  ASSERT(0, offsetof(T, a));
  ASSERT(4, offsetof(T, b));
  ASSERT(8, offsetof(T, c));
  ASSERT(16, offsetof(T, d));

  printf("OK\n");
  return 0;
}
