#include "test.h"

struct foo_1 {float f;union {int x;};};
int test_1(struct foo_1 in) { return in.f; }
int test_2(struct foo_1 in) { return in.x; }

int main() {
  // [54] 支持union
  ASSERT(8, ({ union { int a; char b[6]; } x; sizeof(x); }));
  ASSERT(3, ({ union { int a; char b[4]; } x; x.a = 515; x.b[0]; }));
  ASSERT(2, ({ union { int a; char b[4]; } x; x.a = 515; x.b[1]; }));
  ASSERT(0, ({ union { int a; char b[4]; } x; x.a = 515; x.b[2]; }));
  ASSERT(0, ({ union { int a; char b[4]; } x; x.a = 515; x.b[3]; }));

  // [55] 支持结构体赋值
  ASSERT(3, ({ union {int a,b;} x,y; x.a=3; y.a=5; y=x; y.a; }));
  ASSERT(3, ({ union {struct {int a,b;} c;} x,y; x.c.b=3; y.c.b=5; y=x; y.c.b; }));

  printf("[220] 支持匿名结构体和联合体\n");
  ASSERT(0xef, ({ union { struct { unsigned char a,b,c,d; }; long e; } x; x.e=0xdeadbeef; x.a; }));
  ASSERT(0xbe, ({ union { struct { unsigned char a,b,c,d; }; long e; } x; x.e=0xdeadbeef; x.b; }));
  ASSERT(0xad, ({ union { struct { unsigned char a,b,c,d; }; long e; } x; x.e=0xdeadbeef; x.c; }));
  ASSERT(0xde, ({ union { struct { unsigned char a,b,c,d; }; long e; } x; x.e=0xdeadbeef; x.d; }));

  ASSERT(3, ({struct { union { int a,b; }; union { int c,d; }; } x; x.a=3; x.b; }));
  ASSERT(5, ({struct { union { int a,b; }; union { int c,d; }; } x; x.d=5; x.c; }));

  ASSERT(1, ({ struct foo_1 in;in.f=1.0; in.x=2; test_1(in);}));
  ASSERT(2, ({ struct foo_1 in;in.f=1.0; in.x=2; test_2(in);}));

  printf("OK\n");
  return 0;
}
