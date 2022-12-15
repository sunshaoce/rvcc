#include "rvcc.h"

// 输入文件
static File *CurrentFile;

// 输入文件列表
static File **InputFiles;

// 位于行首时为真
static bool AtBOL;

// 终结符前是有空格时为真
static bool HasSpace;

// 输出错误信息
// static文件内可以访问的函数
// Fmt为传入的字符串， ... 为可变参数，表示Fmt后面所有的参数
void error(char *Fmt, ...) {
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

// 输出例如下面的错误，并退出
// foo.c:10: x = y + 1;
//               ^ <错误信息>
static void verrorAt(char *Filename, char *Input, int LineNo, char *Loc,
                     char *Fmt, va_list VA) {
  // 查找包含loc的行
  char *Line = Loc;
  // Line递减到当前行的最开始的位置
  // Line<CurrentInput, 判断是否读取到文件最开始的位置
  // Line[-1] != '\n'，Line字符串前一个字符是否为换行符（上一行末尾）
  while (Input < Line && Line[-1] != '\n')
    Line--;

  // End递增到行尾的换行符
  char *End = Loc;
  while (*End != '\n')
    End++;

  // 输出 文件名:错误行
  // Indent记录输出了多少个字符
  int Indent = fprintf(stderr, "%s:%d: ", Filename, LineNo);
  // 输出Line的行内所有字符（不含换行符）
  fprintf(stderr, "%.*s\n", (int)(End - Line), Line);

  // 计算错误信息位置，在当前行内的偏移量+前面输出了多少个字符
  int Pos = Loc - Line + Indent;

  // 将字符串补齐为Pos位，因为是空字符串，所以填充Pos个空格。
  fprintf(stderr, "%*s", Pos, "");
  fprintf(stderr, "^ ");
  vfprintf(stderr, Fmt, VA);
  fprintf(stderr, "\n");
  va_end(VA);
}

// 字符解析出错
void errorAt(char *Loc, char *Fmt, ...) {
  int LineNo = 1;
  for (char *P = CurrentFile->Contents; P < Loc; P++)
    if (*P == '\n')
      LineNo++;

  va_list VA;
  va_start(VA, Fmt);
  verrorAt(CurrentFile->Name, CurrentFile->Contents, LineNo, Loc, Fmt, VA);
  exit(1);
}

// Tok解析出错
void errorTok(Token *Tok, char *Fmt, ...) {
  va_list VA;
  va_start(VA, Fmt);
  verrorAt(Tok->File->Name, Tok->File->Contents, Tok->LineNo, Tok->Loc, Fmt,
           VA);
  exit(1);
}

// Tok解析警告
void warnTok(Token *Tok, char *Fmt, ...) {
  va_list VA;
  va_start(VA, Fmt);
  verrorAt(Tok->File->Name, Tok->File->Contents, Tok->LineNo, Tok->Loc, Fmt,
           VA);
  va_end(VA);
}

// 判断Tok的值是否等于指定值，没有用char，是为了后续拓展
bool equal(Token *Tok, char *Str) {
  // 比较字符串LHS（左部），RHS（右部）的前N位，S2的长度应大于等于N.
  // 比较按照字典序，LHS<RHS回负值，LHS=RHS返回0，LHS>RHS返回正值
  // 同时确保，此处的Op位数=N
  return memcmp(Tok->Loc, Str, Tok->Len) == 0 && Str[Tok->Len] == '\0';
}

// 跳过指定的Str
Token *skip(Token *Tok, char *Str) {
  if (!equal(Tok, Str))
    errorTok(Tok, "expect '%s'", Str);
  return Tok->Next;
}

// 消耗掉指定Token
bool consume(Token **Rest, Token *Tok, char *Str) {
  // 存在
  if (equal(Tok, Str)) {
    *Rest = Tok->Next;
    return true;
  }
  // 不存在
  *Rest = Tok;
  return false;
}

// 生成新的Token
static Token *newToken(TokenKind Kind, char *Start, char *End) {
  // 分配1个Token的内存空间
  Token *Tok = calloc(1, sizeof(Token));
  Tok->Kind = Kind;
  Tok->Loc = Start;
  Tok->Len = End - Start;
  // 输入文件
  Tok->File = CurrentFile;
  // 读取是否为行首，然后设置为false
  Tok->AtBOL = AtBOL;
  AtBOL = false;
  // 读取是否为前面是空格，然后设置为false
  Tok->HasSpace = HasSpace;
  HasSpace = false;
  return Tok;
}

// 判断Str是否以SubStr开头
static bool startsWith(char *Str, char *SubStr) {
  // 比较LHS和RHS的N个字符是否相等
  return strncmp(Str, SubStr, strlen(SubStr)) == 0;
}

// 判断标记符的首字母规则
// [a-zA-Z_]
bool isIdent1(char C) {
  // a-z与A-Z在ASCII中不相连，所以需要分别判断
  return ('a' <= C && C <= 'z') || ('A' <= C && C <= 'Z') || C == '_';
}

// 判断标记符的非首字母的规则
// [a-zA-Z0-9_]
bool isIdent2(char C) { return isIdent1(C) || ('0' <= C && C <= '9'); }

// 读取标识符，并返回其长度
static int readIdent(char *Start) {
  char *P = Start;
  uint32_t C = decodeUTF8(&P, P);
  // 如果不是标识符，返回0
  if (!isIdent1_1(C))
    return 0;

  // 遍历标识符所有字符
  while (true) {
    char *Q;
    C = decodeUTF8(&Q, P);
    if (!isIdent2_1(C))
      // 返回标识符长度
      return P - Start;
    P = Q;
  }
}

// 返回一位十六进制转十进制
// hexDigit = [0-9a-fA-F]
// 16: 0 1 2 3 4 5 6 7 8 9  A  B  C  D  E  F
// 10: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
static int fromHex(char C) {
  if ('0' <= C && C <= '9')
    return C - '0';
  if ('a' <= C && C <= 'f')
    return C - 'a' + 10;
  return C - 'A' + 10;
}

// 读取操作符
static int readPunct(char *Ptr) {
  // 判断多字节的操作符
  static char *Kw[] = {"<<=", ">>=", "...", "==", "!=", "<=", ">=", "->",
                       "+=",  "-=",  "*=",  "/=", "++", "--", "%=", "&=",
                       "|=",  "^=",  "&&",  "||", "<<", ">>", "##",
  };

  // 遍历列表匹配Ptr字符串
  for (int I = 0; I < sizeof(Kw) / sizeof(*Kw); ++I) {
    if (startsWith(Ptr, Kw[I]))
      return strlen(Kw[I]);
  }

  // 判断1字节的操作符
  return ispunct(*Ptr) ? 1 : 0;
}

// 判断是否为关键字
static bool isKeyword(Token *Tok) {
  // 关键字列表
  static char *Kw[] = {
      "return",       "if",        "else",     "for",      "while",
      "int",          "sizeof",    "char",     "struct",   "union",
      "long",         "short",     "void",     "typedef",  "_Bool",
      "enum",         "static",    "goto",     "break",    "continue",
      "switch",       "case",      "default",  "extern",   "_Alignof",
      "_Alignas",     "do",        "signed",   "unsigned", "const",
      "volatile",     "auto",      "register", "restrict", "__restrict",
      "__restrict__", "_Noreturn", "float",    "double",
  };

  // 遍历关键字列表匹配
  for (int I = 0; I < sizeof(Kw) / sizeof(*Kw); ++I) {
    if (equal(Tok, Kw[I]))
      return true;
  }

  return false;
}

// 读取转义字符
static int readEscapedChar(char **NewPos, char *P) {
  if ('0' <= *P && *P <= '7') {
    // 读取一个八进制数字，不能长于三位
    // \abc = (a*8+b)*8+c
    int C = *P++ - '0';
    if ('0' <= *P && *P <= '7') {
      C = (C << 3) + (*P++ - '0');
      if ('0' <= *P && *P <= '7')
        C = (C << 3) + (*P++ - '0');
    }
    *NewPos = P;
    return C;
  }

  if (*P == 'x') {
    P++;
    // 判断是否为十六进制数字
    if (!isxdigit(*P))
      errorAt(P, "invalid hex escape sequence");

    int C = 0;
    // 读取一位或多位十六进制数字
    // \xWXYZ = ((W*16+X)*16+Y)*16+Z
    for (; isxdigit(*P); P++)
      C = (C << 4) + fromHex(*P);
    *NewPos = P;
    return C;
  }

  *NewPos = P + 1;

  switch (*P) {
  case 'a': // 响铃（警报）
    return '\a';
  case 'b': // 退格
    return '\b';
  case 't': // 水平制表符，tab
    return '\t';
  case 'n': // 换行
    return '\n';
  case 'v': // 垂直制表符
    return '\v';
  case 'f': // 换页
    return '\f';
  case 'r': // 回车
    return '\r';
  // 属于GNU C拓展
  case 'e': // 转义符
    return 27;
  default: // 默认将原字符返回
    return *P;
  }
}

// 读取到字符串字面量结尾
static char *stringLiteralEnd(char *P) {
  char *Start = P;
  for (; *P != '"'; P++) {
    if (*P == '\n' || *P == '\0')
      errorAt(Start, "unclosed string literal");
    if (*P == '\\')
      P++;
  }
  return P;
}

// 读取字符串字面量：字符串开始的位置，左引号的位置
static Token *readStringLiteral(char *Start, char *Quote) {
  // 读取到字符串字面量的右引号
  char *End = stringLiteralEnd(Quote + 1);
  // 定义一个与字符串字面量内字符数+1的Buf
  // 用来存储最大位数的字符串字面量
  char *Buf = calloc(1, End - Quote);
  // 实际的字符位数，一个转义字符为1位
  int Len = 0;

  // 将读取后的结果写入Buf
  for (char *P = Quote + 1; P < End;) {
    if (*P == '\\') {
      Buf[Len++] = readEscapedChar(&P, P + 1);
    } else {
      Buf[Len++] = *P++;
    }
  }

  // Token这里需要包含带双引号的字符串字面量
  Token *Tok = newToken(TK_STR, Start, End + 1);
  // 为\0增加一位
  Tok->Ty = arrayOf(TyChar, Len + 1);
  Tok->Str = Buf;
  return Tok;
}

// 解码UTF-8的字符串并将其转码为UTF-16
//
// UTF-16 使用2字节或4字节对字符进行编码。
// 小于U+10000的码点，使用2字节。
// 大于U+10000的码点，使用4字节（每2个字节被称为代理项，即前导代理和后尾代理）。
static Token *readUTF16StringLiteral(char *Start, char *Quote) {
  char *End = stringLiteralEnd(Quote + 1);
  uint16_t *Buf = calloc(2, End - Start);
  int Len = 0;

  // 遍历引号内的字符
  for (char *P = Quote + 1; P < End;) {
    // 处理转义字符
    if (*P == '\\') {
      Buf[Len++] = readEscapedChar(&P, P + 1);
      continue;
    }

    // 解码UTF-8的字符串文字
    uint32_t C = decodeUTF8(&P, P);
    if (C < 0x10000) {
      // 用2字节存储码点
      Buf[Len++] = C;
    } else {
      // 用2字节存储码点
      C -= 0x10000;
      // 前导代理
      Buf[Len++] = 0xd800 + ((C >> 10) & 0x3ff);
      // 后尾代理
      Buf[Len++] = 0xdc00 + (C & 0x3ff);
    }
  }

  // 构建UTF-16编码的字符串终结符
  Token *Tok = newToken(TK_STR, Start, End + 1);
  Tok->Ty = arrayOf(TyUShort, Len + 1);
  Tok->Str = (char *)Buf;
  return Tok;
}

// 解码UTF-8的字符串并将其转码为UTF-32
//
// UTF-32是4字节编码
static Token *readUTF32StringLiteral(char *Start, char *Quote, Type *Ty) {
  char *End = stringLiteralEnd(Quote + 1);
  uint32_t *Buf = calloc(4, End - Quote);
  int Len = 0;

  // 解码UTF-8的字符串文字
  for (char *P = Quote + 1; P < End;) {
    if (*P == '\\')
      // 处理转义字符
      Buf[Len++] = readEscapedChar(&P, P + 1);
    else
      // 解码UTF-8
      Buf[Len++] = decodeUTF8(&P, P);
  }

  // 构建UTF-32编码的字符串终结符
  Token *Tok = newToken(TK_STR, Start, End + 1);
  Tok->Ty = arrayOf(Ty, Len + 1);
  Tok->Str = (char *)Buf;
  return Tok;
}

// 读取字符字面量
static Token *readCharLiteral(char *Start, char *Quote, Type *Ty) {
  char *P = Quote + 1;
  // 解析字符为 \0 的情况
  if (*P == '\0')
    errorAt(Start, "unclosed char literal");

  // 解析字符
  int C;
  // 转义
  if (*P == '\\')
    C = readEscapedChar(&P, P + 1);
  else
    // 对于其他字符进行解码
    C = decodeUTF8(&P, P);

  // strchr返回以 ' 开头的字符串，若无则为NULL
  char *End = strchr(P, '\'');
  if (!End)
    errorAt(P, "unclosed char literal");

  // 构造一个NUM的终结符，值为C的数值
  Token *Tok = newToken(TK_NUM, Start, End + 1);
  Tok->Val = C;
  Tok->Ty = Ty;
  return Tok;
}

// 转换预处理数值
static bool convertPPInt(Token *Tok) {
  // 获取终结符对应的字符串
  char *P = Tok->Loc;

  // 读取二、八、十、十六进制
  // 默认为十进制
  int Base = 10;
  // 比较两个字符串前2个字符，忽略大小写，并判断是否为数字
  if (!strncasecmp(P, "0x", 2) && isxdigit(P[2])) {
    // 十六进制
    P += 2;
    Base = 16;
  } else if (!strncasecmp(P, "0b", 2) && (P[2] == '0' || P[2] == '1')) {
    // 二进制
    P += 2;
    Base = 2;
  } else if (*P == '0') {
    // 八进制
    Base = 8;
  }

  // 将字符串转换为Base进制的数字
  int64_t Val = strtoul(P, &P, Base);

  // 读取U L LL后缀
  bool L = false;
  bool U = false;

  // LLU
  if (startsWith(P, "LLU") || startsWith(P, "LLu") || startsWith(P, "llU") ||
      startsWith(P, "llu") || startsWith(P, "ULL") || startsWith(P, "Ull") ||
      startsWith(P, "uLL") || startsWith(P, "ull")) {
    P += 3;
    L = U = true;
  } else if (!strncasecmp(P, "lu", 2) || !strncasecmp(P, "ul", 2)) {
    // LU
    P += 2;
    L = U = true;
  } else if (startsWith(P, "LL") || startsWith(P, "ll")) {
    // LL
    P += 2;
    L = true;
  } else if (*P == 'L' || *P == 'l') {
    // L
    P++;
    L = true;
  } else if (*P == 'U' || *P == 'u') {
    // U
    P++;
    U = true;
  }

  // 不能作为整型进行解析
  if (P != Tok->Loc + Tok->Len)
    return false;

  // 推断出类型，采用能存下当前数值的类型
  Type *Ty;
  if (Base == 10) {
    if (L && U)
      Ty = TyULong;
    else if (L)
      Ty = TyLong;
    else if (U)
      Ty = (Val >> 32) ? TyULong : TyUInt;
    else
      Ty = (Val >> 31) ? TyLong : TyInt;
  } else {
    if (L && U)
      Ty = TyULong;
    else if (L)
      Ty = (Val >> 63) ? TyULong : TyLong;
    else if (U)
      Ty = (Val >> 32) ? TyULong : TyUInt;
    else if (Val >> 63)
      Ty = TyULong;
    else if (Val >> 32)
      Ty = TyLong;
    else if (Val >> 31)
      Ty = TyUInt;
    else
      Ty = TyInt;
  }

  // 构造NUM的终结符
  Tok->Kind = TK_NUM;
  Tok->Val = Val;
  Tok->Ty = Ty;
  return true;
}

// 预处理阶段的数字字面量比后面阶段的定义更宽泛
// 因而先将数字字面量标记为pp-number，随后再转为常规数值终结符
//
// 转换预处理数值终结符为常规数值终结符
static void convertPPNumber(Token *Tok) {
  // 尝试作为整型常量解析
  if (convertPPInt(Tok))
    return;

  // 如果不是整型，那么一定是浮点数
  char *End;
  double Val = strtod(Tok->Loc, &End);

  // 处理浮点数后缀
  Type *Ty;
  if (*End == 'f' || *End == 'F') {
    Ty = TyFloat;
    End++;
  } else if (*End == 'l' || *End == 'L') {
    Ty = TyDouble;
    End++;
  } else {
    Ty = TyDouble;
  }

  // 构建浮点数终结符
  if (Tok->Loc + Tok->Len != End)
    errorTok(Tok, "invalid numeric constant");

  Tok->Kind = TK_NUM;
  Tok->FVal = Val;
  Tok->Ty = Ty;
}

// 转换预处理终结符
void convertPPTokens(Token *Tok) {
  for (Token *T = Tok; T->Kind != TK_EOF; T = T->Next) {
    if (isKeyword(T))
      // 将关键字的终结符设为为TK_KEYWORD
      T->Kind = TK_KEYWORD;
    else if (T->Kind == TK_PP_NUM)
      // 转换预处理数值
      convertPPNumber(T);
  }
}

// 为所有Token添加行号
static void addLineNumbers(Token *Tok) {
  // 读取当前文件的内容
  char *P = CurrentFile->Contents;
  int N = 1;

  do {
    if (P == Tok->Loc) {
      Tok->LineNo = N;
      Tok = Tok->Next;
    }
    if (*P == '\n')
      N++;
  } while (*P++);
}

// 终结符解析，文件名，文件内容
Token *tokenize(File *FP) {
  // 设定当前文件
  CurrentFile = FP;

  // 读取相应的内容
  char *P = FP->Contents;

  Token Head = {};
  Token *Cur = &Head;

  // 文件开始设置为行首
  AtBOL = true;
  // 文件开始设置为前面没有空格
  HasSpace = false;

  while (*P) {
    // 跳过行注释
    if (startsWith(P, "//")) {
      P += 2;
      while (*P != '\n')
        P++;
      HasSpace = true;
      continue;
    }

    // 跳过块注释
    if (startsWith(P, "/*")) {
      // 查找第一个"*/"的位置
      char *Q = strstr(P + 2, "*/");
      if (!Q)
        errorAt(P, "unclosed block comment");
      P = Q + 2;
      HasSpace = true;
      continue;
    }

    // 匹配换行符，设置为行首
    if (*P == '\n') {
      P++;
      AtBOL = true;
      HasSpace = true;
      continue;
    }

    // 跳过所有空白符如：空格、回车
    if (isspace(*P)) {
      ++P;
      HasSpace = true;
      continue;
    }

    // 解析整型和浮点数
    if (isdigit(*P) || (*P == '.' && isdigit(P[1]))) {
      char *Q = P++;
      while (true) {
        if (P[0] && P[1] && strchr("eEpP", P[0]) && strchr("+-", P[1]))
          P += 2;
        else if (isalnum(*P) || *P == '.')
          P++;
        else
          break;
      }
      Cur = Cur->Next = newToken(TK_PP_NUM, Q, P);
      continue;
    }

    // 解析字符串字面量
    if (*P == '"') {
      Cur->Next = readStringLiteral(P, P);
      Cur = Cur->Next;
      P += Cur->Len;
      continue;
    }

    // UTF-8字符串字面量
    if (startsWith(P, "u8\"")) {
      Cur = Cur->Next = readStringLiteral(P, P + 2);
      P += Cur->Len;
      continue;
    }

    // UTF-16字符串字面量
    if (startsWith(P, "u\"")) {
      Cur = Cur->Next = readUTF16StringLiteral(P, P + 1);
      P += Cur->Len;
      continue;
    }

    // 宽字符串字面量
    if (startsWith(P, "L\"")) {
      Cur = Cur->Next = readUTF32StringLiteral(P, P + 1, TyInt);
      P += Cur->Len;
      continue;
    }

    // UTF-32字符串字面量
    if (startsWith(P, "U\"")) {
      Cur = Cur->Next = readUTF32StringLiteral(P, P + 1, TyUInt);
      P += Cur->Len;
      continue;
    }

    // 解析字符字面量
    if (*P == '\'') {
      Cur->Next = readCharLiteral(P, P, TyInt);
      // 单字节字符
      Cur->Val = (char)Cur->Val;
      Cur = Cur->Next;
      P += Cur->Len;
      continue;
    }

    // UTF-16字符字面量
    if (startsWith(P, "u'")) {
      // 使用两个字节
      Cur = Cur->Next = readCharLiteral(P, P + 1, TyUShort);
      Cur->Val &= 0xffff;
      P += Cur->Len;
      continue;
    }

    // 宽字符字面量，占两个字节
    if (startsWith(P, "L'")) {
      Cur = Cur->Next = readCharLiteral(P, P + 1, TyInt);
      P += Cur->Len;
      continue;
    }

    // UTF-32 字符字面量
    if (startsWith(P, "U'")) {
      // 使用四个字节
      Cur = Cur->Next = readCharLiteral(P, P + 1, TyUInt);
      P += Cur->Len;
      continue;
    }

    // 解析标记符或关键字
    // [a-zA-Z_][a-zA-Z0-9_]*
    int IdentLen = readIdent(P);
    if (IdentLen) {
      Cur->Next = newToken(TK_IDENT, P, P + IdentLen);
      Cur = Cur->Next;
      P += Cur->Len;
      continue;
    }

    // 解析操作符
    int PunctLen = readPunct(P);
    if (PunctLen) {
      Cur->Next = newToken(TK_PUNCT, P, P + PunctLen);
      Cur = Cur->Next;
      // 指针前进Punct的长度位
      P += PunctLen;
      continue;
    }

    // 处理无法识别的字符
    errorAt(P, "invalid token");
  }

  // 解析结束，增加一个EOF，表示终止符。
  Cur->Next = newToken(TK_EOF, P, P);
  // 为所有Token添加行号
  addLineNumbers(Head.Next);
  // Head无内容，所以直接返回Next
  return Head.Next;
}

// 返回指定文件的内容
static char *readFile(char *Path) {
  FILE *FP;

  if (strcmp(Path, "-") == 0) {
    // 如果文件名是"-"，那么就从输入中读取
    FP = stdin;
  } else {
    FP = fopen(Path, "r");
    // 文件读取失败
    if (!FP)
      return NULL;
  }

  // 要返回的字符串
  char *Buf;
  size_t BufLen;
  FILE *Out = open_memstream(&Buf, &BufLen);

  // 读取整个文件
  while (true) {
    char Buf2[4096];
    // fread从文件流中读取数据到数组中
    // 数组指针Buf2，数组元素大小1，数组元素个数4096，文件流指针
    int N = fread(Buf2, 1, sizeof(Buf2), FP);
    if (N == 0)
      break;
    // 数组指针Buf2，数组元素大小1，实际元素个数N，文件流指针
    fwrite(Buf2, 1, N, Out);
  }

  // 对文件完成了读取
  if (FP != stdin)
    fclose(FP);

  // 刷新流的输出缓冲区，确保内容都被输出到流中
  fflush(Out);
  // 确保最后一行以'\n'结尾
  if (BufLen == 0 || Buf[BufLen - 1] != '\n')
    // 将字符输出到流中
    fputc('\n', Out);
  fputc('\0', Out);
  fclose(Out);
  return Buf;
}

// 获取输入文件
File **getInputFiles(void) { return InputFiles; }

// 新建一个File
File *newFile(char *Name, int FileNo, char *Contents) {
  File *FP = calloc(1, sizeof(File));
  FP->Name = Name;
  FP->FileNo = FileNo;
  FP->Contents = Contents;
  return FP;
}

// 替换 \r 或 \r\n 为 \n
static void canonicalizeNewline(char *P) {
  int I = 0, J = 0;

  while (P[I]) {
    if (P[I] == '\r' && P[I + 1] == '\n') {
      // 替换\r\n
      I += 2;
      P[J++] = '\n';
    } else if (P[I] == '\r') {
      // 替换\r
      I++;
      P[J++] = '\n';
    } else {
      P[J++] = P[I++];
    }
  }

  P[J] = '\0';
}

// 移除续行，即反斜杠+换行符的形式
static void removeBackslashNewline(char *P) {
  // 旧字符串的索引I（从0开始）
  // 新字符串的索引J（从0开始）
  // 因为J始终<=I，所以二者共用空间，不会有问题
  int I = 0, J = 0;

  // 为了维持行号不变，这里记录了删除的行数
  int N = 0;

  // 如果指向的字符存在
  while (P[I]) {
    // 如果是 '\\'和'\n'
    if (P[I] == '\\' && P[I + 1] == '\n') {
      // I跳过这两个字符
      I += 2;
      // 删除的行数+1
      N++;
    }
    // 如果是换行符
    else if (P[I] == '\n') {
      // P[J]='\n'
      // I、J都+1
      P[J++] = P[I++];
      // 如果删除过N个续行，那么在这里增加N个换行
      // 以保证行号不变
      for (; N > 0; N--)
        P[J++] = '\n';
    }
    // 其他情况，P[J]=P[I]
    // I、J都+1
    else {
      P[J++] = P[I++];
    }
  }

  // 如果最后还删除过N个续行，那么在这里增加N个换行
  for (; N > 0; N--)
    P[J++] = '\n';
  // 以'\0'结束
  P[J] = '\0';
}

// 读取unicode字符
static uint32_t readUniversalChar(char *P, int Len) {
  uint32_t C = 0;
  for (int I = 0; I < Len; I++) {
    if (!isxdigit(P[I]))
      return 0;
    // 左移（十六进制数的）4位，然后存入当前十六进制数
    C = (C << 4) | fromHex(P[I]);
  }
  return C;
}

// 替换\u或\U转义序列为相应的UTF-8编码
static void convertUniversalChars(char *P) {
  char *Q = P;

  while (*P) {
    if (startsWith(P, "\\u")) {
      // 16位(4个十六进制数字)宽字符串字面量
      uint32_t C = readUniversalChar(P + 2, 4);
      if (C) {
        P += 6;
        Q += encodeUTF8(Q, C);
      } else {
        *Q++ = *P++;
      }
    } else if (startsWith(P, "\\U")) {
      // 32位(8个十六进制数字)宽字符串字面量
      uint32_t C = readUniversalChar(P + 2, 8);
      if (C) {
        P += 10;
        Q += encodeUTF8(Q, C);
      } else {
        *Q++ = *P++;
      }
    } else if (P[0] == '\\') {
      // 反斜杠 \ 的匹配
      *Q++ = *P++;
      *Q++ = *P++;
    } else {
      // 其他字符
      *Q++ = *P++;
    }
  }

  *Q = '\0';
}

// 词法分析文件
Token *tokenizeFile(char *Path) {
  // 读取文件内容
  char *P = readFile(Path);
  if (!P)
    return NULL;

  // 规范化换行符
  canonicalizeNewline(P);
  // 移除续行
  removeBackslashNewline(P);
  // 转换unicode字符为UTF-8编码
  convertUniversalChars(P);

  // 文件编号
  static int FileNo;
  // 文件路径，文件编号从1开始，文件内容
  File *FP = newFile(Path, FileNo + 1, P);

  // 为汇编的.file指示保存文件名
  // 最后字符串为空，作为结尾。
  // realloc根据(FileNo + 2)重新分配给定的内存区域
  InputFiles = realloc(InputFiles, sizeof(char *) * (FileNo + 2));
  // 当前文件存入字符串对应编号-1位置
  InputFiles[FileNo] = FP;
  // 最后字符串为空，作为结尾。
  InputFiles[FileNo + 1] = NULL;
  // 文件编号加1
  FileNo++;

  // 词法分析文件
  return tokenize(FP);
}
