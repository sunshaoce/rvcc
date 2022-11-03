int assert(int expected, int actual, char *code);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int strcmp(char *p, char *q);
int memcmp(char *p, char *q, long n);

// [160] 支持 #include "..."
#include "include1.h"

// [159] 支持空指示
#

/* */ #

int main() {
  printf("[160] 支持 #include \"...\"");
  assert(5, include1, "include1");
  assert(7, include2, "include2");

  printf("[163] 支持 #if 和 #endif\n");
#if 0
#include "/no/such/file"
  assert(0, 1, "1");
#endif

  int m = 0;

#if 1
  m = 5;
#endif
  assert(5, m, "m");

  printf("OK\n");
  return 0;
}
