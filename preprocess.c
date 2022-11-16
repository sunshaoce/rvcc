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

// 宏处理函数
typedef Token *macroHandlerFn(Token *);

// 定义的宏变量
typedef struct Macro Macro;
struct Macro {
  Macro *Next;             // 下一个
  char *Name;              // 名称
  bool IsObjlike;          // 宏变量为真，或者宏函数为假
  MacroParam *Params;      // 宏函数参数
  bool IsVariadic;         // 是否为可变参数的
  Token *Body;             // 对应的终结符
  bool Deleted;            // 是否被删除了
  macroHandlerFn *Handler; // 宏处理函数
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
// 查找宏
static Macro *findMacro(Token *tok);

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

// 取两个隐藏集的交集
static Hideset *hidesetIntersection(Hideset *Hs1, Hideset *Hs2) {
  Hideset Head = {};
  Hideset *Cur = &Head;

  // 遍历Hs1，如果Hs2也有，那么就加入链表当中
  for (; Hs1; Hs1 = Hs1->Next)
    if (hidesetContains(Hs2, Hs1->Name, strlen(Hs1->Name)))
      Cur = Cur->Next = newHideset(Hs1->Name);
  return Head.Next;
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

// 将给定的字符串用双引号包住
static char *quoteString(char *Str) {
  // 两个引号，一个\0
  int BufSize = 3;
  // 如果有 \ 或 " 那么需要多留一个位置，存储转义用的 \ 符号
  for (int I = 0; Str[I]; I++) {
    if (Str[I] == '\\' || Str[I] == '"')
      BufSize++;
    BufSize++;
  }

  // 分配相应的空间
  char *Buf = calloc(1, BufSize);

  char *P = Buf;
  // 开头的"
  *P++ = '"';
  for (int I = 0; Str[I]; I++) {
    if (Str[I] == '\\' || Str[I] == '"')
      // 加上转义用的 \ 符号
      *P++ = '\\';
    *P++ = Str[I];
  }
  // 结尾的"\0
  *P++ = '"';
  *P++ = '\0';
  return Buf;
}

// 构建一个新的字符串的终结符
static Token *newStrToken(char *Str, Token *Tmpl) {
  // 将字符串加上双引号
  char *Buf = quoteString(Str);
  // 将字符串和相应的宏名称传入词法分析，去进行解析
  return tokenize(newFile(Tmpl->File->Name, Tmpl->File->FileNo, Buf));
}

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

// 构造数字终结符
static Token *newNumToken(int Val, Token *Tmpl) {
  char *Buf = format("%d\n", Val);
  return tokenize(newFile(Tmpl->File->Name, Tmpl->File->FileNo, Buf));
}

// 读取常量表达式
static Token *readConstExpr(Token **Rest, Token *Tok) {
  // 复制当前行
  Tok = copyLine(Rest, Tok);

  Token Head = {};
  Token *Cur = &Head;

  // 遍历终结符
  while (Tok->Kind != TK_EOF) {
    // "defined(foo)" 或 "defined foo"如果存在foo为1否则为0
    if (equal(Tok, "defined")) {
      Token *Start = Tok;
      // 消耗掉(
      bool HasParen = consume(&Tok, Tok->Next, "(");

      if (Tok->Kind != TK_IDENT)
        errorTok(Start, "macro name must be an identifier");
      // 查找宏
      Macro *M = findMacro(Tok);
      Tok = Tok->Next;

      // 对应着的)
      if (HasParen)
        Tok = skip(Tok, ")");

      // 构造一个相应的数字终结符
      Cur = Cur->Next = newNumToken(M ? 1 : 0, Start);
      continue;
    }

    // 将剩余的终结符存入链表
    Cur = Cur->Next = Tok;
    // 终结符前进
    Tok = Tok->Next;
  }

  // 将剩余的终结符存入链表
  Cur->Next = Tok;
  return Head.Next;
}

// 读取并计算常量表达式
static long evalConstExpr(Token **Rest, Token *Tok) {
  Token *Start = Tok;
  // 解析#if后的常量表达式
  Token *Expr = readConstExpr(Rest, Tok->Next);
  // 对于宏变量进行解析
  Expr = preprocess2(Expr);

  // 空表达式报错
  if (Expr->Kind == TK_EOF)
    errorTok(Start, "no expression");

  // 在计算常量表达式前，将遗留的标识符替换为0
  for (Token *T = Expr; T->Kind != TK_EOF; T = T->Next) {
    if (T->Kind == TK_IDENT) {
      Token *Next = T->Next;
      *T = *newNumToken(0, T);
      T->Next = Next;
    }
  }

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
static MacroParam *readMacroParams(Token **Rest, Token *Tok, bool *IsVariadic) {
  MacroParam Head = {};
  MacroParam *Cur = &Head;

  while (!equal(Tok, ")")) {
    if (Cur != &Head)
      Tok = skip(Tok, ",");

    // 处理可变参数
    if (equal(Tok, "...")) {
      *IsVariadic = true;
      // "..."应为最后一个参数
      *Rest = skip(Tok->Next, ")");
      return Head.Next;
    }

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
    bool IsVariadic = false;
    MacroParam *Params = readMacroParams(&Tok, Tok->Next, &IsVariadic);

    // 增加宏函数
    Macro *M = addMacro(Name, false, copyLine(Rest, Tok));
    M->Params = Params;
    // 传递是否为可变参数的值
    M->IsVariadic = IsVariadic;
  } else {
    // 增加宏变量
    addMacro(Name, true, copyLine(Rest, Tok));
  }
}

// 读取单个宏实参
static MacroArg *readMacroArgOne(Token **Rest, Token *Tok, bool ReadRest) {
  Token Head = {};
  Token *Cur = &Head;
  int Level = 0;

  // 读取实参对应的终结符
  while (true) {
    // 终止条件
    if (Level == 0 && equal(Tok, ")"))
      break;
    // 在这里ReadRest为真时，则可以读取多个终结符
    if (Level == 0 && !ReadRest && equal(Tok, ","))
      break;

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
static MacroArg *readMacroArgs(Token **Rest, Token *Tok, MacroParam *Params,
                               bool IsVariadic) {
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
    Cur = Cur->Next = readMacroArgOne(&Tok, Tok, false);
    // 设置为对应的形参名称
    Cur->Name = PP->Name;
  }

  // 剩余未匹配的实参，如果为可变参数
  if (IsVariadic) {
    MacroArg *Arg;
    // 剩余实参为空
    if (equal(Tok, ")")) {
      Arg = calloc(1, sizeof(MacroArg));
      Arg->Tok = newEOF(Tok);
    } else {
      // 处理对应可变参数的实参
      // 跳过","
      if (PP != Params)
        Tok = skip(Tok, ",");
      Arg = readMacroArgOne(&Tok, Tok, true);
    }
    Arg->Name = "__VA_ARGS__";
    Cur = Cur->Next = Arg;
  }
  // 如果形参没有遍历完，就报错
  else if (PP) {
    errorTok(Start, "too many arguments");
  }

  skip(Tok, ")");
  // 这里返回右括号
  *Rest = Tok;
  return Head.Next;
}

// 遍历查找实参
static MacroArg *findArg(MacroArg *Args, Token *Tok) {
  for (MacroArg *AP = Args; AP; AP = AP->Next)
    if (Tok->Len == strlen(AP->Name) && !strncmp(Tok->Loc, AP->Name, Tok->Len))
      return AP;
  return NULL;
}

// 将终结符链表中的所有终结符都连接起来，然后返回一个新的字符串
static char *joinTokens(Token *Tok, Token *End) {
  // 计算最终终结符的长度
  int Len = 1;
  for (Token *T = Tok; T != End && T->Kind != TK_EOF; T = T->Next) {
    // 非第一个，且前面有空格，计数加一
    if (T != Tok && T->HasSpace)
      Len++;
    // 加上终结符的长度
    Len += T->Len;
  }

  // 开辟相应的空间
  char *Buf = calloc(1, Len);

  // 复制终结符的文本
  int Pos = 0;
  for (Token *T = Tok; T != End && T->Kind != TK_EOF; T = T->Next) {
    // 非第一个，且前面有空格，设为空格
    if (T != Tok && T->HasSpace)
      Buf[Pos++] = ' ';
    // 拷贝相应的内容
    strncpy(Buf + Pos, T->Loc, T->Len);
    Pos += T->Len;
  }
  // 以'\0'结尾
  Buf[Pos] = '\0';
  return Buf;
}

// 将所有实参中的终结符连接起来，然后返回一个字符串的终结符
static Token *stringize(Token *Hash, Token *Arg) {
  // 创建一个字符串的终结符
  char *S = joinTokens(Arg, NULL);
  // 我们需要一个位置用来报错，所以使用了宏的名字
  return newStrToken(S, Hash);
}

// 拼接两个终结符构建一个新的终结符
static Token *paste(Token *LHS, Token *RHS) {
  // 合并两个终结符
  char *Buf = format("%.*s%.*s", LHS->Len, LHS->Loc, RHS->Len, RHS->Loc);

  // 词法解析生成的字符串，转换为相应的终结符
  Token *Tok = tokenize(newFile(LHS->File->Name, LHS->File->FileNo, Buf));
  if (Tok->Next->Kind != TK_EOF)
    errorTok(LHS, "pasting forms '%s', an invalid token", Buf);
  return Tok;
}

// 将宏函数形参替换为指定的实参
static Token *subst(Token *Tok, MacroArg *Args) {
  Token Head = {};
  Token *Cur = &Head;

  // 遍历将形参替换为实参的终结符链表
  while (Tok->Kind != TK_EOF) {
    // #宏实参 会被替换为相应的字符串
    if (equal(Tok, "#")) {
      // 查找实参
      MacroArg *Arg = findArg(Args, Tok->Next);
      if (!Arg)
        errorTok(Tok->Next, "'#' is not followed by a macro parameter");
      // 将实参的终结符字符化
      Cur = Cur->Next = stringize(Tok, Arg->Tok);
      Tok = Tok->Next->Next;
      continue;
    }

    // ##及右边，用于连接终结符
    if (equal(Tok, "##")) {
      if (Cur == &Head)
        errorTok(Tok, "'##' cannot appear at start of macro expansion");

      if (Tok->Next->Kind == TK_EOF)
        errorTok(Tok, "'##' cannot appear at end of macro expansion");

      // 查找下一个终结符
      // 如果是（##右边）宏实参
      MacroArg *Arg = findArg(Args, Tok->Next);
      if (Arg) {
        if (Arg->Tok->Kind != TK_EOF) {
          // 拼接当前终结符和（##右边）实参
          *Cur = *paste(Cur, Arg->Tok);
          // 将（##右边）实参未参与拼接的剩余部分加入到链表当中
          for (Token *T = Arg->Tok->Next; T->Kind != TK_EOF; T = T->Next)
            Cur = Cur->Next = copyToken(T);
        }
        // 指向（##右边）实参的下一个
        Tok = Tok->Next->Next;
        continue;
      }

      // 如果不是（##右边）宏实参
      // 直接拼接
      *Cur = *paste(Cur, Tok->Next);
      Tok = Tok->Next->Next;
      continue;
    }

    // 查找实参
    MacroArg *Arg = findArg(Args, Tok);

    // 左边及##，用于连接终结符
    if (Arg && equal(Tok->Next, "##")) {
      // 读取##右边的终结符
      Token *RHS = Tok->Next->Next;

      // 实参（##左边）为空的情况
      if (Arg->Tok->Kind == TK_EOF) {
        // 查找（##右边）实参
        MacroArg *Arg2 = findArg(Args, RHS);
        if (Arg2) {
          // 如果是实参，那么逐个遍历实参对应的终结符
          for (Token *T = Arg2->Tok; T->Kind != TK_EOF; T = T->Next)
            Cur = Cur->Next = copyToken(T);
        } else {
          // 如果不是实参，那么直接复制进链表
          Cur = Cur->Next = copyToken(RHS);
        }
        // 指向（##右边）实参的下一个
        Tok = RHS->Next;
        continue;
      }

      // 实参（##左边）不为空的情况
      for (Token *T = Arg->Tok; T->Kind != TK_EOF; T = T->Next)
        // 复制此终结符
        Cur = Cur->Next = copyToken(T);
      Tok = Tok->Next;
      continue;
    }

    // 处理宏终结符，宏实参在被替换之前已经被展开了
    if (Arg) {
      // 解析实参对应的终结符链表
      Token *T = preprocess2(Arg->Tok);
      // 传递 是否为行首 和 前面是否有空格 的信息
      T->AtBOL = Tok->AtBOL;
      T->HasSpace = Tok->HasSpace;
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

  // 如果宏设置了相应的处理函数，例如__LINE__
  if (M->Handler) {
    // 就使用相应的处理函数解析当前的宏
    *Rest = M->Handler(Tok);
    // 紧接着处理后续终结符
    (*Rest)->Next = Tok->Next;
    return true;
  }

  // 为宏变量时
  if (M->IsObjlike) {
    // 展开过一次的宏变量，就加入到隐藏集当中
    Hideset *Hs = hidesetUnion(Tok->Hideset, newHideset(M->Name));
    // 处理此宏变量之后，传递隐藏集给之后的终结符
    Token *Body = addHideset(M->Body, Hs);
    // 记录展开前的宏
    for (Token *T = Body; T->Kind != TK_EOF; T = T->Next)
      T->Origin = Tok;
    *Rest = append(Body, Tok->Next);
    // 传递 是否为行首 和 前面是否有空格 的信息
    (*Rest)->AtBOL = Tok->AtBOL;
    (*Rest)->HasSpace = Tok->HasSpace;
    return true;
  }

  // 如果宏函数后面没有参数列表，就处理为正常的标识符
  if (!equal(Tok->Next, "("))
    return false;

  // 处理宏函数，并连接到Tok之后
  // 读取宏函数实参，这里是宏函数的隐藏集
  Token *MacroToken = Tok;
  MacroArg *Args = readMacroArgs(&Tok, Tok, M->Params, M->IsVariadic);
  // 这里返回的是右括号，这里是宏参数的隐藏集
  Token *RParen = Tok;
  // 宏函数间可能具有不同的隐藏集，新的终结符就不知道应该使用哪个隐藏集。
  // 我们取宏终结符和右括号的交集，并将其用作新的隐藏集。
  Hideset *Hs = hidesetIntersection(MacroToken->Hideset, RParen->Hideset);

  // 将当前函数名加入隐藏集
  Hs = hidesetUnion(Hs, newHideset(M->Name));
  // 替换宏函数内的形参为实参
  Token *Body = subst(M->Body, Args);
  // 为宏函数内部设置隐藏集
  Body = addHideset(Body, Hs);
  // 记录展开前的宏函数
  for (Token *T = Body; T->Kind != TK_EOF; T = T->Next)
    T->Origin = MacroToken;
  // 将设置好的宏函数内部连接到终结符链表中
  *Rest = append(Body, Tok->Next);
  // 传递 是否为行首 和 前面是否有空格 的信息
  (*Rest)->AtBOL = MacroToken->AtBOL;
  (*Rest)->HasSpace = MacroToken->HasSpace;
  return true;
}

// 搜索引入路径区
static char *searchIncludePaths(char *Filename) {
  // 以"/"开头的视为绝对路径
  if (Filename[0] == '/')
    return Filename;

  // 从引入路径区查找文件
  for (int I = 0; I < IncludePaths.Len; I++) {
    char *Path = format("%s/%s", IncludePaths.Data[I], Filename);
    if (fileExists(Path))
      return Path;
  }
  return NULL;
}

// 读取#include参数
static char *readIncludeFilename(Token **Rest, Token *Tok, bool *IsDquote) {
  // 匹配样式1: #include "foo.h"
  if (Tok->Kind == TK_STR) {
    // #include 的双引号文件名是一种特殊的终结符
    // 不能转义其中的任何转义字符。
    // 例如，“C:\foo”中的“\f”不是换页符，而是\和f。
    // 所以此处不使用token->str。
    *IsDquote = true;
    *Rest = skipLine(Tok->Next);
    return strndup(Tok->Loc + 1, Tok->Len - 2);
  }

  // 匹配样式2: #include <foo.h>
  if (equal(Tok, "<")) {
    // 从"<"和">"间构建文件名.
    Token *Start = Tok;

    // 查找">"
    for (; !equal(Tok, ">"); Tok = Tok->Next)
      if (Tok->AtBOL || Tok->Kind == TK_EOF)
        errorTok(Tok, "expected '>'");

    // 没有引号
    *IsDquote = false;
    // 跳到行首
    *Rest = skipLine(Tok->Next);
    // "<"到">"前拼接为字符串
    return joinTokens(Start->Next, Tok);
  }

  // 匹配样式3: #include FOO
  if (Tok->Kind == TK_IDENT) {
    // FOO 必须宏展开为单个字符串标记或 "<" ... ">" 序列
    Token *Tok2 = preprocess2(copyLine(Rest, Tok));
    // 然后读取引入的文件名
    return readIncludeFilename(&Tok2, Tok2, IsDquote);
  }

  errorTok(Tok, "expected a filename");
  return NULL;
}

// 引入文件
static Token *includeFile(Token *Tok, char *Path, Token *FilenameTok) {
  // 词法分析文件
  Token *Tok2 = tokenizeFile(Path);
  if (!Tok2)
    errorTok(FilenameTok, "%s: cannot open file: %s", Path, strerror(errno));
  return append(Tok2, Tok);
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
      // 是否有双引号
      bool IsDquote;
      // 获取引入的文件名
      char *Filename = readIncludeFilename(&Tok, Tok->Next, &IsDquote);

      // 不以"/"开头的视为相对路径
      if (Filename[0] != '/' && IsDquote) {
        // 以当前文件所在目录为起点
        // 路径为：终结符文件名所在的文件夹路径/当前终结符名
        char *Path =
            format("%s/%s", dirname(strdup(Start->File->Name)), Filename);
        // 路径存在时引入文件
        if (fileExists(Path)) {
          Tok = includeFile(Tok, Path, Start->Next->Next);
          continue;
        }
      }

      // 直接引入文件，搜索引入路径
      char *Path = searchIncludePaths(Filename);
      Tok = includeFile(Tok, Path ? Path : Filename, Start->Next->Next);
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

    // 匹配#error
    if (equal(Tok, "error"))
      errorTok(Tok, "error");

    // 支持空指示
    if (Tok->AtBOL)
      continue;

    errorTok(Tok, "invalid preprocessor directive");
  }

  Cur->Next = Tok;
  return Head.Next;
}

// 定义预定义的宏
static void defineMacro(char *Name, char *Buf) {
  Token *Tok = tokenize(newFile("<built-in>", 1, Buf));
  addMacro(Name, true, Tok);
}

// 增加内建的宏和相应的宏处理函数
static Macro *addBuiltin(char *Name, macroHandlerFn *Fn) {
  // 增加宏
  Macro *M = addMacro(Name, true, NULL);
  // 设置相应的处理函数
  M->Handler = Fn;
  return M;
}

// 文件标号函数
static Token *fileMacro(Token *Tmpl) {
  // 如果存在原始的宏，则遍历后使用原始的宏
  while (Tmpl->Origin)
    Tmpl = Tmpl->Origin;
  // 根据原始宏的文件名构建字符串终结符
  return newStrToken(Tmpl->File->Name, Tmpl);
}

// 行标号函数
static Token *lineMacro(Token *Tmpl) {
  // 如果存在原始的宏，则遍历后使用原始的宏
  while (Tmpl->Origin)
    Tmpl = Tmpl->Origin;
  // 根据原始的宏的行号构建数值终结符
  return newNumToken(Tmpl->LineNo, Tmpl);
}

// 初始化预定义的宏
static void initMacros(void) {
  defineMacro("_LP64", "1");
  defineMacro("__C99_MACRO_WITH_VA_ARGS", "1");
  defineMacro("__ELF__", "1");
  defineMacro("__LP64__", "1");
  defineMacro("__SIZEOF_DOUBLE__", "8");
  defineMacro("__SIZEOF_FLOAT__", "4");
  defineMacro("__SIZEOF_INT__", "4");
  defineMacro("__SIZEOF_LONG_DOUBLE__", "8");
  defineMacro("__SIZEOF_LONG_LONG__", "8");
  defineMacro("__SIZEOF_LONG__", "8");
  defineMacro("__SIZEOF_POINTER__", "8");
  defineMacro("__SIZEOF_PTRDIFF_T__", "8");
  defineMacro("__SIZEOF_SHORT__", "2");
  defineMacro("__SIZEOF_SIZE_T__", "8");
  defineMacro("__SIZE_TYPE__", "unsigned long");
  defineMacro("__STDC_HOSTED__", "1");
  defineMacro("__STDC_NO_ATOMICS__", "1");
  defineMacro("__STDC_NO_COMPLEX__", "1");
  defineMacro("__STDC_NO_THREADS__", "1");
  defineMacro("__STDC_NO_VLA__", "1");
  defineMacro("__STDC_VERSION__", "201112L");
  defineMacro("__STDC__", "1");
  defineMacro("__USER_LABEL_PREFIX__", "");
  defineMacro("__alignof__", "_Alignof");
  defineMacro("__rvcc__", "1");
  defineMacro("__const__", "const");
  defineMacro("__gnu_linux__", "1");
  defineMacro("__inline__", "inline");
  defineMacro("__linux", "1");
  defineMacro("__linux__", "1");
  defineMacro("__signed__", "signed");
  defineMacro("__typeof__", "typeof");
  defineMacro("__unix", "1");
  defineMacro("__unix__", "1");
  defineMacro("__volatile__", "volatile");
  defineMacro("linux", "1");
  defineMacro("unix", "1");
  defineMacro("__riscv_mul", "1");
  defineMacro("__riscv_muldiv", "1");
  defineMacro("__riscv_fdiv", "1");
  defineMacro("__riscv_xlen", "64");
  defineMacro("__riscv", "1");
  defineMacro("__riscv64", "1");
  defineMacro("__riscv_div", "1");
  defineMacro("__riscv_float_abi_double", "1");
  defineMacro("__riscv_flen", "64");

  addBuiltin("__FILE__", fileMacro);
  addBuiltin("__LINE__", lineMacro);
}

// 预处理器入口函数
Token *preprocess(Token *Tok) {
  // 初始化预定义的宏
  initMacros();
  // 处理宏和指示
  Tok = preprocess2(Tok);
  // 此时#if应该都被清空了，否则报错
  if (CondIncls)
    errorTok(CondIncls->Tok, "unterminated conditional directive");
  // 将所有关键字的终结符，都标记为KEYWORD
  convertKeywords(Tok);
  return Tok;
}
