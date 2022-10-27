#include "rvcc.h"

// cc1选项
static bool OptCC1;
// ###选项
static bool OptHashHashHash;
// 目标文件的路径
static char *OptO;

// 输入文件的路径
static char *InputPath;

// 输出程序的使用说明
static void usage(int Status) {
  fprintf(stderr, "rvcc [ -o <path> ] <file>\n");
  exit(Status);
}

// 解析传入程序的参数
static void parseArgs(int Argc, char **Argv) {
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
      // 不存在目标文件则报错
      if (!Argv[++I])
        usage(1);
      // 目标文件的路径
      OptO = Argv[I];
      continue;
    }

    // 解析-oXXX的参数
    if (!strncmp(Argv[I], "-o", 2)) {
      // 目标文件的路径
      OptO = Argv[I] + 2;
      continue;
    }

    // 解析为-的参数
    if (Argv[I][0] == '-' && Argv[I][1] != '\0')
      error("unknown argument: %s", Argv[I]);

    // 其他情况则匹配为输入文件
    InputPath = Argv[I];
  }

  // 不存在输入文件时报错
  if (!InputPath)
    error("no input files");
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
static void runCC1(int Argc, char **Argv) {
  // 多开辟10个字符串的位置，用于传递需要新传入的参数
  char **Args = calloc(Argc + 10, sizeof(char *));
  // 将传入程序的参数全部写入Args
  memcpy(Args, Argv, Argc * sizeof(char *));
  // 在选项最后新加入"-cc1"选项
  Args[Argc++] = "-cc1";
  // 运行自身作为子进程，同时传入选项
  runSubprocess(Args);
}

// 编译C文件到汇编文件
static void cc1(void) {
  // 解析文件，生成终结符流
  Token *Tok = tokenizeFile(InputPath);

  // 解析终结符流
  Obj *Prog = parse(Tok);

  // 生成代码
  FILE *Out = openFile(OptO);
  // .file 文件编号 文件名
  fprintf(Out, ".file 1 \"%s\"\n", InputPath);
  codegen(Prog, Out);
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
  // 解析传入程序的参数
  parseArgs(Argc, Argv);

  // 如果指定了-cc1选项
  // 直接编译C文件到汇编文件
  if (OptCC1) {
    cc1();
    return 0;
  }

  // 默认情况下，执行调用cc1程序
  runCC1(Argc, Argv);
  return 0;
}
