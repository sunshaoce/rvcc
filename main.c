#include "rvcc.h"

// 【注意】
// 如果是交叉编译，请把这个路径改为$RISCV对应的路径
// 注意 ~ 应替换为具体的 /home/用户名 的路径
static char *RVPath = "";

// 文件类型
typedef enum {
  FILE_NONE, // 空类型
  FILE_C,    // C语言源代码类型
  FILE_ASM,  // 汇编代码类型
  FILE_OBJ,  // 可重定位文件类型
  FILE_AR,   // 静态库文件类型
  FILE_DSO,  // 动态库文件类型
} FileType;

// 引入路径区
StringArray IncludePaths;
// common块默认生成
bool OptFCommon = true;
// 位置无关代码的标记
bool OptFPIC;

// -x选项
static FileType OptX;
// -include所引入的文件
static StringArray OptInclude;
// -E选项
static bool OptE;
// -M选项
static bool OptM;
// -MD选项
static bool OptMD;
// -MMD选项
static bool OptMMD;
// -MP选项
static bool OptMP;
// -S选项
static bool OptS;
// -c选项
static bool OptC;
// cc1选项
static bool OptCC1;
// ###选项
static bool OptHashHashHash;
//-static选项
static bool OptStatic;
// -shared选项
static bool OptShared;
// -MF选项
static char *OptMF;
// -MT选项
static char *OptMT;
// 目标文件的路径
static char *OptO;

// 链接器额外参数
static StringArray LdExtraArgs;
// 标准库所引入的路径，用于-MMD选项
static StringArray StdIncludePaths;

// 输入文件名
char *BaseFile;
// 输出文件名
static char *OutputFile;

// 输入文件区
static StringArray InputPaths;
// 临时文件区
static StringArray TmpFiles;

// 输出程序的使用说明
static void usage(int Status) {
  fprintf(stderr, "rvcc [ -o <path> ] <file>\n");
  exit(Status);
}

// 判断需要一个参数的选项，是否具有一个参数
static bool takeArg(char *Arg) {
  char *X[] = {"-o", "-I",  "-idirafter", "-include",
               "-x", "-MF", "-MT",        "-Xlinker"};

  for (int I = 0; I < sizeof(X) / sizeof(*X); I++)
    if (!strcmp(Arg, X[I]))
      return true;
  return false;
}

// 增加默认引入路径
static void addDefaultIncludePaths(char *Argv0) {
  // rvcc特定的引入文件被安装到了argv[0]的./include位置
  strArrayPush(&IncludePaths, format("%s/include", dirname(strdup(Argv0))));

  // 支持标准的引入路径
  strArrayPush(&IncludePaths, "/usr/local/include");
  strArrayPush(&IncludePaths, "/usr/include/riscv64-linux-gnu");
  strArrayPush(&IncludePaths, "/usr/include");

  // 为-MMD选项，复制一份标准库引入路径
  for (int I = 0; I < IncludePaths.Len; I++)
    strArrayPush(&StdIncludePaths, IncludePaths.Data[I]);
}

// 定义宏
static void define(char *Str) {
  char *Eq = strchr(Str, '=');
  if (Eq)
    // 存在赋值，使用该值
    defineMacro(strndup(Str, Eq - Str), Eq + 1);
  else
    // 不存在赋值，则设为1
    defineMacro(Str, "1");
}

// 解析-x选项
static FileType parseOptX(char *S) {
  // -xc，解析为C语言源代码
  if (!strcmp(S, "c"))
    return FILE_C;
  // -xassembler，解析为汇编源代码
  if (!strcmp(S, "assembler"))
    return FILE_ASM;
  // -xnone，解析为空类型
  if (!strcmp(S, "none"))
    return FILE_NONE;
  error("<command line>: unknown argument for -x: %s", S);
}

// 对Make的目标中的特殊字符进行处理
static char *quoteMakefile(char *S) {
  // 新字符串，确保即使S的全部字符都处理，加上'\0'也能够存储下
  char *Buf = calloc(1, strlen(S) * 2 + 1);

  // 遍历字符串，对特殊字符进行处理
  for (int I = 0, J = 0; S[I]; I++) {
    switch (S[I]) {
    case '$':
      Buf[J++] = '$';
      Buf[J++] = '$';
      break;
    case '#':
      Buf[J++] = '\\';
      Buf[J++] = '#';
      break;
    case ' ':
    case '\t':
      // 反向遍历反斜杠字符
      for (int K = I - 1; K >= 0 && S[K] == '\\'; K--)
        Buf[J++] = '\\';
      Buf[J++] = '\\';
      Buf[J++] = S[I];
      break;
    default:
      Buf[J++] = S[I];
      break;
    }
  }
  // 返回新字符串
  return Buf;
}

// 解析传入程序的参数
static void parseArgs(int Argc, char **Argv) {
  // 确保需要一个参数的选项，存在一个参数
  for (int I = 1; I < Argc; I++)
    // 如果需要一个参数
    if (takeArg(Argv[I]))
      // 如果不存在一个参数，则打印出使用说明
      if (!Argv[++I])
        usage(1);

  // 存储-idirafter的路径参数
  StringArray Idirafter = {};

  // 遍历所有传入程序的参数
  for (int I = 1; I < Argc; I++) {
    // 解析-###
    if (!strcmp(Argv[I], "-###")) {
      OptHashHashHash = true;
      continue;
    }

    // 解析-cc1
    if (!strcmp(Argv[I], "-cc1")) {
      OptCC1 = true;
      continue;
    }

    // 如果存在help，则直接显示用法说明
    if (!strcmp(Argv[I], "--help"))
      usage(0);

    // 解析-o XXX的参数
    if (!strcmp(Argv[I], "-o")) {
      // 目标文件的路径
      OptO = Argv[++I];
      continue;
    }

    // 解析-oXXX的参数
    if (!strncmp(Argv[I], "-o", 2)) {
      // 目标文件的路径
      OptO = Argv[I] + 2;
      continue;
    }

    // 解析-S
    if (!strcmp(Argv[I], "-S")) {
      OptS = true;
      continue;
    }

    // // 解析-fcommon
    if (!strcmp(Argv[I], "-fcommon")) {
      OptFCommon = true;
      continue;
    }

    // 解析-fno-common
    if (!strcmp(Argv[I], "-fno-common")) {
      OptFCommon = false;
      continue;
    }

    // 解析-c
    if (!strcmp(Argv[I], "-c")) {
      OptC = true;
      continue;
    }

    // 解析-E
    if (!strcmp(Argv[I], "-E")) {
      OptE = true;
      continue;
    }

    // 解析-I
    if (!strncmp(Argv[I], "-I", 2)) {
      strArrayPush(&IncludePaths, Argv[I] + 2);
      continue;
    }

    // 解析-D
    if (!strcmp(Argv[I], "-D")) {
      define(Argv[++I]);
      continue;
    }

    // 解析-D
    if (!strncmp(Argv[I], "-D", 2)) {
      define(Argv[I] + 2);
      continue;
    }

    // 解析-U
    if (!strcmp(Argv[I], "-U")) {
      undefMacro(Argv[++I]);
      continue;
    }

    // 解析-U
    if (!strncmp(Argv[I], "-U", 2)) {
      undefMacro(Argv[I] + 2);
      continue;
    }

    // 解析-include
    if (!strcmp(Argv[I], "-include")) {
      strArrayPush(&OptInclude, Argv[++I]);
      continue;
    }

    // 解析-x
    if (!strcmp(Argv[I], "-x")) {
      OptX = parseOptX(Argv[++I]);
      continue;
    }

    // 解析-x
    if (!strncmp(Argv[I], "-x", 2)) {
      OptX = parseOptX(Argv[I] + 2);
      continue;
    }

    // 解析-l、-Wl,
    if (!strncmp(Argv[I], "-l", 2) || !strncmp(Argv[I], "-Wl,", 4)) {
      strArrayPush(&InputPaths, Argv[I]);
      continue;
    }

    // 解析-Xlinker
    if (!strcmp(Argv[I], "-Xlinker")) {
      strArrayPush(&LdExtraArgs, Argv[++I]);
      continue;
    }

    // 解析-s
    if (!strcmp(Argv[I], "-s")) {
      strArrayPush(&LdExtraArgs, "-s");
      continue;
    }

    // 解析-M
    if (!strcmp(Argv[I], "-M")) {
      OptM = true;
      continue;
    }

    // 解析-MF
    if (!strcmp(Argv[I], "-MF")) {
      OptMF = Argv[++I];
      continue;
    }

    // 解析-MP
    if (!strcmp(Argv[I], "-MP")) {
      OptMP = true;
      continue;
    }

    // 解析-MT
    // `-MT File`，指定File为依赖规则中的目标
    if (!strcmp(Argv[I], "-MT")) {
      if (OptMT == NULL)
        // 无依赖规则中的目标
        OptMT = Argv[++I];
      else
        // 合并依赖规则中的目标
        OptMT = format("%s %s", OptMT, Argv[++I]);
      continue;
    }

    // 解析-MD
    if (!strcmp(Argv[I], "-MD")) {
      OptMD = true;
      continue;
    }

    // 解析-MQ
    if (!strcmp(Argv[I], "-MQ")) {
      if (OptMT == NULL)
        // 无依赖规则中的目标
        OptMT = quoteMakefile(Argv[++I]);
      else
        // 合并依赖规则中的目标
        OptMT = format("%s %s", OptMT, quoteMakefile(Argv[++I]));
      continue;
    }

    // 解析-MMD
    if (!strcmp(Argv[I], "-MMD")) {
      // 同时启用-MD选项
      OptMD = OptMMD = true;
      continue;
    }

    // 解析-fpic或-fPIC
    if (!strcmp(Argv[I], "-fpic") || !strcmp(Argv[I], "-fPIC")) {
      OptFPIC = true;
      continue;
    }

    // 解析-cc1-input
    if (!strcmp(Argv[I], "-cc1-input")) {
      BaseFile = Argv[++I];
      continue;
    }

    // 解析-cc1-output
    if (!strcmp(Argv[I], "-cc1-output")) {
      OutputFile = Argv[++I];
      continue;
    }

    // 解析-idirafter
    // 将参数存入Idirafter
    if (!strcmp(Argv[I], "-idirafter")) {
      strArrayPush(&Idirafter, Argv[I++]);
      continue;
    }

    // 解析-static
    if (!strcmp(Argv[I], "-static")) {
      OptStatic = true;
      strArrayPush(&LdExtraArgs, "-static");
      continue;
    }

    // 解析-shared
    if (!strcmp(Argv[I], "-shared")) {
      OptShared = true;
      strArrayPush(&LdExtraArgs, "-shared");
      continue;
    }

    // 解析-L
    if (!strcmp(Argv[I], "-L")) {
      strArrayPush(&LdExtraArgs, "-L");
      strArrayPush(&LdExtraArgs, Argv[++I]);
      continue;
    }

    // 解析-L
    if (!strncmp(Argv[I], "-L", 2)) {
      strArrayPush(&LdExtraArgs, "-L");
      strArrayPush(&LdExtraArgs, Argv[I] + 2);
      continue;
    }

    // 哈希表测试
    if (!strcmp(Argv[I], "-hashmap-test")) {
      hashmapTest();
      exit(0);
    }

    // 忽略多个选项
    if (!strncmp(Argv[I], "-O", 2) || !strncmp(Argv[I], "-W", 2) ||
        !strncmp(Argv[I], "-g", 2) || !strncmp(Argv[I], "-std=", 5) ||
        !strcmp(Argv[I], "-ffreestanding") ||
        !strcmp(Argv[I], "-fno-builtin") ||
        !strcmp(Argv[I], "-fno-omit-frame-pointer") ||
        !strcmp(Argv[I], "-fno-stack-protector") ||
        !strcmp(Argv[I], "-fno-strict-aliasing") || !strcmp(Argv[I], "-m64") ||
        !strcmp(Argv[I], "-mno-red-zone") || !strcmp(Argv[I], "-w") ||
        !strcmp(Argv[I], "-march=native"))
      continue;

    // 解析为-的参数
    if (Argv[I][0] == '-' && Argv[I][1] != '\0')
      error("unknown argument: %s", Argv[I]);

    // 其他情况则匹配为输入文件
    strArrayPush(&InputPaths, Argv[I]);
  }

  // 将所用Idirafter内的路径，都存入引用路径区中
  for (int I = 0; I < Idirafter.Len; I++)
    strArrayPush(&IncludePaths, Idirafter.Data[I]);

  // 不存在输入文件时报错
  if (InputPaths.Len == 0)
    error("no input files");

  // -E隐式包含输入是C语言的宏
  if (OptE)
    OptX = FILE_C;
}

// 打开需要写入的文件
static FILE *openFile(char *Path) {
  if (!Path || strcmp(Path, "-") == 0)
    return stdout;

  // 以写入模式打开文件
  FILE *Out = fopen(Path, "w");
  if (!Out)
    error("cannot open output file: %s: %s", Path, strerror(errno));
  return Out;
}

// 判断字符串P是否以字符串Q结尾
static bool endsWith(char *P, char *Q) {
  int len1 = strlen(P);
  int len2 = strlen(Q);
  // P比Q长且P最后Len2长度的字符和Q相等，则为真
  return (len1 >= len2) && !strcmp(P + len1 - len2, Q);
}

// 替换文件的后缀名
static char *replaceExtn(char *Tmpl, char *Extn) {
  // 去除路径，返回基础文件名
  char *Filename = basename(strdup(Tmpl));
  // 最后一次字符出现的位置
  char *Dot = strrchr(Filename, '.');
  // 如果存在'.'，清除此后的内容
  if (Dot)
    *Dot = '\0';
  // 将新后缀写入文件名中
  return format("%s%s", Filename, Extn);
}

// 清理临时文件区
static void cleanup(void) {
  // 遍历删除临时文件
  for (int I = 0; I < TmpFiles.Len; I++)
    unlink(TmpFiles.Data[I]);
}

// 创建临时文件
static char *createTmpFile(void) {
  // 临时文件的路径格式
  char *Path = strdup("/tmp/rvcc-XXXXXX");
  // 创建临时文件
  int FD = mkstemp(Path);
  // 临时文件创建失败
  if (FD == -1)
    error("mkstemp failed: %s", strerror(errno));
  // 关闭文件
  close(FD);

  // 将文件路径存入临时文件区中
  strArrayPush(&TmpFiles, Path);
  return Path;
}

// 开辟子进程
static void runSubprocess(char **Argv) {
  // 打印出子进程所有的命令行参数
  if (OptHashHashHash) {
    // 程序名
    fprintf(stderr, "%s", Argv[0]);
    // 程序参数
    for (int I = 1; Argv[I]; I++)
      fprintf(stderr, " %s", Argv[I]);
    // 换行
    fprintf(stderr, "\n");
  }

  // Fork–exec模型
  // 创建当前进程的副本，这里开辟了一个子进程
  // 返回-1表示错位，为0表示成功
  if (fork() == 0) {
    // 执行文件rvcc，没有斜杠时搜索环境变量，此时会替换子进程
    execvp(Argv[0], Argv);
    // 如果exec函数返回，表明没有正常执行命令
    fprintf(stderr, "exec failed: %s: %s\n", Argv[0], strerror(errno));
    _exit(1);
  }

  // 父进程， 等待子进程结束
  int Status;
  while (wait(&Status) > 0)
    ;
  // 处理子进程返回值
  if (Status != 0)
    exit(1);
}

// 执行调用cc1程序
// 因为rvcc自身就是cc1程序
// 所以调用自身，并传入-cc1参数作为子进程
static void runCC1(int Argc, char **Argv, char *Input, char *Output) {
  // 多开辟10个字符串的位置，用于传递需要新传入的参数
  char **Args = calloc(Argc + 10, sizeof(char *));
  // 将传入程序的参数全部写入Args
  memcpy(Args, Argv, Argc * sizeof(char *));
  // 在选项最后新加入"-cc1"选项
  Args[Argc++] = "-cc1";

  // 存入输入文件的参数
  if (Input) {
    Args[Argc++] = "-cc1-input";
    Args[Argc++] = Input;
  }

  // 存入输出文件的参数
  if (Output) {
    Args[Argc++] = "-cc1-output";
    Args[Argc++] = Output;
  }

  // 运行自身作为子进程，同时传入选项
  runSubprocess(Args);
}

// 当指定-E选项时，打印出所有终结符
static void printTokens(Token *Tok) {
  // 输出文件，默认为stdout
  FILE *Out = openFile(OptO ? OptO : "-");

  // 记录行数
  int Line = 1;
  // 遍历读取终结符
  for (; Tok->Kind != TK_EOF; Tok = Tok->Next) {
    // 位于行首打印出换行符
    if (Line > 1 && Tok->AtBOL)
      fprintf(Out, "\n");
    // 打印出需要空格的位置
    if (Tok->HasSpace && !Tok->AtBOL)
      fprintf(Out, " ");
    // 打印出终结符
    fprintf(Out, "%.*s", Tok->Len, Tok->Loc);
    Line++;
  }
  // 文件以换行符结尾
  fprintf(Out, "\n");
}

// 判断是否为标准库路径
static bool inStdIncludePath(char *Path) {
  for (int I = 0; I < StdIncludePaths.Len; I++) {
    char *Dir = StdIncludePaths.Data[I];
    int Len = strlen(Dir);
    // 与库路径相同，且以斜杠结尾
    if (strncmp(Dir, Path, Len) == 0 && Path[Len] == '/')
      return true;
  }
  return false;
}

// 输出可用于Make的规则，自动化文件依赖管理
static void printDependencies(void) {
  char *Path;
  if (OptMF)
    // 将Make的规则写入`-MF File`的File中
    Path = OptMF;
  else if (OptMD)
    // 相当于`-M -MF File.d`，将Make的规则写入.d文件
    Path = replaceExtn(OptO ? OptO : BaseFile, ".d");
  else if (OptO)
    Path = OptO;
  else
    Path = "-";

  // 输出文件
  FILE *Out = openFile(Path);
  // 如果未指定-MT，默认需要目标名中的特殊字符处理
  if (OptMT)
    fprintf(Out, "%s:", OptMT);
  else
    // -MF指定依赖规则中的目标，否则替换后缀为.o
    fprintf(Out, "%s:", quoteMakefile(replaceExtn(BaseFile, ".o")));

  // 获取输入文件
  File **Files = getInputFiles();

  // 遍历输入文件，并将格式化的结果写入输出文件
  for (int I = 0; Files[I]; I++) {
    // 不输出标准库内的文件
    if (OptMMD && inStdIncludePath(Files[I]->Name))
      continue;
    fprintf(Out, " \\\n  %s", Files[I]->Name);
  }

  fprintf(Out, "\n\n");

  // 如果指定了-MP，则为头文件生成伪目标
  if (OptMP) {
    for (int I = 1; Files[I]; I++) {
      // 不输出标准库内的文件
      if (OptMMD && inStdIncludePath(Files[I]->Name))
        continue;
      // 处理头文件中的特殊字符
      fprintf(Out, "%s:\n\n", quoteMakefile(Files[I]->Name));
    }
  }
}

// 解析文件，生成终结符流
static Token *mustTokenizeFile(char *Path) {
  Token *Tok = tokenizeFile(Path);
  // 终结符流生成失败，对应文件报错
  if (!Tok)
    error("%s: %s", Path, strerror(errno));
  return Tok;
}

// 拼接终结符链表
static Token *appendTokens(Token *Tok1, Token *Tok2) {
  // Tok1为空时直接返回Tok2
  if (!Tok1 || Tok1->Kind == TK_EOF)
    return Tok2;

  // 链表指针T
  Token *T = Tok1;
  // T指向遍历到Tok1链表中最后一个
  while (T->Next->Kind != TK_EOF)
    T = T->Next;
  // T->Next指向Tok2
  T->Next = Tok2;
  // 返回拼接好的Tok1
  return Tok1;
}

// 编译C文件到汇编文件
static void cc1(void) {
  Token *Tok = NULL;

  // 处理-include选项
  for (int I = 0; I < OptInclude.Len; I++) {
    // 需要引入的文件
    char *Incl = OptInclude.Data[I];

    char *Path;
    if (fileExists(Incl)) {
      // 如果文件存在，则直接使用路径
      Path = Incl;
    } else {
      // 否则搜索引入路径区
      Path = searchIncludePaths(Incl);
      if (!Path)
        error("-include: %s: %s", Incl, strerror(errno));
    }

    // 解析文件，生成终结符流
    Token *Tok2 = mustTokenizeFile(Path);
    Tok = appendTokens(Tok, Tok2);
  }

  // 解析文件，生成终结符流
  Token *Tok2 = mustTokenizeFile(BaseFile);
  Tok = appendTokens(Tok, Tok2);

  // 预处理
  Tok = preprocess(Tok);

  // 如果指定了-M，打印出文件的依赖关系
  if (OptM || OptMD) {
    printDependencies();
    if (OptM)
      return;
  }

  // 如果指定了-E那么打印出预处理过的C代码
  if (OptE) {
    printTokens(Tok);
    return;
  }

  // 解析终结符流
  Obj *Prog = parse(Tok);

  // 生成代码

  // 防止编译器在编译途中退出，而只生成了部分的文件
  // 开启临时输出缓冲区
  char *Buf;
  size_t BufLen;
  FILE *OutputBuf = open_memstream(&Buf, &BufLen);

  // 输出汇编到缓冲区中
  codegen(Prog, OutputBuf);
  fclose(OutputBuf);

  // 从缓冲区中写入到文件中
  FILE *Out = openFile(OutputFile);
  fwrite(Buf, BufLen, 1, Out);
  fclose(Out);
}

// 调用汇编器
static void assemble(char *Input, char *Output) {
  // 选择对应环境内的汇编器
  char *As = strlen(RVPath)
                 ? format("%s/bin/riscv64-unknown-linux-gnu-as", RVPath)
                 : "as";
  char *Cmd[] = {As, "-c", Input, "-o", Output, NULL};
  runSubprocess(Cmd);
}

// 查找文件
static char *findFile(char *Pattern) {
  char *Path = NULL;
  // Linux文件系统中路径名称的模式匹配
  glob_t Buf = {};
  // 参数：用来模式匹配的路径，标记（例如是否排序结果），错误处理函数，结果存放缓冲区
  glob(Pattern, 0, NULL, &Buf);
  // gl_pathc匹配到的路径计数
  // 复制最后的一条匹配结果到Path中
  if (Buf.gl_pathc > 0)
    Path = strdup(Buf.gl_pathv[Buf.gl_pathc - 1]);
  // 释放内存
  globfree(&Buf);
  return Path;
}

// 文件存在时，为真
bool fileExists(char *Path) {
  struct stat St;
  return !stat(Path, &St);
}

// 查找库路径
static char *findLibPath(void) {
  if (fileExists("/usr/lib/riscv64-linux-gnu/crti.o"))
    return "/usr/lib/riscv64-linux-gnu";
  if (fileExists("/usr/lib64/crti.o"))
    return "/usr/lib64";

  if (fileExists(format("%s/sysroot/usr/lib/crti.o", RVPath)))
    return format("%s/sysroot/usr/lib/", RVPath);
  error("library path is not found");
  return NULL;
}

// 查找gcc库路径
static char *findGCCLibPath(void) {
  char *paths[] = {
      "/usr/lib/gcc/riscv64-linux-gnu/*/crtbegin.o",
      // Gentoo
      "/usr/lib/gcc/riscv64-pc-linux-gnu/*/crtbegin.o",
      // Fedora
      "/usr/lib/gcc/riscv64-redhat-linux/*/crtbegin.o",
      // 交叉编译
      format("%s/lib/gcc/riscv64-unknown-linux-gnu/*/crtbegin.o", RVPath),
  };

  // 遍历以查找gcc库的路径
  for (int I = 0; I < sizeof(paths) / sizeof(*paths); I++) {
    char *path = findFile(paths[I]);
    if (path)
      return dirname(path);
  }

  error("gcc library path is not found");
  return NULL;
}

// 运行链接器ld
static void runLinker(StringArray *Inputs, char *Output) {
  // 需要传递ld子进程的参数
  StringArray Arr = {};

  // 链接器
  char *Ld = strlen(RVPath)
                 ? format("%s/bin/riscv64-unknown-linux-gnu-ld", RVPath)
                 : "ld";
  strArrayPush(&Arr, Ld);

  // 输出文件
  strArrayPush(&Arr, "-o");
  strArrayPush(&Arr, Output);
  strArrayPush(&Arr, "-m");
  strArrayPush(&Arr, "elf64lriscv");
  if (!OptStatic) {
    strArrayPush(&Arr, "-dynamic-linker");

    char *LP64D =
        strlen(RVPath)
            ? format("%s/sysroot/lib/ld-linux-riscv64-lp64d.so.1", RVPath)
            : "/lib/ld-linux-riscv64-lp64d.so.1";
    strArrayPush(&Arr, LP64D);
  }

  char *LibPath = findLibPath();
  char *GccLibPath = findGCCLibPath();

  if (OptShared) {
    strArrayPush(&Arr, format("%s/crti.o", LibPath));
    strArrayPush(&Arr, format("%s/crtbeginS.o", GccLibPath));
  } else {
    strArrayPush(&Arr, format("%s/crt1.o", LibPath));
    strArrayPush(&Arr, format("%s/crti.o", LibPath));
    strArrayPush(&Arr, format("%s/crtbegin.o", GccLibPath));
  }

  strArrayPush(&Arr, format("-L%s", GccLibPath));

  if (strlen(RVPath)) {
    strArrayPush(&Arr, format("-L%s/sysroot/usr/lib64", RVPath));
    strArrayPush(&Arr, format("-L%s/sysroot/lib64", RVPath));
    strArrayPush(&Arr,
                 format("-L%s/sysroot/usr/lib/riscv64-linux-gnu", RVPath));
    strArrayPush(&Arr,
                 format("-L%s/sysroot/usr/lib/riscv64-pc-linux-gnu", RVPath));
    strArrayPush(&Arr,
                 format("-L%s/sysroot/usr/lib/riscv64-redhat-linux", RVPath));
    strArrayPush(&Arr, format("-L%s/sysroot/usr/lib", RVPath));
    strArrayPush(&Arr, format("-L%s/sysroot/lib", RVPath));
  } else {
    strArrayPush(&Arr, "-L/usr/lib64");
    strArrayPush(&Arr, "-L/lib64");
    strArrayPush(&Arr, "-L/usr/lib/riscv64-linux-gnu");
    strArrayPush(&Arr, "-L/usr/lib/riscv64-pc-linux-gnu");
    strArrayPush(&Arr, "-L/usr/lib/riscv64-redhat-linux");
    strArrayPush(&Arr, "-L/usr/lib");
    strArrayPush(&Arr, "-L/lib");
  }

  // 链接器额外参数存入到链接器参数中
  for (int I = 0; I < LdExtraArgs.Len; I++)
    strArrayPush(&Arr, LdExtraArgs.Data[I]);

  // 输入文件，存入到链接器参数中
  for (int I = 0; I < Inputs->Len; I++)
    strArrayPush(&Arr, Inputs->Data[I]);

  if (OptStatic) {
    strArrayPush(&Arr, "--start-group");
    strArrayPush(&Arr, "-lgcc");
    strArrayPush(&Arr, "-lgcc_eh");
    strArrayPush(&Arr, "-lc");
    strArrayPush(&Arr, "--end-group");
  } else {
    strArrayPush(&Arr, "-lc");
    strArrayPush(&Arr, "-lgcc");
    strArrayPush(&Arr, "--as-needed");
    strArrayPush(&Arr, "-lgcc_s");
    strArrayPush(&Arr, "--no-as-needed");
  }

  if (OptShared)
    strArrayPush(&Arr, format("%s/crtendS.o", GccLibPath));
  else
    strArrayPush(&Arr, format("%s/crtend.o", GccLibPath));
  strArrayPush(&Arr, format("%s/crtn.o", LibPath));
  strArrayPush(&Arr, NULL);

  // 开辟的链接器子进程
  runSubprocess(Arr.Data);
}

// 获取文件的类型
static FileType getFileType(char *Filename) {
  // 若-x指定了不为空的类型，使用该类型
  if (OptX != FILE_NONE)
    return OptX;

  // 以.a结尾的文件，解析为静态库文件类型
  if (endsWith(Filename, ".a"))
    return FILE_AR;
  // 以.so结尾的文件，解析为动态库文件类型
  if (endsWith(Filename, ".so"))
    return FILE_DSO;
  // 以.o结尾的文件，解析为空重定位文件类型
  if (endsWith(Filename, ".o"))
    return FILE_OBJ;
  // 以.c结尾的文件，解析为C语言源代码类型
  if (endsWith(Filename, ".c"))
    return FILE_C;
  // 以.s结尾的文件，解析为汇编类型
  if (endsWith(Filename, ".s"))
    return FILE_ASM;

  error("<command line>: unknown file extension: %s", Filename);
}

// 编译器驱动流程
//
// 源文件
//   ↓
// 预处理器预处理后的文件
//   ↓
// cc1编译为汇编文件
//   ↓
// as编译为可重定位文件
//   ↓
// ld链接为可执行文件

// rvcc的程序入口函数
int main(int Argc, char **Argv) {
  // 在程序退出时，执行cleanup函数
  atexit(cleanup);
  // 初始化预定义的宏
  initMacros();
  // 解析传入程序的参数
  parseArgs(Argc, Argv);

  // 如果指定了-cc1选项
  // 直接编译C文件到汇编文件
  if (OptCC1) {
    // 增加默认引入路径
    addDefaultIncludePaths(Argv[0]);
    cc1();
    return 0;
  }

  // 当前不能指定-c、-S、-E后，将多个输入文件，输出到一个文件中
  if (InputPaths.Len > 1 && OptO && (OptC || OptS || OptE))
    error("cannot specify '-o' with '-c', '-S' or '-E' with multiple files");

  // 链接器参数
  StringArray LdArgs = {};

  // 遍历每个输入文件
  for (int I = 0; I < InputPaths.Len; I++) {
    // 读取输入文件
    char *Input = InputPaths.Data[I];

    // 链接时搜索指定的库文件
    if (!strncmp(Input, "-l", 2)) {
      strArrayPush(&LdArgs, Input);
      continue;
    }

    // 匹配到 -Wl, 将后续参数存入链接器选项
    if (!strncmp(Input, "-Wl,", 4)) {
      // 以 , 分割开的参数字符串
      char *S = strdup(Input + 4);
      // 以 , 分割字符串，并返回一个参数
      char *Arg = strtok(S, ",");
      // 循环遍历剩余的参数
      while (Arg) {
        // 加入到链接器参数
        strArrayPush(&LdArgs, Arg);
        Arg = strtok(NULL, ",");
      }
      continue;
    }

    // 输出文件
    char *Output;
    // 如果指定了输出文件，则直接使用
    if (OptO)
      Output = OptO;
    // 若未指定输出的汇编文件名，则输出到后缀为.s的同名文件中
    else if (OptS)
      Output = replaceExtn(Input, ".s");
    // 若未指定输出的可重定位文件名，则输出到后缀为.o的同名文件中
    else
      Output = replaceExtn(Input, ".o");

    // 获取输入文件的类型
    FileType Ty = getFileType(Input);

    // 处理.o或.a或.so文件
    if (Ty == FILE_OBJ || Ty == FILE_AR || Ty == FILE_DSO) {
      // 存入链接器选项中
      strArrayPush(&LdArgs, Input);
      continue;
    }

    // 处理.s文件
    if (Ty == FILE_ASM) {
      // 如果没有指定-S，那么需要进行汇编
      if (!OptS)
        assemble(Input, Output);
      continue;
    }

    // 处理.c文件
    assert(Ty == FILE_C);

    // 只进行解析
    if (OptE || OptM) {
      runCC1(Argc, Argv, Input, NULL);
      continue;
    }

    // 如果有-S选项，那么执行调用cc1程序
    if (OptS) {
      runCC1(Argc, Argv, Input, Output);
      continue;
    }

    // 编译并汇编
    if (OptC) {
      // 临时文件Tmp作为cc1输出的汇编文件
      char *Tmp = createTmpFile();
      // cc1，编译C文件为汇编文件
      runCC1(Argc, Argv, Input, Tmp);
      // as，编译汇编文件为可重定位文件
      assemble(Tmp, Output);
      continue;
    }

    // 否则运行cc1和as
    // 临时文件Tmp1作为cc1输出的汇编文件
    // 临时文件Tmp2作为as输出的可重定位文件
    char *Tmp1 = createTmpFile();
    char *Tmp2 = createTmpFile();
    // cc1，编译C文件为汇编文件
    runCC1(Argc, Argv, Input, Tmp1);
    // as，编译汇编文件为可重定位文件
    assemble(Tmp1, Tmp2);
    // 将Tmp2存入链接器选项
    strArrayPush(&LdArgs, Tmp2);
    continue;
  }

  // 需要链接的情况
  // 未指定文件名时，默认为a.out
  if (LdArgs.Len > 0)
    runLinker(&LdArgs, OptO ? OptO : "a.out");

  return 0;
}
