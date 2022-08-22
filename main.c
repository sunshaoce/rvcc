#include "rvcc.h"

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

int main(int Argc, char **Argv) {
  // 解析传入程序的参数
  parseArgs(Argc, Argv);

  // 解析文件，生成终结符流
  Token *Tok = tokenizeFile(InputPath);

  // 解析终结符流
  Obj *Prog = parse(Tok);

  // 生成代码
  FILE *Out = openFile(OptO);
  // .file 文件编号 文件名
  fprintf(Out, ".file 1 \"%s\"\n", InputPath);
  codegen(Prog, Out);
  return 0;
}
