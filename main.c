#include "rvcc.h"

int main(int Argc, char **Argv) {
  // 判断传入程序的参数是否为2个，Argv[0]为程序名称，Argv[1]为传入的第一个参数
  if (Argc != 2) {
    // 异常处理，提示参数数量不对。
    // fprintf，格式化文件输出，往文件内写入字符串
    // stderr，异常文件（Linux一切皆文件），用于往屏幕显示异常信息
    // %s，字符串
    error("%s: invalid number of arguments", Argv[0]);
  }

  // 解析Argv[1]，生成终结符流
  Token *Tok = tokenize(Argv[1]);

  // 解析终结符流
  Obj *Prog = parse(Tok);

  // 生成代码
  codegen(Prog);

  return 0;
}
