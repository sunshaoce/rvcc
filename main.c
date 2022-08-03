#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 为每个终结符都设置种类来表示
typedef enum {
  TK_PUNCT, // 操作符如： + -
  TK_NUM,   // 数字
  TK_EOF,   // 文件终止符，即文件的最后
} TokenKind;

// 终结符结构体
typedef struct Token Token;
struct Token {
  TokenKind Kind; // 种类
  Token *Next;    // 指向下一终结符
  int Val;        // 值
  char *Loc;      // 在解析的字符串内的位置
  int Len;        // 长度
};

// 输出错误信息
// static文件内可以访问的函数
// Fmt为传入的字符串， ... 为可变参数，表示Fmt后面所有的参数
static void error(char *Fmt, ...) {
  // 定义一个va_list变量
  va_list VA;
  // VA获取Fmt后面的所有参数
  va_start(VA, Fmt);
  // vfprintf可以输出va_list类型的参数
  vfprintf(stderr, Fmt, VA);
  // 在结尾加上一个换行符
  fprintf(stderr, "\n");
  // 清除VA
  va_end(VA);
  // 终止程序
  exit(1);
}

// 判断Tok的值是否等于指定值，没有用char，是为了后续拓展
static bool equal(Token *Tok, char *Str) {
  // 比较字符串LHS（左部），RHS（右部）的前N位，S2的长度应大于等于N.
  // 比较按照字典序，LHS<RHS回负值，LHS=RHS返回0，LHS>RHS返回正值
  // 同时确保，此处的Op位数=N
  return memcmp(Tok->Loc, Str, Tok->Len) == 0 && Str[Tok->Len] == '\0';
}

// 跳过指定的Str
static Token *skip(Token *Tok, char *Str) {
  if (!equal(Tok, Str))
    error("expect '%s'", Str);
  return Tok->Next;
}

// 返回TK_NUM的值
static int getNumber(Token *Tok) {
  if (Tok->Kind != TK_NUM)
    error("expect a number");
  return Tok->Val;
}

// 生成新的Token
static Token *newToken(TokenKind Kind, char *Start, char *End) {
  // 分配1个Token的内存空间
  Token *Tok = calloc(1, sizeof(Token));
  Tok->Kind = Kind;
  Tok->Loc = Start;
  Tok->Len = End - Start;
  return Tok;
}

// 终结符解析
static Token *tokenize(char *P) {
  Token Head = {};
  Token *Cur = &Head;

  while (*P) {
    // 跳过所有空白符如：空格、回车
    if (isspace(*P)) {
      ++P;
      continue;
    }

    // 解析数字
    if (isdigit(*P)) {
      // 初始化，类似于C++的构造函数
      // 我们不使用Head来存储信息，仅用来表示链表入口，这样每次都是存储在Cur->Next
      // 否则下述操作将使第一个Token的地址不在Head中。
      Cur->Next = newToken(TK_NUM, P, P);
      // 指针前进
      Cur = Cur->Next;
      const char *OldPtr = P;
      Cur->Val = strtoul(P, &P, 10);
      Cur->Len = P - OldPtr;
      continue;
    }

    // 解析操作符
    if (*P == '+' || *P == '-') {
      // 操作符长度都为1
      Cur->Next = newToken(TK_PUNCT, P, P + 1);
      Cur = Cur->Next;
      ++P;
      continue;
    }

    // 处理无法识别的字符
    error("invalid token: %c", *P);
  }

  // 解析结束，增加一个EOF，表示终止符。
  Cur->Next = newToken(TK_EOF, P, P);
  // Head无内容，所以直接返回Next
  return Head.Next;
}

int main(int Argc, char **Argv) {
  // 判断传入程序的参数是否为2个，Argv[0]为程序名称，Argv[1]为传入的第一个参数
  if (Argc != 2) {
    // 异常处理，提示参数数量不对。
    // fprintf，格式化文件输出，往文件内写入字符串
    // stderr，异常文件（Linux一切皆文件），用于往屏幕显示异常信息
    // %s，字符串
    error("%s: invalid number of arguments", Argv[0]);
  }

  // 解析Argv[1]
  Token *Tok = tokenize(Argv[1]);

  // 声明一个全局main段，同时也是程序入口段
  printf("  .globl main\n");
  // main段标签
  printf("main:\n");
  // li为addi别名指令，加载一个立即数到寄存器中

  // 这里我们将算式分解为 num (op num) (op num)...的形式
  // 所以先将第一个num传入a0
  printf("  li a0, %d\n", getNumber(Tok));
  Tok = Tok->Next;

  // 解析 (op num)
  while (Tok->Kind != TK_EOF) {
    if (equal(Tok, "+")) {
      Tok = Tok->Next;
      printf("  addi a0, a0, %d\n", getNumber(Tok));
      Tok = Tok->Next;
      continue;
    }

    // 不是+，则判断-
    // 没有subi指令，但是addi支持有符号数，所以直接对num取反
    Tok = skip(Tok, "-");
    printf("  addi a0, a0, -%d\n", getNumber(Tok));
    Tok = Tok->Next;
  }

  // ret为jalr x0, x1, 0别名指令，用于返回子程序
  // 返回的为a0的值
  printf("  ret\n");

  return 0;
}
