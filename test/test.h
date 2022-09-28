#define ASSERT(x, y) assert(x, y, #y)

// [69] 对未定义或未声明的函数报错
void assert(int expected, int actual, char *code);

// [60] 支持函数声明
int printf();

// [107] 为全局变量处理联合体初始化
int strcmp(char *p, char *q);
int memcmp(char *p, char *q, long n);
