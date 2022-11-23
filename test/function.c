#include "test.h"

int ret3(void) { // [114] 支持void作为形参
  return 3;
  return 5;
}

int add2(int x, int y) {
  return x + y;
}

int sub2(int x, int y) {
  return x - y;
}

int add6(int a, int b, int c, int d, int e, int f) {
  return a + b + c + d + e + f;
}

int addx(int *x, int y) {
  return *x + y;
}

int sub_char(char a, char b, char c) {
  return a - b - c;
}

int fib(int x) {
  if (x<=1)
    return 1;
  return fib(x-1) + fib(x-2);
}

// [57] 支持long类型
int sub_long(long a, long b, long c) {
  return a - b - c;
}

// [58] 支持short类型
int sub_short(short a, short b, short c) {
  return a - b - c;
}

// [70] 处理返回类型转换
int g1;

int *g1_ptr(void) { return &g1; } // [114] 支持void作为形参
char int_to_char(int x) { return x; }

// {71] 处理函数实参类型转换
int div_long(long a, long b) {
  return a / b;
}

// [72] 支持_Bool类型
_Bool bool_fn_add(_Bool x) { return x + 1; }
_Bool bool_fn_sub(_Bool x) { return x - 1; }

// [75] 支持文件域内函数
// [114] 支持void作为形参
static int static_fn(void) { return 3; }

// [87] 在函数形参中退化数组为指针
int param_decay(int x[]) { return x[0]; }

// [120] 支持静态局部变量
int counter() {
  static int i;
  static int j = 1+1;
  return i++ + j++;
}

// [122] 支持空返回语句
void ret_none() { return; }

// [126] 支持函数返回短整数
_Bool true_fn();
_Bool false_fn();
char char_fn();
short short_fn();

// [127] 允许调用可变参数函数
int add_all(int n, ...);

// [128] 增加__va_area__以支持可变参数函数
typedef void *va_list;

int sprintf(char *buf, char *fmt, ...);
int vsprintf(char *buf, char *fmt, va_list ap);

char *fmt(char *buf, char *fmt, ...) {
  va_list ap = __va_area__;
  vsprintf(buf, fmt, ap);
}

// [129] 设置空参函数调用为可变的
int nullParam() { return 123; }

// [131] 支持无符号整型
unsigned char uchar_fn();
unsigned short ushort_fn();

signed char schar_fn();
short sshort_fn();

// [144] 允许函数使用浮点数
double add_double(double x, double y);
float add_float(float x, float y);

// [145] 允许使用浮点数定义函数
float add_float3(float x, float y, float z) {
  return x + y + z;
}

double add_double3(double x, double y, double z) {
  return x + y + z;
}

int main() {
  // [25] 支持零参函数定义
  ASSERT(3, ret3());
  // [26] 支持最多6个参数的函数定义
  ASSERT(8, add2(3, 5));
  ASSERT(2, sub2(5, 3));
  ASSERT(21, add6(1,2,3,4,5,6));
  ASSERT(66, add6(1,2,add6(3,4,5,6,7,8),9,10,11));
  ASSERT(136, add6(1,2,add6(3,add6(4,5,6,7,8,9),10,11,12,13),14,15,16));

  ASSERT(7, add2(3,4));
  ASSERT(1, sub2(4,3));
  ASSERT(55, fib(9));

  ASSERT(1, ({ sub_char(7, 3, 3); }));

  // [70] 处理返回类型转换
  g1 = 3;

  ASSERT(3, *g1_ptr());
  ASSERT(5, int_to_char(261));

  // {71] 处理函数实参类型转换
  ASSERT(-5, div_long(-10, 2));

  // [72] 支持_Bool类型
  ASSERT(1, bool_fn_add(3));
  ASSERT(0, bool_fn_sub(3));
  ASSERT(1, bool_fn_add(-3));
  ASSERT(0, bool_fn_sub(-3));
  ASSERT(1, bool_fn_add(0));
  ASSERT(1, bool_fn_sub(0));

  // [75] 支持文件域内函数
  ASSERT(3, static_fn());

  // [87] 在函数形参中退化数组为指针
  ASSERT(3, ({ int x[2]; x[0]=3; param_decay(x); }));

  // [120] 支持静态局部变量
  ASSERT(2, counter());
  ASSERT(4, counter());
  ASSERT(6, counter());

  // [122] 支持空返回语句
  ret_none();

  // [126] 支持函数返回短整数
  ASSERT(1, true_fn());
  ASSERT(0, false_fn());
  ASSERT(3, char_fn());
  ASSERT(5, short_fn());

  // [127] 允许调用可变参数函数
  ASSERT(6, add_all(3,1,2,3));
  ASSERT(5, add_all(4,1,2,3,-1));

  // [128] 增加__va_area__以支持可变参数函数
  { char buf[100]; fmt(buf, "%d %d %s", 1, 2, "foo"); printf("%s\n", buf); }

  ASSERT(0, ({ char buf[100]; sprintf(buf, "%d %d %s", 1, 2, "foo"); strcmp("1 2 foo", buf); }));

  ASSERT(0, ({ char buf[100]; fmt(buf, "%d %d %s", 1, 2, "foo"); strcmp("1 2 foo", buf); }));

  // [129] 设置空参函数调用为可变的
  ASSERT(123, ({ nullParam(); }));

  // [131] 支持无符号整型
  ASSERT(251, uchar_fn());
  ASSERT(65528, ushort_fn());
  ASSERT(-5, schar_fn());
  ASSERT(-8, sshort_fn());

  // [144] 允许使用浮点数调用函数
  ASSERT(6, add_float(2.3, 3.8));
  ASSERT(6, add_double(2.3, 3.8));

  // [145] 允许使用浮点数定义函数
  ASSERT(7, add_float3(2.5, 2.5, 2.5));
  ASSERT(7, add_double3(2.5, 2.5, 2.5));

  printf("OK\n");
  return 0;
}
