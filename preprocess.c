#include "rvcc.h"

// 宏函数形参
typedef struct MacroParam MacroParam;
struct MacroParam {
  MacroParam *Next; // 下一个
  char *Name;       // 名称
};

// 宏函数实参
typedef struct MacroArg MacroArg;
struct MacroArg {
  MacroArg *Next; // 下一个
  char *Name;     // 名称
  Token *Tok;     // 对应的终结符链表
};

// 定义的宏变量
typedef struct Macro Macro;
struct Macro {
  Macro *Next;        // 下一个
  char *Name;         // 名称
  bool IsObjlike;     // 宏变量为真，或者宏函数为假
  MacroParam *Params; // 宏函数参数
  Token *Body;        // 对应的终结符
  bool Deleted;       // 是否被删除了
};

// 宏变量栈
static Macro *Macros;

// #if可以嵌套，所以使用栈来保存嵌套的#if
typedef struct CondIncl CondIncl;
struct CondIncl {
  CondIncl *Next;                         // 下一个
  enum { IN_THEN, IN_ELIF, IN_ELSE } Ctx; // 类型
  Token *Tok;                             // 对应的终结符
  bool Included;                          // 是否被包含
};

// 预处理语言的设计方式使得即使存在递归宏也可以保证停止。
// 一个宏只对每个终结符应用一次。
//
// 宏展开时的隐藏集
typedef struct Hideset Hideset;
struct Hideset {
  Hideset *Next; // 下一个
  char *Name;    // 名称
};

// 全局的#if保存栈
static CondIncl *CondIncls;

// 处理所有的宏和指示
static Token *preprocess2(Token *Tok);

// 是否行首是#号
static bool isHash(Token *Tok) { return Tok->AtBOL && equal(Tok, "#"); }

// 一些预处理器允许#include等指示，在换行前有多余的终结符
// 此函数跳过这些终结符
static Token *skipLine(Token *Tok) {
  // 在行首，正常情况
  if (Tok->AtBOL)
    return Tok;
  // 提示多余的字符
  warnTok(Tok, "extra token");
  // 跳过终结符，直到行首
  while (!Tok->AtBOL)
    Tok = Tok->Next;
  return Tok;
}

static Token *copyToken(Token *Tok) {
  Token *T = calloc(1, sizeof(Token));
  *T = *Tok;
  T->Next = NULL;
  return T;
}

// 新建一个EOF终结符
static Token *newEOF(Token *Tok) {
  Token *T = copyToken(Tok);
  T->Kind = TK_EOF;
  T->Len = 0;
  return T;
}

// 新建一个隐藏集
static Hideset *newHideset(char *Name) {
  Hideset *Hs = calloc(1, sizeof(Hideset));
  Hs->Name = Name;
  return Hs;
}

// 连接两个隐藏集
static Hideset *hidesetUnion(Hideset *Hs1, Hideset *Hs2) {
  Hideset Head = {};
  Hideset *Cur = &Head;

  for (; Hs1; Hs1 = Hs1->Next)
    Cur = Cur->Next = newHideset(Hs1->Name);
  Cur->Next = Hs2;
  return Head.Next;
}

// 是否已经处于宏变量当中
static bool hidesetContains(Hideset *Hs, char *S, int Len) {
  // 遍历隐藏集
  for (; Hs; Hs = Hs->Next)
    if (strlen(Hs->Name) == Len && !strncmp(Hs->Name, S, Len))
      return true;
  return false;
}

// 遍历Tok之后的所有终结符，将隐藏集Hs都赋给每个终结符
static Token *addHideset(Token *Tok, Hideset *Hs) {
  Token Head = {};
  Token *Cur = &Head;

  for (; Tok; Tok = Tok->Next) {
    Token *T = copyToken(Tok);
    T->Hideset = hidesetUnion(T->Hideset, Hs);
    Cur = Cur->Next = T;
  }
  return Head.Next;
}

// 将Tok2放入Tok1的尾部
static Token *append(Token *Tok1, Token *Tok2) {
  // Tok1为空时，直接返回Tok2
  if (Tok1->Kind == TK_EOF)
    return Tok2;

  Token Head = {};
  Token *Cur = &Head;

  // 遍历Tok1，存入链表
  for (; Tok1->Kind != TK_EOF; Tok1 = Tok1->Next)
    Cur = Cur->Next = copyToken(Tok1);

  // 链表后接Tok2
  Cur->Next = Tok2;
  // 返回下一个
  return Head.Next;
}

// 跳过#if和#endif
static Token *skipCondIncl2(Token *Tok) {
  while (Tok->Kind != TK_EOF) {
    if (isHash(Tok) && (equal(Tok->Next, "if") || equal(Tok->Next, "ifdef") ||
                        equal(Tok->Next, "ifndef"))) {
      Tok = skipCondIncl2(Tok->Next->Next);
      continue;
    }
    if (isHash(Tok) && equal(Tok->Next, "endif"))
      return Tok->Next->Next;
    Tok = Tok->Next;
  }
  return Tok;
}

// #if为空时，一直跳过到#endif
// 其中嵌套的#if语句也一起跳过
static Token *skipCondIncl(Token *Tok) {
  while (Tok->Kind != TK_EOF) {
    // 跳过#if语句
    if (isHash(Tok) && (equal(Tok->Next, "if") || equal(Tok->Next, "ifdef") ||
                        equal(Tok->Next, "ifndef"))) {
      Tok = skipCondIncl2(Tok->Next->Next);
      continue;
    }

    // #elif，#else和#endif
    if (isHash(Tok) && (equal(Tok->Next, "elif") || equal(Tok->Next, "else") ||
                        equal(Tok->Next, "endif")))
      break;
    Tok = Tok->Next;
  }
  return Tok;
}

// 拷贝当前Tok到换行符间的所有终结符，并以EOF终结符结尾
// 此函数为#if分析参数
static Token *copyLine(Token **Rest, Token *Tok) {
  Token head = {};
  Token *Cur = &head;

  // 遍历复制终结符
  for (; !Tok->AtBOL; Tok = Tok->Next)
    Cur = Cur->Next = copyToken(Tok);

  // 以EOF终结符结尾
  Cur->Next = newEOF(Tok);
  *Rest = Tok;
  return head.Next;
}

// 读取并计算常量表达式
static long evalConstExpr(Token **Rest, Token *Tok) {
  Token *Start = Tok;
  // 解析#if后的常量表达式
  Token *Expr = copyLine(Rest, Tok->Next);
  // 对于宏变量进行解析
  Expr = preprocess2(Expr);

  // 空表达式报错
  if (Expr->Kind == TK_EOF)
    errorTok(Start, "no expression");

  // 计算常量表达式的值
  Token *Rest2;
  long Val = constExpr(&Rest2, Expr);
  // 计算后还有多余的终结符，则报错
  if (Rest2->Kind != TK_EOF)
    errorTok(Rest2, "extra token");
  // 返回计算的值
  return Val;
}

// 压入#if栈中
static CondIncl *pushCondIncl(Token *Tok, bool Included) {
  CondIncl *CI = calloc(1, sizeof(CondIncl));
  CI->Next = CondIncls;
  CI->Ctx = IN_THEN;
  CI->Tok = Tok;
  CI->Included = Included;
  CondIncls = CI;
  return CI;
}

// 查找相应的宏变量
static Macro *findMacro(Token *Tok) {
  // 如果不是标识符，直接报错
  if (Tok->Kind != TK_IDENT)
    return NULL;

  // 遍历宏变量栈，如果匹配则返回相应的宏变量
  for (Macro *M = Macros; M; M = M->Next)
    if (strlen(M->Name) == Tok->Len && !strncmp(M->Name, Tok->Loc, Tok->Len))
      // 被删除的宏变量返回空
      return M->Deleted ? NULL : M;
  return NULL;
}

// 新增宏变量，压入宏变量栈中
static Macro *addMacro(char *Name, bool IsObjlike, Token *Body) {
  Macro *M = calloc(1, sizeof(Macro));
  M->Next = Macros;
  M->Name = Name;
  M->IsObjlike = IsObjlike;
  M->Body = Body;
  Macros = M;
  return M;
}

// 读取宏形参
static MacroParam *readMacroParams(Token **Rest, Token *Tok) {
  MacroParam Head = {};
  MacroParam *Cur = &Head;

  while (!equal(Tok, ")")) {
    if (Cur != &Head)
      Tok = skip(Tok, ",");

    // 如果不是标识符报错
    if (Tok->Kind != TK_IDENT)
      errorTok(Tok, "expected an identifier");
    // 开辟空间
    MacroParam *M = calloc(1, sizeof(MacroParam));
    // 设置名称
    M->Name = strndup(Tok->Loc, Tok->Len);
    // 加入链表
    Cur = Cur->Next = M;
    Tok = Tok->Next;
  }
  *Rest = Tok->Next;
  return Head.Next;
}

// 读取宏定义
static void readMacroDefinition(Token **Rest, Token *Tok) {
  // 如果匹配到的不是标识符就报错
  if (Tok->Kind != TK_IDENT)
    errorTok(Tok, "macro name must be an identifier");
  // 复制名字
  char *Name = strndup(Tok->Loc, Tok->Len);
  Tok = Tok->Next;

  // 判断是宏变量还是宏函数，括号前没有空格则为宏函数
  if (!Tok->HasSpace && equal(Tok, "(")) {
    // 构造形参
    MacroParam *Params = readMacroParams(&Tok, Tok->Next);
    // 增加宏函数
    Macro *M = addMacro(Name, false, copyLine(Rest, Tok));
    M->Params = Params;
  } else {
    // 增加宏变量
    addMacro(Name, true, copyLine(Rest, Tok));
  }
}

// 读取单个宏实参
static MacroArg *readMacroArgOne(Token **Rest, Token *Tok) {
  Token Head = {};
  Token *Cur = &Head;
  int Level = 0;

  // 读取实参对应的终结符
  while (Level > 0 || !equal(Tok, ",") && !equal(Tok, ")")) {
    if (Tok->Kind == TK_EOF)
      errorTok(Tok, "premature end of input");

    if (equal(Tok, "("))
      Level++;
    else if (equal(Tok, ")"))
      Level--;

    // 将标识符加入到链表中
    Cur = Cur->Next = copyToken(Tok);
    Tok = Tok->Next;
  }

  // 加入EOF终结
  Cur->Next = newEOF(Tok);

  MacroArg *Arg = calloc(1, sizeof(MacroArg));
  // 赋值实参的终结符链表
  Arg->Tok = Head.Next;
  *Rest = Tok;
  return Arg;
}

// 读取宏实参
static MacroArg *readMacroArgs(Token **Rest, Token *Tok, MacroParam *Params) {
  Token *Start = Tok;
  Tok = Tok->Next->Next;

  MacroArg Head = {};
  MacroArg *Cur = &Head;

  // 遍历形参，然后对应着加入到实参链表中
  MacroParam *PP = Params;
  for (; PP; PP = PP->Next) {
    if (Cur != &Head)
      Tok = skip(Tok, ",");
    // 读取单个实参
    Cur = Cur->Next = readMacroArgOne(&Tok, Tok);
    // 设置为对应的形参名称
    Cur->Name = PP->Name;
  }

  // 如果形参没有遍历完，就报错
  if (PP)
    errorTok(Start, "too many arguments");
  *Rest = skip(Tok, ")");
  return Head.Next;
}

// 遍历查找实参
static MacroArg *findArg(MacroArg *Args, Token *Tok) {
  for (MacroArg *AP = Args; AP; AP = AP->Next)
    if (Tok->Len == strlen(AP->Name) && !strncmp(Tok->Loc, AP->Name, Tok->Len))
      return AP;
  return NULL;
}

// 将宏函数形参替换为指定的实参
static Token *subst(Token *Tok, MacroArg *Args) {
  Token Head = {};
  Token *Cur = &Head;

  // 遍历将形参替换为实参的终结符链表
  while (Tok->Kind != TK_EOF) {
    // 查找实参
    MacroArg *Arg = findArg(Args, Tok);

    // 处理宏终结符，宏实参在被替换之前已经被展开了
    if (Arg) {
      // 解析实参对应的终结符链表
      Token *T = preprocess2(Arg->Tok);
      for (; T->Kind != TK_EOF; T = T->Next)
        Cur = Cur->Next = copyToken(T);
      Tok = Tok->Next;
      continue;
    }

    // 处理非宏的终结符
    Cur = Cur->Next = copyToken(Tok);
    Tok = Tok->Next;
    continue;
  }

  Cur->Next = Tok;
  // 将宏链表返回
  return Head.Next;
}

// 如果是宏变量并展开成功，返回真
static bool expandMacro(Token **Rest, Token *Tok) {
  // 判断是否处于隐藏集之中
  if (hidesetContains(Tok->Hideset, Tok->Loc, Tok->Len))
    return false;

  // 判断是否为宏变量
  Macro *M = findMacro(Tok);
  if (!M)
    return false;

  // 为宏变量时
  if (M->IsObjlike) {
    // 展开过一次的宏变量，就加入到隐藏集当中
    Hideset *Hs = hidesetUnion(Tok->Hideset, newHideset(M->Name));
    // 处理此宏变量之后，传递隐藏集给之后的终结符
    Token *Body = addHideset(M->Body, Hs);
    *Rest = append(Body, Tok->Next);
    return true;
  }

  // 如果宏函数后面没有参数列表，就处理为正常的标识符
  if (!equal(Tok->Next, "("))
    return false;

  // 处理宏函数，并连接到Tok之后
  // 读取宏函数实参
  MacroArg *Args = readMacroArgs(&Tok, Tok, M->Params);
  *Rest = append(subst(M->Body, Args), Tok);
  return true;
}

// 遍历终结符，处理宏和指示
static Token *preprocess2(Token *Tok) {
  Token Head = {};
  Token *Cur = &Head;

  // 遍历终结符
  while (Tok->Kind != TK_EOF) {
    // 如果是个宏变量，那么就展开
    if (expandMacro(&Tok, Tok))
      continue;

    // 如果不是#号开头则前进
    if (!isHash(Tok)) {
      Cur->Next = Tok;
      Cur = Cur->Next;
      Tok = Tok->Next;
      continue;
    }

    // 记录开始的终结符
    Token *Start = Tok;
    // 下一终结符
    Tok = Tok->Next;

    // 匹配#include
    if (equal(Tok, "include")) {
      // 跳过"
      Tok = Tok->Next;

      // 需要后面跟文件名
      if (Tok->Kind != TK_STR)
        errorTok(Tok, "expected a filename");

      // 文件路径
      char *Path;
      if (Tok->Str[0] == '/')
        // "/"开头的视为绝对路径
        Path = Tok->Str;
      else
        // 以当前文件所在目录为起点
        // 路径为：终结符文件名所在的文件夹路径/当前终结符名
        Path = format("%s/%s", dirname(strdup(Tok->File->Name)), Tok->Str);

      // 词法解析文件
      Token *Tok2 = tokenizeFile(Path);
      if (!Tok2)
        errorTok(Tok, "%s", strerror(errno));
      // 处理多余的终结符
      Tok = skipLine(Tok->Next);
      // 将Tok2接续到Tok->Next的位置
      Tok = append(Tok2, Tok);
      continue;
    }

    // 匹配#define
    if (equal(Tok, "define")) {
      // 读取宏定义
      readMacroDefinition(&Tok, Tok->Next);
      continue;
    }

    // 匹配#undef
    if (equal(Tok, "undef")) {
      Tok = Tok->Next;
      // 如果匹配到的不是标识符就报错
      if (Tok->Kind != TK_IDENT)
        errorTok(Tok, "macro name must be an identifier");
      // 复制名字
      char *Name = strndup(Tok->Loc, Tok->Len);
      // 跳到行首
      Tok = skipLine(Tok->Next);

      // 增加宏变量
      Macro *M = addMacro(Name, true, NULL);
      // 将宏变量设为删除状态
      M->Deleted = true;
      continue;
    }

    // 匹配#if
    if (equal(Tok, "if")) {
      // 计算常量表达式
      long Val = evalConstExpr(&Tok, Tok);
      // 将Tok压入#if栈中
      pushCondIncl(Start, Val);
      // 处理#if后值为假的情况，全部跳过
      if (!Val)
        Tok = skipCondIncl(Tok);
      continue;
    }

    // 匹配#ifdef
    if (equal(Tok, "ifdef")) {
      // 查找宏变量
      bool Defined = findMacro(Tok->Next);
      // 压入#if栈
      pushCondIncl(Tok, Defined);
      // 跳到行首
      Tok = skipLine(Tok->Next->Next);
      // 如果没被定义，那么应该跳过这个部分
      if (!Defined)
        Tok = skipCondIncl(Tok);
      continue;
    }

    // 匹配#ifndef
    if (equal(Tok, "ifndef")) {
      // 查找宏变量
      bool Defined = findMacro(Tok->Next);
      // 压入#if栈，此时不存在时则设为真
      pushCondIncl(Tok, !Defined);
      // 跳到行首
      Tok = skipLine(Tok->Next->Next);
      // 如果被定义了，那么应该跳过这个部分
      if (Defined)
        Tok = skipCondIncl(Tok);
      continue;
    }

    // 匹配#elif
    if (equal(Tok, "elif")) {
      if (!CondIncls || CondIncls->Ctx == IN_ELSE)
        errorTok(Start, "stray #elif");
      CondIncls->Ctx = IN_ELIF;

      if (!CondIncls->Included && evalConstExpr(&Tok, Tok))
        // 处理之前的值都为假且当前#elif为真的情况
        CondIncls->Included = true;
      else
        // 否则其他的情况，全部跳过
        Tok = skipCondIncl(Tok);
      continue;
    }

    // 匹配#else
    if (equal(Tok, "else")) {
      if (!CondIncls || CondIncls->Ctx == IN_ELSE)
        errorTok(Start, "stray #else");
      CondIncls->Ctx = IN_ELSE;
      // 走到行首
      Tok = skipLine(Tok->Next);

      // 处理之前有值为真的情况，则#else全部跳过
      if (CondIncls->Included)
        Tok = skipCondIncl(Tok);
      continue;
    }

    // 匹配#endif
    if (equal(Tok, "endif")) {
      // 弹栈，失败报错
      if (!CondIncls)
        errorTok(Start, "stray #endif");
      CondIncls = CondIncls->Next;
      // 走到行首
      Tok = skipLine(Tok->Next);
      continue;
    }

    // 支持空指示
    if (Tok->AtBOL)
      continue;

    errorTok(Tok, "invalid preprocessor directive");
  }

  Cur->Next = Tok;
  return Head.Next;
}

// 预处理器入口函数
Token *preprocess(Token *Tok) {
  // 处理宏和指示
  Tok = preprocess2(Tok);
  // 此时#if应该都被清空了，否则报错
  if (CondIncls)
    errorTok(CondIncls->Tok, "unterminated conditional directive");
  // 将所有关键字的终结符，都标记为KEYWORD
  convertKeywords(Tok);
  return Tok;
}
