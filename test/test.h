#define ASSERT(x, y) assert(x, y, #y)

// [69] 对未定义或未声明的函数报错
void assert(int expected, int actual, char *code);

// [60] 支持函数声明
int printf();
