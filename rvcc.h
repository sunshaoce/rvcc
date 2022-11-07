// 使用POSIX.1标准
// 使用了strndup函数
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// 宏展开函数
// 括号是为了保证内部表达式作为整体去求值
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

//
// 共用头文件，定义了多个文件间共同使用的函数和数据
//

typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;
typedef struct Relocation Relocation;
typedef struct Hideset Hideset;

//
// 字符串
//

// 字符串数组
typedef struct {
  char **Data;  // 数据内容
  int Capacity; // 能容纳字符串的容量
  int Len;      // 当前字符串的数量，Len ≤ Capacity
} StringArray;

void strArrayPush(StringArray *Arr, char *S);
char *format(char *Fmt, ...);

//
// 终结符分析，词法分析
//

// 为每个终结符都设置种类来表示
typedef enum {
  TK_IDENT,   // 标记符，可以为变量名、函数名等
  TK_PUNCT,   // 操作符如： + -
  TK_KEYWORD, // 关键字
  TK_STR,     // 字符串字面量
  TK_NUM,     // 数字
  TK_EOF,     // 文件终止符，即文件的最后
} TokenKind;

// 文件
typedef struct {
  char *Name;     // 文件名
  int FileNo;     // 文件编号，从1开始
  char *Contents; // 文件内容
} File;

// 终结符结构体
typedef struct Token Token;
struct Token {
  TokenKind Kind; // 种类
  Token *Next;    // 指向下一终结符
  int64_t Val;    // TK_NUM值
  double FVal;    // TK_NUM浮点值
  char *Loc;      // 在解析的字符串内的位置
  int Len;        // 长度
  Type *Ty;       // TK_NUM或TK_STR使用
  char *Str;      // 字符串字面量，包括'\0'

  File *File;       // 源文件位置
  int LineNo;       // 行号
  bool AtBOL;       // 终结符在行首（begin of line）时为true
  bool HasSpace;    // 终结符前是否有空格
  Hideset *Hideset; // 用于宏展开时的隐藏集
};

// 去除了static用以在多个文件间访问
// 报错函数
void error(char *Fmt, ...);
void errorAt(char *Loc, char *Fmt, ...);
void errorTok(Token *Tok, char *Fmt, ...);
// 警告函数
void warnTok(Token *Tok, char *Fmt, ...);
// 判断Token与Str的关系
bool equal(Token *Tok, char *Str);
Token *skip(Token *Tok, char *Str);
bool consume(Token **Rest, Token *Tok, char *Str);
// 转换关键字
void convertKeywords(Token *Tok);
// 获取输入文件
File **getInputFiles(void);
// 词法分析
Token *tokenizeFile(char *Path);

// 指rvcc源文件的某个文件的某一行出了问题，打印出文件名和行号
#define unreachable() error("internal error at %s:%d", __FILE__, __LINE__)

//
// 预处理器
//

Token *preprocess(Token *Tok);

//
// 生成AST（抽象语法树），语法解析
//

// 变量 或 函数
typedef struct Obj Obj;
struct Obj {
  Obj *Next;    // 指向下一对象
  char *Name;   // 变量名
  Type *Ty;     // 变量类型
  Token *Tok;   // 对应的终结符
  bool IsLocal; // 是 局部或全局 变量
  int Align;    // 对齐量
  // 局部变量
  int Offset; // fp的偏移量

  // 函数 或 全局变量
  bool IsFunction;
  bool IsDefinition; // 是否为函数定义
  bool IsStatic;     // 是否为文件域内的

  // 全局变量
  char *InitData;  // 用于初始化的数据
  Relocation *Rel; // 指向其他全局变量的指针

  // 函数
  Obj *Params;   // 形参
  Node *Body;    // 函数体
  Obj *Locals;   // 本地变量
  Obj *VaArea;   // 可变参数区域
  int StackSize; // 栈大小
};

// 全局变量可被 常量表达式 或者 指向其他全局变量的指针 初始化。
// 此结构体用于 指向其他全局变量的指针 的情况。
typedef struct Relocation Relocation;
struct Relocation {
  Relocation *Next; // 下一个
  int Offset;       // 偏移量
  char *Label;      // 标签名
  long Addend;      // 加数
};

// AST的节点种类
typedef enum {
  ND_NULL_EXPR, // 空表达式
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_NEG,       // 负号-
  ND_MOD,       // %，取余
  ND_BITAND,    // &，按位与
  ND_BITOR,     // |，按位或
  ND_BITXOR,    // ^，按位异或
  ND_SHL,       // <<，左移
  ND_SHR,       // >>，右移
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=
  ND_ASSIGN,    // 赋值
  ND_COND,      // ?:，条件运算符
  ND_COMMA,     // , 逗号
  ND_MEMBER,    // . 结构体成员访问
  ND_ADDR,      // 取地址 &
  ND_DEREF,     // 解引用 *
  ND_NOT,       // !，非
  ND_BITNOT,    // ~，按位取非
  ND_LOGAND,    // &&，与
  ND_LOGOR,     // ||，或
  ND_RETURN,    // 返回
  ND_IF,        // "if"，条件判断
  ND_FOR,       // "for" 或 "while"，循环
  ND_DO,        // "do"，用于do while语句
  ND_SWITCH,    // "switch"，分支语句
  ND_CASE,      // "case"
  ND_BLOCK,     // { ... }，代码块
  ND_GOTO,      // goto，直接跳转语句
  ND_LABEL,     // 标签语句
  ND_FUNCALL,   // 函数调用
  ND_EXPR_STMT, // 表达式语句
  ND_STMT_EXPR, // 语句表达式
  ND_VAR,       // 变量
  ND_NUM,       // 数字
  ND_CAST,      // 类型转换
  ND_MEMZERO,   // 栈中变量清零
} NodeKind;

// AST中二叉树节点
struct Node {
  NodeKind Kind; // 节点种类
  Node *Next;    // 下一节点，指代下一语句
  Token *Tok;    // 节点对应的终结符
  Type *Ty;      // 节点中数据的类型

  Node *LHS; // 左部，left-hand side
  Node *RHS; // 右部，right-hand side

  // "if"语句 或者 "for"语句
  Node *Cond; // 条件内的表达式
  Node *Then; // 符合条件后的语句
  Node *Els;  // 不符合条件后的语句
  Node *Init; // 初始化语句
  Node *Inc;  // 递增语句

  // "break" 标签
  char *BrkLabel;
  // "continue" 标签
  char *ContLabel;

  // 代码块 或 语句表达式
  Node *Body;

  // 结构体成员访问
  Member *Mem;

  // 函数调用
  Type *FuncType; // 函数类型
  Node *Args;     // 函数参数

  // goto和标签语句
  char *Label;
  char *UniqueLabel;
  Node *GotoNext;

  // switch和case
  Node *CaseNext;
  Node *DefaultCase;

  Obj *Var;    // 存储ND_VAR种类的变量
  int64_t Val; // 存储ND_NUM种类的值
  double FVal; // 存储ND_NUM种类的浮点值
};

// 类型转换，将表达式的值转换为另一种类型
Node *newCast(Node *Expr, Type *Ty);
// 解析常量表达式
int64_t constExpr(Token **Rest, Token *Tok);
// 语法解析入口函数
Obj *parse(Token *Tok);

//
// 类型系统
//

// 类型种类
typedef enum {
  TY_VOID,   // void类型
  TY_BOOL,   // _Bool布尔类型
  TY_CHAR,   // char字符类型
  TY_SHORT,  // short短整型
  TY_INT,    // int整型
  TY_LONG,   // long长整型
  TY_FLOAT,  // float类型
  TY_DOUBLE, // double类型
  TY_ENUM,   // enum枚举类型
  TY_PTR,    // 指针
  TY_FUNC,   // 函数
  TY_ARRAY,  // 数组
  TY_STRUCT, // 结构体
  TY_UNION,  // 联合体
} TypeKind;

struct Type {
  TypeKind Kind;   // 种类
  int Size;        // 大小, sizeof返回的值
  int Align;       // 对齐
  bool IsUnsigned; // 是否为无符号的

  // 指针
  Type *Base; // 指向的类型

  // 类型对应名称，如：变量名、函数名
  Token *Name;
  Token *NamePos; // 名称位置

  // 数组
  int ArrayLen; // 数组长度, 元素总个数

  // 结构体
  Member *Mems;
  bool IsFlexible; // 是否为灵活的

  // 函数类型
  Type *ReturnTy;  // 函数返回的类型
  Type *Params;    // 形参
  bool IsVariadic; // 是否为可变参数
  Type *Next;      // 下一类型
};

// 结构体成员
struct Member {
  Member *Next; // 下一成员
  Type *Ty;     // 类型
  Token *Tok;   // 用于报错信息
  Token *Name;  // 名称
  int Idx;      // 索引值
  int Align;    // 对齐量
  int Offset;   // 偏移量
};

// 声明全局变量，定义在type.c中。
extern Type *TyVoid;
extern Type *TyBool;

extern Type *TyChar;
extern Type *TyShort;
extern Type *TyInt;
extern Type *TyLong;

extern Type *TyUChar;
extern Type *TyUShort;
extern Type *TyUInt;
extern Type *TyULong;

extern Type *TyFloat;
extern Type *TyDouble;

// 判断是否为整型
bool isInteger(Type *TY);
// 判断是否为浮点类型
bool isFloNum(Type *Ty);
// 判断是否为数字
bool isNumeric(Type *Ty);
// 复制类型
Type *copyType(Type *Ty);
// 构建一个指针类型，并指向基类
Type *pointerTo(Type *Base);
// 为节点内的所有节点添加类型
void addType(Node *Nd);
// 数组类型
Type *arrayOf(Type *Base, int Size);
// 枚举类型
Type *enumType(void);
// 结构体类型
Type *structType(void);
// 函数类型
Type *funcType(Type *ReturnTy);

//
// 语义分析与代码生成
//

// 代码生成入口函数
void codegen(Obj *Prog, FILE *Out);
int alignTo(int N, int Align);

//
// 主程序，驱动文件
//

extern char *BaseFile;
