#include "test.h"
#include <pthread.h>
#include <stdatomic.h>

static int incr(_Atomic int *p) {
  int oldval = *p;
  int newval;
  do {
    newval = oldval + 1;
  } while (!atomic_compare_exchange_weak(p, &oldval, newval));
  return newval;
}

static int add1(void *arg) {
  _Atomic int *x = arg;
  for (int i = 0; i < 1000 * 1000; i++)
    incr(x);
  return 0;
}

static int add2(void *arg) {
  _Atomic int *x = arg;
  for (int i = 0; i < 1000 * 1000; i++)
    (*x)++;
  return 0;
}

static int add3(void *arg) {
  _Atomic int *x = arg;
  for (int i = 0; i < 1000 * 1000; i++)
    *x += 5;
  return 0;
}

static int add_millions(void) {
  _Atomic int x = 0;

  pthread_t thr1;
  pthread_t thr2;
  pthread_t thr3;

  pthread_create(&thr1, NULL, add1, &x);
  pthread_create(&thr2, NULL, add2, &x);
  pthread_create(&thr3, NULL, add3, &x);

  for (int i = 0; i < 1000 * 1000; i++)
    x--;

  pthread_join(thr1, NULL);
  pthread_join(thr2, NULL);
  pthread_join(thr3, NULL);
  return x;
}

int main() {
  printf("[309] 支持_Atomic及一些原子操作\n");
  ASSERT(6 * 1000 * 1000, add_millions());

  printf("[308] 支持原子交换操作\n");
  ASSERT(3, ({ int x=3; atomic_exchange(&x, 5); }));
  ASSERT(5, ({ int x=3; atomic_exchange(&x, 5); x; }));

  printf("OK\n");
  return 0;
}
