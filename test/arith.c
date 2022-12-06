#include "test.h"

int main() {
  // [1] 返回指定数值
  ASSERT(0, 0);
  ASSERT(42, 42);
  // [2] 支持 + - 运算符
  ASSERT(21, 5+20-4);
  // [3] 支持空格
  ASSERT(41,  12 + 34 - 5 );
  // [5] 支持 * /() 运算符
  ASSERT(47, 5+6*7);
  ASSERT(15, 5*(9-6));
  ASSERT(4, (3+5)/2);
  // [6] 支持一元运算的 + -ASSERT(10, -10 + 20);
  ASSERT(10, - -10);
  ASSERT(10, - - +10);

  // [7] 支持条件运算符
  ASSERT(0, 0==1);
  ASSERT(1, 42==42);
  ASSERT(1, 0!=1);
  ASSERT(0, 42!=42);

  ASSERT(1, 0<1);
  ASSERT(0, 1<1);
  ASSERT(0, 2<1);
  ASSERT(1, 0<=1);
  ASSERT(1, 1<=1);
  ASSERT(0, 2<=1);

  ASSERT(1, 1>0);
  ASSERT(0, 1>1);
  ASSERT(0, 1>2);
  ASSERT(1, 1>=0);
  ASSERT(1, 1>=1);
  ASSERT(0, 1>=2);

  printf("OK\n");
  return 0;
}
