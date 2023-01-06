#include "test.h"
#include <pthread.h>
#include <stdio.h>

// [267] 支持线程局部变量
_Thread_local int v1;
_Thread_local int v2 = 5;
int v3 = 7;

int thread_main(void *unused) {
  ASSERT(0, v1);
  ASSERT(5, v2);
  ASSERT(7, v3);

  v1 = 1;
  v2 = 2;
  v3 = 3;

  ASSERT(1, v1);
  ASSERT(2, v2);
  ASSERT(3, v3);

  return 0;
}

int main() {
  printf("[267] 支持线程局部变量\n");
  pthread_t thr;

  ASSERT(0, v1);
  ASSERT(5, v2);
  ASSERT(7, v3);

  ASSERT(0, pthread_create(&thr, NULL, thread_main, NULL));
  ASSERT(0, pthread_join(thr, NULL));

  ASSERT(0, v1);
  ASSERT(5, v2);
  ASSERT(3, v3);

  printf("OK\n");
  return 0;
}
