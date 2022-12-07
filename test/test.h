#define ASSERT(x, y) assert(x, y, #y)

// [69] 对未定义或未声明的函数报错
void assert(int expected, int actual, char *code);

// [60] 支持函数声明
int printf(char *fmt, ...);

// [107] 为全局变量处理联合体初始化
int strcmp(char *p, char *q);
int memcmp(char *p, char *q, long n);
// [193] 连接相邻的字符串
int strncmp(char *p, char *q, long n);

// [127] 允许调用可变参数函数
int sprintf(char *buf, char *fmt, ...);

// [136] 忽略const volatile auto register restrict _Noreturn
void exit(int n);

// [204] 支持可变参数函数接受任意数量的参数
int vsprintf(char *buf, char *fmt, void *ap);

// [221] 支持__DATE__和__TIME__宏
long strlen(char *s);
