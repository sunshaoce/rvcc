// [179] 使用内建的预处理器用于所有测试
#include "test.h"

// [160] 支持 #include "..."
#include "include1.h"

// [189] 支持 __FILE__ 和 __LINE__
char *main_filename1 = __FILE__;
int main_line1 = __LINE__;
#define LINE() __LINE__
int main_line2 = LINE();

// [159] 支持空指示
#

/* */ #

// [172] 支持 #define 零参宏函数
int ret3(void) { return 3; }

// [176] 宏函数中只展开一次
int dbl(int x) { return x * x; }

// [190] 支持 __VA_ARGS__
int add2(int x, int y) {
  return x + y;
}

int add6(int a, int b, int c, int d, int e, int f) {
  return a + b + c + d + e + f;
}

int main() {
  printf("[160] 支持 #include \"...\"\n");
  ASSERT(5, include1);
  ASSERT(7, include2);

  printf("[163] 支持 #if 和 #endif\n");
#if 0
#include "/no/such/file"
  ASSERT(0, 1);

  // [164] 在值为假的#if语句中，跳过嵌套的 #if 语句
  #if nested
  #endif
#endif

  int m = 0;

#if 1
  m = 5;
#endif
  ASSERT(5, m);

  printf("[165] 支持 #else");
#if 1
# if 0
#  if 1
    foo bar
#  endif
# endif
      m = 3;
#endif
    ASSERT(3, m);

#if 1-1
# if 1
# endif
# if 1
# else
# endif
# if 0
# else
# endif
  m = 2;
#else
# if 1
  m = 3;
# endif
#endif
  ASSERT(3, m);

#if 1
  m = 2;
#else
  m = 3;
#endif
  ASSERT(2, m);

  printf("[166] 支持 #elif\n");
#if 1
  m = 2;
#else
  m = 3;
#endif
  ASSERT(2, m);

#if 0
  m = 1;
#elif 0
  m = 2;
#elif 3 + 5
  m = 3;
#elif 1 * 5
  m = 4;
#endif
  ASSERT(3, m);

#if 1 + 5
  m = 1;
#elif 1
  m = 2;
#elif 3
  m = 2;
#endif
  ASSERT(1, m);

#if 0
  m = 1;
#elif 1
#if 1
  m = 2;
#else
  m = 3;
#endif
#else
  m = 5;
#endif
  ASSERT(2, m);

  printf("[167] 支持 #define\n");
  int M1 = 5;

#define M1 3
  ASSERT(3, M1);
#define M1 4
  ASSERT(4, M1);

#define M1 3+4+
  ASSERT(12, M1 5);

#define M1 3+4
  ASSERT(23, M1*5);

#define ASSERT_ assert(
#define if 5
#define five "5"
#define END )
  ASSERT_ 5, if, five END;

  printf(" [168] 支持 #undef\n");
#undef ASSERT_
#undef if
#undef five
#undef END

  if (0);

  printf("[169] 展开 #if 和 #elif 中的参数\n");
#define M 5
#if M
  m = 5;
#else
  m = 6;
#endif
  ASSERT(5, m);

#define M 5
#if M-5
  m = 6;
#elif M
  m = 5;
#endif
  ASSERT(5, m);

  printf("[170] 宏中只展开一次\n");
  int M2 = 6;
#define M2 M2 + 3
  ASSERT(9, M2);

#define M3 M2 + 3
  ASSERT(12, M3);

  int M4 = 3;
#define M4 M5 * 5
#define M5 M4 + 2
  ASSERT(13, M4);

  printf("[171] 支持 #ifdef 和 #ifndef\n");
#ifdef M6
  m = 5;
#else
  m = 3;
#endif
  ASSERT(3, m);

#define M6
#ifdef M6
  m = 5;
#else
  m = 3;
#endif
  ASSERT(5, m);

#ifndef M7
  m = 3;
#else
  m = 5;
#endif
  ASSERT(3, m);

#define M7
#ifndef M7
  m = 3;
#else
  m = 5;
#endif
  ASSERT(5, m);

#if 0
#ifdef NO_SUCH_MACRO
#endif
#ifndef NO_SUCH_MACRO
#endif
#else
#endif

  printf("[172] 支持 #define 零参宏函数\n");
#define M7() 1
  int M7 = 5;
  ASSERT(1, M7());
  ASSERT(5, M7);

#define M7 ()
  ASSERT(3, ret3 M7);

  printf("[173] 支持 #define 多参宏函数\n");
#define M8(x, y) x + y
  ASSERT(7, M8(3, 4));

#define M8(x, y) x *y
  ASSERT(24, M8(3 + 4, 4 + 5));

#define M8(x, y) (x) * (y)
  ASSERT(63, M8(3 + 4, 4 + 5));

  printf("[174] 支持空的宏参数\n");
#define M8(x, y) x y
  ASSERT(9, M8(, 4 + 5));

  printf("[175] 允许括号内的表达式作为宏参数\n");
#define M8(x, y) x *y
  ASSERT(20, M8((2 + 3), 4));

#define M8(x, y) x *y
  ASSERT(12, M8((2, 3), 4));

  printf("[176] 宏函数中只展开一次\n");
#define dbl(x) M10(x) * x
#define M10(x) dbl(x) + 3
  ASSERT(10, dbl(2));

  printf("[177] 支持宏字符化操作符#\n");
#define M11(x) #x
  ASSERT('a', M11( a!b  `""c)[0]);
  ASSERT('!', M11( a!b  `""c)[1]);
  ASSERT('b', M11( a!b  `""c)[2]);
  ASSERT(' ', M11( a!b  `""c)[3]);
  ASSERT('`', M11( a!b  `""c)[4]);
  ASSERT('"', M11( a!b  `""c)[5]);
  ASSERT('"', M11( a!b  `""c)[6]);
  ASSERT('c', M11( a!b  `""c)[7]);
  ASSERT(0,   M11( a!b  `""c)[8]);

  printf("[178] 支持宏 ## 操作符\n");
#define paste(x,y) x##y
  ASSERT(15, paste(1,5));
  ASSERT(255, paste(0,xff));
  ASSERT(3, ({ int foobar=3; paste(foo,bar); }));
  ASSERT(5, paste(5,));
  ASSERT(5, paste(,5));

#define i 5
  ASSERT(101, ({ int i3=100; paste(1+i,3); }));
#undef i

#define paste2(x) x##5
  ASSERT(26, paste2(1+2));

#define paste3(x) 2##x
  ASSERT(23, paste3(1+2));

#define paste4(x, y, z) x##y##z
  ASSERT(123, paste4(1,2,3));

  printf("[180] 支持 defined() 宏操作符\n");
#define M12
#if defined(M12)
  m = 3;
#else
  m = 4;
#endif
  ASSERT(3, m);

#define M12
#if defined M12
  m = 3;
#else
  m = 4;
#endif
  ASSERT(3, m);

#if defined(M12) - 1
  m = 3;
#else
  m = 4;
#endif
  ASSERT(4, m);

#if defined(NO_SUCH_MACRO)
  m = 3;
#else
  m = 4;
#endif
  ASSERT(4, m);

  printf("[181] 在常量表达式中替代遗留的标志符为0\n");
#if no_such_symbol == 0
  m = 5;
#else
  m = 6;
#endif
  ASSERT(5, m);

  printf("[182] 宏展开时保留新行和空格\n");
#define STR(x) #x
#define M12(x) STR(x)
#define M13(x) M12(foo.x)
  ASSERT(0, strcmp(M13(bar), "foo.bar"));

#define M13(x) M12(foo. x)
  ASSERT(0, strcmp(M13(bar), "foo. bar"));

#define M12 foo
#define M13(x) STR(x)
#define M14(x) M13(x.M12)
  ASSERT(0, strcmp(M14(bar), "bar.foo"));

#define M14(x) M13(x. M12)
  ASSERT(0, strcmp(M14(bar), "bar. foo"));

  printf("[184] 支持 #include <...>\n");
#include "include3.h"
  ASSERT(3, foo);

#include "include4.h"
  ASSERT(4, foo);

#define M13 "include3.h"
#include M13
  ASSERT(3, foo);

#define M13 < include4.h
#include M13 >
  ASSERT(4, foo);

#undef foo

  printf("[189] 支持 __FILE__ 和 __LINE__\n");
  ASSERT(0, strcmp(main_filename1, "test/macro.c"));
  ASSERT(9, main_line1);
  ASSERT(11, main_line2);
  ASSERT(0, strcmp(include1_filename, "test/include1.h"));
  ASSERT(5, include1_line);

// [190] 支持 __VA_ARGS__
#define M14(...) 3
  ASSERT(3, M14());

#define M14(...) __VA_ARGS__
  ASSERT(2, M14() 2);
  ASSERT(5, M14(5));

#define M14(...) add2(__VA_ARGS__)
  ASSERT(8, M14(2, 6));

#define M14(...) add6(1,2,__VA_ARGS__,6)
  ASSERT(21, M14(3,4,5));

#define M14(x, ...) add6(1,2,x,__VA_ARGS__,6)
  ASSERT(21, M14(3,4,5));

  printf("[253] [GNU] 支持GCC风格的可变参数宏\n");
#define M14(args...) 3
  ASSERT(3, M14());

#define M14(x, ...) x
  ASSERT(5, M14(5));

  printf("[253] [GNU] 支持GCC风格的可变参数宏\n");
#define M14(args...) args
  ASSERT(2, M14() 2);
  ASSERT(5, M14(5));

#define M14(args...) add2(args)
  ASSERT(8, M14(2, 6));

#define M14(args...) add6(1,2,args,6)
  ASSERT(21, M14(3,4,5));

#define M14(x, args...) add6(1,2,x,args,6)
  ASSERT(21, M14(3,4,5));

#define M14(x, args...) x
  ASSERT(5, M14(5));

// [207] 解析数值终结符为pp-number
#define CONCAT(x,y) x##y
  ASSERT(5, ({ int f0zz=5; CONCAT(f,0zz); }));
  ASSERT(5, ({ CONCAT(4,.57) + 0.5; }));

  printf("[221] 支持__DATE__和__TIME__宏\n");
  ASSERT(11, strlen(__DATE__));
  ASSERT(8, strlen(__TIME__));

  printf("[222] [GNU] 支持__COUNTER__宏\n");
  ASSERT(0, __COUNTER__);
  ASSERT(1, __COUNTER__);
  ASSERT(2, __COUNTER__);

  printf("[248] [GNU] 支持__TIMESTAMP__宏\n");
  ASSERT(24, strlen(__TIMESTAMP__));

  printf("[249] [GNU] 支持__BASE_FILE__宏\n");
  ASSERT(0, strcmp(__BASE_FILE__, "test/macro.c"));

  printf("[250] 支持__VA_OPT__\n");
#define M30(buf, fmt, ...) sprintf(buf, fmt __VA_OPT__(, ) __VA_ARGS__)
  ASSERT(0, ({ char buf[100]; M30(buf, "foo"); strcmp(buf, "foo"); }));
  ASSERT(0, ({ char buf[100]; M30(buf, "foo%d", 3); strcmp(buf, "foo3"); }));
  ASSERT(0, ({ char buf[100]; M30(buf, "foo%d%d", 3, 5); strcmp(buf, "foo35"); }));

  printf("[251] [GNU] 处理 ## __VA_ARGS__\n");
#define M31(buf, fmt, ...) sprintf(buf, fmt, ##__VA_ARGS__)
  ASSERT(0, ({ char buf[100]; M31(buf, "foo"); strcmp(buf, "foo"); }));
  ASSERT(0, ({ char buf[100]; M31(buf, "foo%d", 3); strcmp(buf, "foo3"); }));
  ASSERT(0, ({ char buf[100]; M31(buf, "foo%d%d", 3, 5); strcmp(buf, "foo35"); }));

#define M31(x, y) (1, ##x y)
  ASSERT(3, M31(, 3));

  printf("OK\n");
  return 0;
}
