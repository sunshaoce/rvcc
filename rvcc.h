#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// 共用头文件，定义了多个文件间共同使用的函数和数据
//

//
// 标记符分析，词法分析
//

// 为每个终结符都设置类型来表示
typedef enum {
  TK_IDENT, // 标记符，可以为变量名、函数名等
  TK_PUNCT, // 操作符如： + -
  TK_NUM,   // 数字
  TK_EOF,   // 文件终止符，即文件的最后
} TokenKind;

// 标记符结构体
typedef struct Token Token;
struct Token {
  TokenKind Kind; // 类型
  Token *Next;    // 指向下一终结符
  int Val;        // 值
  char *Loc;      // 在解析的字符串内的位置
  int Len;        // 长度
};

// 去除了static用以在多个文件间访问
// 报错函数
void error(char *Fmt, ...);
void errorAt(char *Loc, char *Fmt, ...);
void errorTok(Token *Tok, char *Fmt, ...);
// 判断Token与Str的关系
bool equal(Token *Tok, char *Str);
Token *skip(Token *Tok, char *Str);
// 词法分析
Token *tokenize(char *Input);

//
// 生成AST（抽象语法树），语法解析
//

// AST的节点类型
typedef enum {
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_NEG,       // 负号-
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=
  ND_ASSIGN,    // 赋值
  ND_EXPR_STMT, // 表达式语句
  ND_VAR,       // 变量
  ND_NUM,       // 整形
} NodeKind;

// AST中二叉树节点
typedef struct Node Node;
struct Node {
  NodeKind Kind; // 节点类型
  Node *Next;    // 下一节点，指代下一语句
  Node *LHS;     // 左部，left-hand side
  Node *RHS;     // 右部，right-hand side
  char Name;     // 存储ND_VAR的字符串
  int Val;       // 存储ND_NUM类型的值
};

// 语法解析入口函数
Node *parse(Token *Tok);

//
// 语义分析与代码生成
//

// 代码生成入口函数
void codegen(Node *Nd);
