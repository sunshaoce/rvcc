#include "rvcc.h"

// 输出文件
static FILE *OutputFile;
// 记录栈深度
static int Depth;
// 用于函数参数的寄存器们
static char *ArgReg[] = {"a0", "a1", "a2", "a3", "a4", "a5"};
// 当前的函数
static Obj *CurrentFn;

static void genExpr(Node *Nd);
static void genStmt(Node *Nd);

// 输出字符串到目标文件并换行
static void printLn(char *Fmt, ...) {
  va_list VA;

  va_start(VA, Fmt);
  vfprintf(OutputFile, Fmt, VA);
  va_end(VA);

  fprintf(OutputFile, "\n");
}

// 代码段计数
static int count(void) {
  static int I = 1;
  return I++;
}

// 压栈，将结果临时压入栈中备用
// sp为栈指针，栈反向向下增长，64位下，8个字节为一个单位，所以sp-8
// 当前栈指针的地址就是sp，将a0的值压入栈
// 不使用寄存器存储的原因是因为需要存储的值的数量是变化的。
static void push(void) {
  printLn("  # 压栈，将a0的值存入栈顶");
  printLn("  addi sp, sp, -8");
  printLn("  sd a0, 0(sp)");
  Depth++;
}

// 弹栈，将sp指向的地址的值，弹出到a1
static void pop(char *Reg) {
  printLn("  # 弹栈，将栈顶的值存入%s", Reg);
  printLn("  ld %s, 0(sp)", Reg);
  printLn("  addi sp, sp, 8");
  Depth--;
}

// 对齐到Align的整数倍
int alignTo(int N, int Align) {
  // (0,Align]返回Align
  return (N + Align - 1) / Align * Align;
}

// 计算给定节点的绝对地址
// 如果报错，说明节点不在内存中
static void genAddr(Node *Nd) {
  switch (Nd->Kind) {
  // 变量
  case ND_VAR:
    if (Nd->Var->IsLocal) { // 偏移量是相对于fp的
      printLn("  # 获取局部变量%s的栈内地址为%d(fp)", Nd->Var->Name,
              Nd->Var->Offset);
      printLn("  addi a0, fp, %d", Nd->Var->Offset);
    } else {
      printLn("  # 获取全局变量%s的地址", Nd->Var->Name);
      printLn("  la a0, %s", Nd->Var->Name);
    }
    return;
  // 解引用*
  case ND_DEREF:
    genExpr(Nd->LHS);
    return;
  // 逗号
  case ND_COMMA:
    genExpr(Nd->LHS);
    genAddr(Nd->RHS);
    return;
  // 结构体成员
  case ND_MEMBER:
    genAddr(Nd->LHS);
    printLn("  # 计算成员变量的地址偏移量");
    printLn("  li t0, %d", Nd->Mem->Offset);
    printLn("  add a0, a0, t0");
    return;
  default:
    break;
  }

  errorTok(Nd->Tok, "not an lvalue");
}

// 加载a0指向的值
static void load(Type *Ty) {
  if (Ty->Kind == TY_ARRAY || Ty->Kind == TY_STRUCT || Ty->Kind == TY_UNION)
    return;

  printLn("  # 读取a0中存放的地址，得到的值存入a0");
  if (Ty->Size == 1)
    printLn("  lb a0, 0(a0)");
  else if (Ty->Size == 2)
    printLn("  lh a0, 0(a0)");
  else if (Ty->Size == 4)
    printLn("  lw a0, 0(a0)");
  else
    printLn("  ld a0, 0(a0)");
}

// 将栈顶值(为一个地址)存入a0
static void store(Type *Ty) {
  pop("a1");

  if (Ty->Kind == TY_STRUCT || Ty->Kind == TY_UNION) {
    printLn("  # 对%s进行赋值", Ty->Kind == TY_STRUCT ? "结构体" : "联合体");
    for (int I = 0; I < Ty->Size; ++I) {
      printLn("  li t0, %d", I);
      printLn("  add t0, a0, t0");
      printLn("  lb t1, 0(t0)");

      printLn("  li t0, %d", I);
      printLn("  add t0, a1, t0");
      printLn("  sb t1, 0(t0)");
    }
    return;
  }

  printLn("  # 将a0的值，写入到a1中存放的地址");
  if (Ty->Size == 1)
    printLn("  sb a0, 0(a1)");
  else if (Ty->Size == 2)
    printLn("  sh a0, 0(a1)");
  else if (Ty->Size == 4)
    printLn("  sw a0, 0(a1)");
  else
    printLn("  sd a0, 0(a1)");
};

// 类型枚举
enum { I8, I16, I32, I64 };

// 获取类型对应的枚举值
static int getTypeId(Type *Ty) {
  switch (Ty->Kind) {
  case TY_CHAR:
    return I8;
  case TY_SHORT:
    return I16;
  case TY_INT:
    return I32;
  default:
    return I64;
  }
}

// 类型映射表
// 先逻辑左移N位，再算术右移N位，就实现了将64位有符号数转换为64-N位的有符号数
static char i64i8[] = "  # 转换为i8类型\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56";
static char i64i16[] = "  # 转换为i16类型\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48";
static char i64i32[] = "  # 转换为i32类型\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";

// 所有类型转换表
static char *castTable[10][10] = {
    // clang-format off

    // 被映射到
    // {i8,  i16,    i32,    i64}
    {NULL,   NULL,   NULL,   NULL}, // 从i8转换
    {i64i8,  NULL,   NULL,   NULL}, // 从i16转换
    {i64i8,  i64i16, NULL,   NULL}, // 从i32转换
    {i64i8,  i64i16, i64i32, NULL}, // 从i64转换

    // clang-format on
};

// 类型转换
static void cast(Type *From, Type *To) {
  if (To->Kind == TY_VOID)
    return;

  if (To->Kind == TY_BOOL) {
    printLn("  # 转为bool类型：为0置0，非0置1");
    printLn("  snez a0, a0");
    return;
  }

  // 获取类型的枚举值
  int T1 = getTypeId(From);
  int T2 = getTypeId(To);
  if (castTable[T1][T2]) {
    printLn("  # 转换函数");
    printLn("%s", castTable[T1][T2]);
  }
}

// 生成表达式
static void genExpr(Node *Nd) {
  // .loc 文件编号 行号
  printLn("  .loc 1 %d", Nd->Tok->LineNo);

  // 生成各个根节点
  switch (Nd->Kind) {
  // 空表达式
  case ND_NULL_EXPR:
    return;
  // 加载数字到a0
  case ND_NUM:
    printLn("  # 将%d加载到a0中", Nd->Val);
    printLn("  li a0, %ld", Nd->Val);
    return;
  // 对寄存器取反
  case ND_NEG:
    genExpr(Nd->LHS);
    // neg a0, a0是sub a0, x0, a0的别名, 即a0=0-a0
    printLn("  # 对a0值进行取反");
    printLn("  neg%s a0, a0", Nd->Ty->Size <= 4 ? "w" : "");
    return;
  // 变量
  case ND_VAR:
  case ND_MEMBER:
    // 计算出变量的地址，然后存入a0
    genAddr(Nd);
    load(Nd->Ty);
    return;
  // 解引用
  case ND_DEREF:
    genExpr(Nd->LHS);
    load(Nd->Ty);
    return;
  // 取地址
  case ND_ADDR:
    genAddr(Nd->LHS);
    return;
  // 赋值
  case ND_ASSIGN:
    // 左部是左值，保存值到的地址
    genAddr(Nd->LHS);
    push();
    // 右部是右值，为表达式的值
    genExpr(Nd->RHS);
    store(Nd->Ty);
    return;
  // 语句表达式
  case ND_STMT_EXPR:
    for (Node *N = Nd->Body; N; N = N->Next)
      genStmt(N);
    return;
  // 逗号
  case ND_COMMA:
    genExpr(Nd->LHS);
    genExpr(Nd->RHS);
    return;
  // 类型转换
  case ND_CAST:
    genExpr(Nd->LHS);
    cast(Nd->LHS->Ty, Nd->Ty);
    return;
  // 条件运算符
  case ND_COND: {
    int C = count();
    printLn("\n# =====条件运算符%d===========", C);
    genExpr(Nd->Cond);
    printLn("  # 条件判断，为0则跳转");
    printLn("  beqz a0, .L.else.%d", C);
    genExpr(Nd->Then);
    printLn("  # 跳转到条件运算符结尾部分");
    printLn("  j .L.end.%d", C);
    printLn(".L.else.%d:", C);
    genExpr(Nd->Els);
    printLn(".L.end.%d:", C);
    return;
  }
  // 非运算
  case ND_NOT:
    genExpr(Nd->LHS);
    printLn("  # 非运算");
    // a0=0则置1，否则为0
    printLn("  seqz a0, a0");
    return;
  // 逻辑与
  case ND_LOGAND: {
    int C = count();
    printLn("\n# =====逻辑与%d===============", C);
    genExpr(Nd->LHS);
    // 判断是否为短路操作
    printLn("  # 左部短路操作判断，为0则跳转");
    printLn("  beqz a0, .L.false.%d", C);
    genExpr(Nd->RHS);
    printLn("  # 右部判断，为0则跳转");
    printLn("  beqz a0, .L.false.%d", C);
    printLn("  li a0, 1");
    printLn("  j .L.end.%d", C);
    printLn(".L.false.%d:", C);
    printLn("  li a0, 0");
    printLn(".L.end.%d:", C);
    return;
  }
  // 逻辑或
  case ND_LOGOR: {
    int C = count();
    printLn("\n# =====逻辑或%d===============", C);
    genExpr(Nd->LHS);
    // 判断是否为短路操作
    printLn("  # 左部短路操作判断，不为0则跳转");
    printLn("  bnez a0, .L.true.%d", C);
    genExpr(Nd->RHS);
    printLn("  # 右部判断，不为0则跳转");
    printLn("  bnez a0, .L.true.%d", C);
    printLn("  li a0, 0");
    printLn("  j .L.end.%d", C);
    printLn(".L.true.%d:", C);
    printLn("  li a0, 1");
    printLn(".L.end.%d:", C);
    return;
  }
  // 按位取非运算
  case ND_BITNOT:
    genExpr(Nd->LHS);
    printLn("  # 按位取反");
    // 这里的 not a0, a0 为 xori a0, a0, -1 的伪码
    printLn("  not a0, a0");
    return;
  // 函数调用
  case ND_FUNCALL: {
    // 记录参数个数
    int NArgs = 0;
    // 计算所有参数的值，正向压栈
    for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
      genExpr(Arg);
      push();
      NArgs++;
    }

    // 反向弹栈，a0->参数1，a1->参数2……
    for (int i = NArgs - 1; i >= 0; i--)
      pop(ArgReg[i]);

    // 调用函数
    printLn("  # 调用%s函数", Nd->FuncName);
    printLn("  call %s", Nd->FuncName);
    return;
  }
  default:
    break;
  }

  // 递归到最右节点
  genExpr(Nd->RHS);
  // 将结果压入栈
  push();
  // 递归到左节点
  genExpr(Nd->LHS);
  // 将结果弹栈到a1
  pop("a1");

  // 生成各个二叉树节点
  char *Suffix = Nd->LHS->Ty->Kind == TY_LONG || Nd->LHS->Ty->Base ? "" : "w";
  switch (Nd->Kind) {
  case ND_ADD: // + a0=a0+a1
    printLn("  # a0+a1，结果写入a0");
    printLn("  add%s a0, a0, a1", Suffix);
    return;
  case ND_SUB: // - a0=a0-a1
    printLn("  # a0-a1，结果写入a0");
    printLn("  sub%s a0, a0, a1", Suffix);
    return;
  case ND_MUL: // * a0=a0*a1
    printLn("  # a0×a1，结果写入a0");
    printLn("  mul%s a0, a0, a1", Suffix);
    return;
  case ND_DIV: // / a0=a0/a1
    printLn("  # a0÷a1，结果写入a0");
    printLn("  div%s a0, a0, a1", Suffix);
    return;
  case ND_MOD: // % a0=a0%a1
    printLn("  # a0%%a1，结果写入a0");
    printLn("  rem%s a0, a0, a1", Suffix);
    return;
  case ND_BITAND: // & a0=a0&a1
    printLn("  # a0&a1，结果写入a0");
    printLn("  and a0, a0, a1");
    return;
  case ND_BITOR: // | a0=a0|a1
    printLn("  # a0|a1，结果写入a0");
    printLn("  or a0, a0, a1");
    return;
  case ND_BITXOR: // ^ a0=a0^a1
    printLn("  # a0^a1，结果写入a0");
    printLn("  xor a0, a0, a1");
    return;
  case ND_EQ:
  case ND_NE:
    // a0=a0^a1，异或指令
    printLn("  # 判断是否a0%sa1", Nd->Kind == ND_EQ ? "=" : "≠");
    printLn("  xor a0, a0, a1");

    if (Nd->Kind == ND_EQ)
      // a0==a1
      // a0=a0^a1, sltiu a0, a0, 1
      // 等于0则置1
      printLn("  seqz a0, a0");
    else
      // a0!=a1
      // a0=a0^a1, sltu a0, x0, a0
      // 不等于0则置1
      printLn("  snez a0, a0");
    return;
  case ND_LT:
    printLn("  # 判断a0<a1");
    printLn("  slt a0, a0, a1");
    return;
  case ND_LE:
    // a0<=a1等价于
    // a0=a1<a0, a0=a0^1
    printLn("  # 判断是否a0≤a1");
    printLn("  slt a0, a1, a0");
    printLn("  xori a0, a0, 1");
    return;
  case ND_SHL:
    printLn("  # a0逻辑左移a1位");
    printLn("  sll%s a0, a0, a1", Suffix);
    return;
  case ND_SHR:
    printLn("  # a0算术右移a1位");
    printLn("  sra%s a0, a0, a1", Suffix);
    return;
  default:
    break;
  }

  errorTok(Nd->Tok, "invalid expression");
}

// 生成语句
static void genStmt(Node *Nd) {
  // .loc 文件编号 行号
  printLn("  .loc 1 %d", Nd->Tok->LineNo);

  switch (Nd->Kind) {
  // 生成if语句
  case ND_IF: {
    // 代码段计数
    int C = count();
    printLn("\n# =====分支语句%d==============", C);
    // 生成条件内语句
    printLn("\n# Cond表达式%d", C);
    genExpr(Nd->Cond);
    // 判断结果是否为0，为0则跳转到else标签
    printLn("  # 若a0为0，则跳转到分支%d的.L.else.%d段", C, C);
    printLn("  beqz a0, .L.else.%d", C);
    // 生成符合条件后的语句
    printLn("\n# Then语句%d", C);
    genStmt(Nd->Then);
    // 执行完后跳转到if语句后面的语句
    printLn("  # 跳转到分支%d的.L.end.%d段", C, C);
    printLn("  j .L.end.%d", C);
    // else代码块，else可能为空，故输出标签
    printLn("\n# Else语句%d", C);
    printLn("# 分支%d的.L.else.%d段标签", C, C);
    printLn(".L.else.%d:", C);
    // 生成不符合条件后的语句
    if (Nd->Els)
      genStmt(Nd->Els);
    // 结束if语句，继续执行后面的语句
    printLn("\n# 分支%d的.L.end.%d段标签", C, C);
    printLn(".L.end.%d:", C);
    return;
  }
  // 生成for或while循环语句
  case ND_FOR: {
    // 代码段计数
    int C = count();
    printLn("\n# =====循环语句%d===============", C);
    // 生成初始化语句
    if (Nd->Init) {
      printLn("\n# Init语句%d", C);
      genStmt(Nd->Init);
    }
    // 输出循环头部标签
    printLn("\n# 循环%d的.L.begin.%d段标签", C, C);
    printLn(".L.begin.%d:", C);
    // 处理循环条件语句
    printLn("# Cond表达式%d", C);
    if (Nd->Cond) {
      // 生成条件循环语句
      genExpr(Nd->Cond);
      // 判断结果是否为0，为0则跳转到结束部分
      printLn("  # 若a0为0，则跳转到循环%d的%s段", C, Nd->BrkLabel);
      printLn("  beqz a0, %s", Nd->BrkLabel);
    }
    // 生成循环体语句
    printLn("\n# Then语句%d", C);
    genStmt(Nd->Then);
    // continue标签语句
    printLn("%s:", Nd->ContLabel);
    // 处理循环递增语句
    if (Nd->Inc) {
      printLn("\n# Inc语句%d", C);
      // 生成循环递增语句
      genExpr(Nd->Inc);
    }
    // 跳转到循环头部
    printLn("  # 跳转到循环%d的.L.begin.%d段", C, C);
    printLn("  j .L.begin.%d", C);
    // 输出循环尾部标签
    printLn("\n# 循环%d的%s段标签", C, Nd->BrkLabel);
    printLn("%s:", Nd->BrkLabel);
    return;
  }
  case ND_SWITCH:
    printLn("\n# =====switch语句===============");
    genExpr(Nd->Cond);

    printLn("  # 遍历跳转到值等于a0的case标签");
    for (Node *N = Nd->CaseNext; N; N = N->CaseNext) {
      printLn("  li t0, %ld", N->Val);
      printLn("  beq a0, t0, %s", N->Label);
    }

    if (Nd->DefaultCase) {
      printLn("  # 跳转到default标签");
      printLn("  j %s", Nd->DefaultCase->Label);
    }

    printLn("  # 结束switch，跳转break标签");
    printLn("  j %s", Nd->BrkLabel);
    // 生成case标签的语句
    genStmt(Nd->Then);
    printLn("# switch的break标签，结束switch");
    printLn("%s:", Nd->BrkLabel);
    return;
  case ND_CASE:
    printLn("# case标签，值为%ld", Nd->Val);
    printLn("%s:", Nd->Label);
    genStmt(Nd->LHS);
    return;
  // 生成代码块，遍历代码块的语句链表
  case ND_BLOCK:
    for (Node *N = Nd->Body; N; N = N->Next)
      genStmt(N);
    return;
  // goto语句
  case ND_GOTO:
    printLn("  j %s", Nd->UniqueLabel);
    return;
  // 标签语句
  case ND_LABEL:
    printLn("%s:", Nd->UniqueLabel);
    genStmt(Nd->LHS);
    return;
  // 生成return语句
  case ND_RETURN:
    printLn("# 返回语句");
    genExpr(Nd->LHS);
    // 无条件跳转语句，跳转到.L.return段
    // j offset是 jal x0, offset的别名指令
    printLn("  # 跳转到.L.return.%s段", CurrentFn->Name);
    printLn("  j .L.return.%s", CurrentFn->Name);
    return;
  // 生成表达式语句
  case ND_EXPR_STMT:
    genExpr(Nd->LHS);
    return;
  default:
    break;
  }

  errorTok(Nd->Tok, "invalid statement");
}

// 根据变量的链表计算出偏移量
static void assignLVarOffsets(Obj *Prog) {
  // 为每个函数计算其变量所用的栈空间
  for (Obj *Fn = Prog; Fn; Fn = Fn->Next) {
    // 如果不是函数,则终止
    if (!Fn->IsFunction)
      continue;

    int Offset = 0;
    // 读取所有变量
    for (Obj *Var = Fn->Locals; Var; Var = Var->Next) {
      // 每个变量分配空间
      Offset += Var->Ty->Size;
      // 对齐变量
      Offset = alignTo(Offset, Var->Ty->Align);
      // 为每个变量赋一个偏移量，或者说是栈中地址
      Var->Offset = -Offset;
    }
    // 将栈对齐到16字节
    Fn->StackSize = alignTo(Offset, 16);
  }
}

static void emitData(Obj *Prog) {
  for (Obj *Var = Prog; Var; Var = Var->Next) {
    if (Var->IsFunction)
      continue;

    printLn("\n  # 数据段标签");
    printLn("  .data");
    // 判断是否有初始值
    if (Var->InitData) {
      printLn("%s:", Var->Name);
      // 打印出字符串的内容，包括转义字符
      for (int I = 0; I < Var->Ty->Size; ++I) {
        char C = Var->InitData[I];
        if (isprint(C))
          printLn("  .byte %d\t# 字符：%c", C, C);
        else
          printLn("  .byte %d", C);
      }
    } else {
      printLn("\n  # 全局段%s", Var->Name);
      printLn("  .globl %s", Var->Name);
      printLn("%s:", Var->Name);
      printLn("  # 全局变量零填充%d位", Var->Ty->Size);
      printLn("  .zero %d", Var->Ty->Size);
    }
  }
}

// 将整形寄存器的值存入栈中
static void storeGeneral(int Reg, int Offset, int Size) {
  printLn("  # 将%s寄存器的值存入%d(fp)的栈地址", ArgReg[Reg], Offset);
  switch (Size) {
  case 1:
    printLn("  sb %s, %d(fp)", ArgReg[Reg], Offset);
    return;
  case 2:
    printLn("  sh %s, %d(fp)", ArgReg[Reg], Offset);
    return;
  case 4:
    printLn("  sw %s, %d(fp)", ArgReg[Reg], Offset);
    return;
  case 8:
    printLn("  sd %s, %d(fp)", ArgReg[Reg], Offset);
    return;
  }
  unreachable();
}

// 代码生成入口函数，包含代码块的基础信息
void emitText(Obj *Prog) {
  // 为每个函数单独生成代码
  for (Obj *Fn = Prog; Fn; Fn = Fn->Next) {
    if (!Fn->IsFunction || !Fn->IsDefinition)
      continue;

    if (Fn->IsStatic) {
      printLn("\n  # 定义局部%s函数", Fn->Name);
      printLn("  .local %s", Fn->Name);
    } else {
      printLn("\n  # 定义全局%s函数", Fn->Name);
      printLn("  .globl %s", Fn->Name);
    }

    printLn("  # 代码段标签");
    printLn("  .text");
    printLn("# =====%s段开始===============", Fn->Name);
    printLn("# %s段标签", Fn->Name);
    printLn("%s:", Fn->Name);
    CurrentFn = Fn;

    // 栈布局
    //-------------------------------// sp
    //              ra
    //-------------------------------// ra = sp-8
    //              fp
    //-------------------------------// fp = sp-16
    //             变量
    //-------------------------------// sp = sp-16-StackSize
    //           表达式计算
    //-------------------------------//

    // Prologue, 前言
    // 将ra寄存器压栈,保存ra的值
    printLn("  # 将ra寄存器压栈,保存ra的值");
    printLn("  addi sp, sp, -16");
    printLn("  sd ra, 8(sp)");
    // 将fp压入栈中，保存fp的值
    printLn("  # 将fp压栈，fp属于“被调用者保存”的寄存器，需要恢复原值");
    printLn("  sd fp, 0(sp)");
    // 将sp写入fp
    printLn("  # 将sp的值写入fp");
    printLn("  mv fp, sp");

    // 偏移量为实际变量所用的栈大小
    printLn("  # sp腾出StackSize大小的栈空间");
    printLn("  addi sp, sp, -%d", Fn->StackSize);

    int I = 0;
    for (Obj *Var = Fn->Params; Var; Var = Var->Next)
      storeGeneral(I++, Var->Offset, Var->Ty->Size);

    // 生成语句链表的代码
    printLn("# =====%s段主体===============", Fn->Name);
    genStmt(Fn->Body);
    assert(Depth == 0);

    // Epilogue，后语
    // 输出return段标签
    printLn("# =====%s段结束===============", Fn->Name);
    printLn("# return段标签");
    printLn(".L.return.%s:", Fn->Name);
    // 将fp的值改写回sp
    printLn("  # 将fp的值写回sp");
    printLn("  mv sp, fp");
    // 将最早fp保存的值弹栈，恢复fp。
    printLn("  # 将最早fp保存的值弹栈，恢复fp和sp");
    printLn("  ld fp, 0(sp)");
    // 将ra寄存器弹栈,恢复ra的值
    printLn("  # 将ra寄存器弹栈,恢复ra的值");
    printLn("  ld ra, 8(sp)");
    printLn("  addi sp, sp, 16");
    // 返回
    printLn("  # 返回a0值给系统调用");
    printLn("  ret");
  }
}

void codegen(Obj *Prog, FILE *Out) {
  // 设置目标文件的文件流指针
  OutputFile = Out;

  // 计算局部变量的偏移量
  assignLVarOffsets(Prog);
  // 生成数据
  emitData(Prog);
  // 生成代码
  emitText(Prog);
}
