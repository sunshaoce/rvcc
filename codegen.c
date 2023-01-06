#include "rvcc.h"

#define GP_MAX 8
#define FP_MAX 8

// 输出文件
static FILE *OutputFile;
// 记录栈深度
static int Depth;
// 记录大结构体的深度
static int BSDepth;
// 当前的函数
static Obj *CurrentFn;

static void genExpr(Node *Nd);
static void genStmt(Node *Nd);

// 输出字符串到目标文件并换行
__attribute__((format(printf, 1, 2))) static void printLn(char *Fmt, ...) {
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
static void pop(int Reg) {
  printLn("  # 弹栈，将栈顶的值存入a%d", Reg);
  printLn("  ld a%d, 0(sp)", Reg);
  printLn("  addi sp, sp, 8");
  Depth--;
}

// 对于浮点类型进行压栈
static void pushF(void) {
  printLn("  # 压栈，将fa0的值存入栈顶");
  printLn("  addi sp, sp, -8");
  printLn("  fsd fa0, 0(sp)");
  Depth++;
}

// 对于浮点类型进行弹栈
static void popF(int Reg) {
  printLn("  # 弹栈，将栈顶的值存入fa%d", Reg);
  printLn("  fld fa%d, 0(sp)", Reg);
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
    // 局部变量
    if (Nd->Var->IsLocal) { // 偏移量是相对于fp的
      printLn("  # 获取局部变量%s的栈内地址为%d(fp)", Nd->Var->Name,
              Nd->Var->Offset);
      printLn("  li t0, %d", Nd->Var->Offset);
      printLn("  add a0, fp, t0");
      return;
    }

    // 线程局部变量
    if (Nd->Var->IsTLS) {
      // 计算TLS高20位地址
      printLn("  lui a0, %%tprel_hi(%s)", Nd->Var->Name);
      // 计算TLS低12位地址
      printLn("  addi a0, a0, %%tprel_lo(%s)", Nd->Var->Name);
      return;
    }

    // 函数
    if (Nd->Ty->Kind == TY_FUNC) {
      // 定义的函数
      if (Nd->Var->IsDefinition) {
        printLn("  # 获取函数%s的地址", Nd->Var->Name);
        printLn("  la a0, %s", Nd->Var->Name);
      }
      // 外部函数
      else {
        int C = count();
        printLn("  # 获取外部函数的绝对地址");
        printLn(".Lpcrel_hi%d:", C);
        // 高20位地址，存到a0中
        printLn("  auipc a0, %%got_pcrel_hi(%s)", Nd->Var->Name);
        // 低12位地址，加到a0中
        printLn("  ld a0, %%pcrel_lo(.Lpcrel_hi%d)(a0)", C);
      }
      return;
    }

    // 全局变量
    int C = count();
    printLn("  # 获取全局变量的绝对地址");
    printLn(".Lpcrel_hi%d:", C);
    // 高20位地址，存到a0中
    printLn("  auipc a0, %%got_pcrel_hi(%s)", Nd->Var->Name);
    // 低12位地址，加到a0中
    printLn("  ld a0, %%pcrel_lo(.Lpcrel_hi%d)(a0)", C);
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
  // 函数调用
  case ND_FUNCALL:
    // 如果存在返回值缓冲区
    if (Nd->RetBuffer) {
      genExpr(Nd);
      return;
    }
    break;
  default:
    break;
  }

  errorTok(Nd->Tok, "not an lvalue");
}

// 加载a0指向的值
static void load(Type *Ty) {
  switch (Ty->Kind) {
  case TY_ARRAY:
  case TY_STRUCT:
  case TY_UNION:
  case TY_FUNC:
    return;
  case TY_FLOAT:
    printLn("  # 访问a0中存放的地址，取得的值存入fa0");
    printLn("  flw fa0, 0(a0)");
    return;
  case TY_DOUBLE:
    printLn("  # 访问a0中存放的地址，取得的值存入fa0");
    printLn("  fld fa0, 0(a0)");
    return;
  default:
    break;
  }

  // 添加无符号类型的后缀u
  char *Suffix = Ty->IsUnsigned ? "u" : "";

  printLn("  # 读取a0中存放的地址，得到的值存入a0");
  if (Ty->Size == 1)
    printLn("  lb%s a0, 0(a0)", Suffix);
  else if (Ty->Size == 2)
    printLn("  lh%s a0, 0(a0)", Suffix);
  else if (Ty->Size == 4)
    printLn("  lw%s a0, 0(a0)", Suffix);
  else
    printLn("  ld a0, 0(a0)");
}

// 将栈顶值(为一个地址)存入a0
static void store(Type *Ty) {
  pop(1);

  switch (Ty->Kind) {
  case TY_STRUCT:
  case TY_UNION:
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
  case TY_FLOAT:
    printLn("  # 将fa0的值，写入到a1中存放的地址");
    printLn("  fsw fa0, 0(a1)");
    return;
  case TY_DOUBLE:
    printLn("  # 将fa0的值，写入到a1中存放的地址");
    printLn("  fsd fa0, 0(a1)");
    return;
  default:
    break;
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

// 与0进行比较，不等于0则置1
static void notZero(Type *Ty) {
  switch (Ty->Kind) {
  case TY_FLOAT:
    printLn("  # 判断fa1是否不为0，为0置0，非0置1");
    printLn("  fmv.s.x fa1, zero");
    printLn("  feq.s a0, fa0, fa1");
    printLn("  xori a0, a0, 1");
    return;
  case TY_DOUBLE:
    printLn("  # 判断fa1是否不为0，为0置0，非0置1");
    printLn("  fmv.d.x fa1, zero");
    printLn("  feq.d a0, fa0, fa1");
    printLn("  xori a0, a0, 1");
    return;
  default:
    return;
  }
}

// 类型枚举
enum { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64 };

// 获取类型对应的枚举值
static int getTypeId(Type *Ty) {
  switch (Ty->Kind) {
  case TY_CHAR:
    return Ty->IsUnsigned ? U8 : I8;
  case TY_SHORT:
    return Ty->IsUnsigned ? U16 : I16;
  case TY_INT:
    return Ty->IsUnsigned ? U32 : I32;
  case TY_LONG:
    return Ty->IsUnsigned ? U64 : I64;
  case TY_FLOAT:
    return F32;
  case TY_DOUBLE:
    return F64;
  default:
    return U64;
  }
}

// 类型映射表
// 有符号整型转换
static char i32f32[] = "  # i32转换为f32类型\n"
                       "  fcvt.s.w fa0, a0";
static char i32f64[] = "  # i32转换为f64类型\n"
                       "  fcvt.d.w fa0, a0";

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

// 先逻辑左移N位，再逻辑右移N位，就实现了将64位无符号数转换为64-N位的无符号数
static char i64u8[] = "  # 转换为u8类型\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56";
static char i64u16[] = "  # 转换为u16类型\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48";
static char i64u32[] = "  # 转换为u32类型\n"
                       "  slli a0, a0, 32\n"
                       "  srli a0, a0, 32";

// 有符号整型转换为浮点数
static char i64f32[] = "  # i64转换为f32类型\n"
                       "  fcvt.s.l fa0, a0";
static char i64f64[] = "  # i64转换为f64类型\n"
                       "  fcvt.d.l fa0, a0";

// 无符号整型转换
static char u32f32[] = "  # u32转换为f32类型\n"
                       "  fcvt.s.wu fa0, a0";
static char u32f64[] = "  # u32转换为f64类型\n"
                       "  fcvt.d.wu fa0, a0";

static char u32i64[] = "  # u32转换为i64类型\n"
                       "  slli a0, a0, 32\n"
                       "  srli a0, a0, 32";

// 无符号整型转换为浮点数
static char u64f32[] = "  # u64转换为f32类型\n"
                       "  fcvt.s.lu fa0, a0";
static char u64f64[] = "  # u64转换为f64类型\n"
                       "  fcvt.d.lu fa0, a0";

// 单精度浮点数转换为整型
static char f32i8[] = "  # f32转换为i8类型\n"
                      "  fcvt.w.s a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56";
static char f32i16[] = "  # f32转换为i16类型\n"
                       "  fcvt.w.s a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48";
static char f32i32[] = "  # f32转换为i32类型\n"
                       "  fcvt.w.s a0, fa0, rtz\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";
static char f32i64[] = "  # f32转换为i64类型\n"
                       "  fcvt.l.s a0, fa0, rtz";

// 单精度浮点数转换为无符号浮点数
static char f32u8[] = "  # f32转换为u8类型\n"
                      "  fcvt.wu.s a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56";
static char f32u16[] = "  # f32转换为u16类型\n"
                       "  fcvt.wu.s a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48\n";
static char f32u32[] = "  # f32转换为u32类型\n"
                       "  fcvt.wu.s a0, fa0, rtz\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";
static char f32u64[] = "  # f32转换为u64类型\n"
                       "  fcvt.lu.s a0, fa0, rtz";

// 单精度转换为双精度浮点数
static char f32f64[] = "  # f32转换为f64类型\n"
                       "  fcvt.d.s fa0, fa0";

// 双精度浮点数转换为整型
static char f64i8[] = "  # f64转换为i8类型\n"
                      "  fcvt.w.d a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56";
static char f64i16[] = "  # f64转换为i16类型\n"
                       "  fcvt.w.d a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48";
static char f64i32[] = "  # f64转换为i32类型\n"
                       "  fcvt.w.d a0, fa0, rtz\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";
static char f64i64[] = "  # f64转换为i64类型\n"
                       "  fcvt.l.d a0, fa0, rtz";

// 双精度浮点数转换为无符号整型
static char f64u8[] = "  # f64转换为u8类型\n"
                      "  fcvt.wu.d a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56";
static char f64u16[] = "  # f64转换为u16类型\n"
                       "  fcvt.wu.d a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48";
static char f64u32[] = "  # f64转换为u32类型\n"
                       "  fcvt.wu.d a0, fa0, rtz\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";
static char f64u64[] = "  # f64转换为u64类型\n"
                       "  fcvt.lu.d a0, fa0, rtz";

// 双精度转换为单精度浮点数
static char f64f32[] = "  # f64转换为f32类型\n"
                       "  fcvt.s.d fa0, fa0";

// 所有类型转换表
static char *castTable[11][11] = {
    // clang-format off

  // 被映射到
  // {i8,  i16,     i32,     i64,     u8,     u16,     u32,     u64,     f32,     f64}
  {NULL,   NULL,    NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i8转换
  {i64i8,  NULL,    NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i16转换
  {i64i8,  i64i16,  NULL,    NULL,    i64u8,  i64u16,  i64u32,  NULL,    i32f32,  i32f64}, // 从i32转换
  {i64i8,  i64i16,  i64i32,  NULL,    i64u8,  i64u16,  i64u32,  NULL,    i64f32,  i64f64}, // 从i64转换

  {i64i8,  NULL,    NULL,    NULL,    NULL,   NULL,    NULL,    NULL,    u32f32,  u32f64}, // 从u8转换
  {i64i8,  i64i16,  NULL,    NULL,    i64u8,  NULL,    NULL,    NULL,    u32f32,  u32f64}, // 从u16转换
  {i64i8,  i64i16,  i64i32,  u32i64,  i64u8,  i64u16,  NULL,    u32i64,  u32f32,  u32f64}, // 从u32转换
  {i64i8,  i64i16,  i64i32,  NULL,    i64u8,  i64u16,  i64u32,  NULL,    u64f32,  u64f64}, // 从u64转换

  {f32i8,  f32i16,  f32i32,  f32i64,  f32u8,  f32u16,  f32u32,  f32u64,  NULL,    f32f64}, // 从f32转换
  {f64i8,  f64i16,  f64i32,  f64i64,  f64u8,  f64u16,  f64u32,  f64u64,  f64f32,  NULL},   // 从f64转换

    // clang-format on
};

// 类型转换
static void cast(Type *From, Type *To) {
  if (To->Kind == TY_VOID)
    return;

  if (To->Kind == TY_BOOL) {
    notZero(From);
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

// 获取浮点结构体的成员类型
void getFloStMemsTy(Type *Ty, Type **RegsTy, int *Idx) {
  switch (Ty->Kind) {
  case TY_STRUCT:
    // 遍历结构体的成员，获取成员类型
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next)
      getFloStMemsTy(Mem->Ty, RegsTy, Idx);
    return;
  case TY_UNION:
    // 含有联合体不是浮点结构体
    *Idx += 2;
    return;
  case TY_ARRAY:
    // 遍历数组的成员，计算是否为浮点结构体
    for (int I = 0; I < Ty->ArrayLen; ++I)
      getFloStMemsTy(Ty->Base, RegsTy, Idx);
    return;
  default:
    // 若为基础类型，且存在可用寄存器时，填充成员的类型
    if (*Idx < 2)
      RegsTy[*Idx] = Ty;
    *Idx += 1;
    return;
  }
}

// 是否为一或两个含浮点成员变量的结构体
void setFloStMemsTy(Type **Ty, int GP, int FP) {
  Type *T = *Ty;
  T->FSReg1Ty = TyVoid;
  T->FSReg2Ty = TyVoid;

  // 联合体不通过浮点寄存器传递
  if (T->Kind == TY_UNION)
    return;

  // RTy：RegsType，结构体的第一、二个寄存器的类型
  Type *RTy[2] = {TyVoid, TyVoid};
  // 记录可以使用的寄存器的索引值
  int RegsTyIdx = 0;
  // 获取浮点结构体的寄存器类型，如果不是则为TyVoid
  getFloStMemsTy(T, RTy, &RegsTyIdx);

  // 不是浮点结构体，直接退出
  if (RegsTyIdx > 2)
    return;

  if ( // 只有一个浮点成员的结构体，使用1个FP
      (isFloNum(RTy[0]) && RTy[1] == TyVoid && FP < FP_MAX) ||
      // 一个浮点成员和一个整型成员的结构体，使用1个FP和1个GP
      (isFloNum(RTy[0]) && isInteger(RTy[1]) && FP < FP_MAX && GP < GP_MAX) ||
      (isInteger(RTy[0]) && isFloNum(RTy[1]) && FP < FP_MAX && GP < GP_MAX) ||
      // 两个浮点成员的结构体，使用2个FP
      (isFloNum(RTy[0]) && isFloNum(RTy[1]) && FP + 1 < FP_MAX)) {
    T->FSReg1Ty = RTy[0];
    T->FSReg2Ty = RTy[1];
  }
}

// 为大结构体开辟空间
static int createBSSpace(Node *Args) {
  int BSStack = 0;
  for (Node *Arg = Args; Arg; Arg = Arg->Next) {
    Type *Ty = Arg->Ty;
    // 大于16字节的结构体
    if (Ty->Size > 16 && Ty->Kind == TY_STRUCT) {
      printLn("  # 大于16字节的结构体，先开辟相应的栈空间");
      int Sz = alignTo(Ty->Size, 8);
      printLn("  addi sp, sp, -%d", Sz);
      // t6指向了最终的 大结构体空间的起始位置
      printLn("  mv t6, sp");
      Depth += Sz / 8;
      BSStack += Sz / 8;
      BSDepth += Sz / 8;
    }
  }
  return BSStack;
}

// 传递结构体的指针
static void pushStruct(Type *Ty) {
  // 大于16字节的结构体
  if (Ty->Size > 16) {
    // 将结构体复制一份到栈中，然后通过寄存器或栈传递被复制结构体的地址
    // ---------------------------------
    //             大结构体      ←
    // --------------------------------- <- t6
    //      栈传递的   其他变量
    // ---------------------------------
    //            大结构体的指针  ↑
    // --------------------------------- <- sp

    // 计算大结构体的偏移量
    int Sz = alignTo(Ty->Size, 8);
    // BSDepth记录了剩余 大结构体的字节数
    BSDepth -= Sz / 8;
    // t6存储了，大结构体空间的起始位置
    int BSOffset = BSDepth * 8;

    printLn("  # 复制%d字节的大结构体到%d(t6)的位置", Sz, BSOffset);
    for (int I = 0; I < Sz; I++) {
      printLn("  lb t0, %d(a0)", I);
      printLn("  sb t0, %d(t6)", BSOffset + I);
    }

    printLn("  # 大于16字节的结构体，对该结构体地址压栈");
    printLn("  addi a0, t6, %d", BSOffset);
    push();
    return;
  }

  // 含有两个成员（含浮点）的结构体
  // 展开到栈内的两个8字节的空间
  if ((isFloNum(Ty->FSReg1Ty) && Ty->FSReg2Ty != TyVoid) ||
      isFloNum(Ty->FSReg2Ty)) {
    printLn("  # 对含有两个成员（含浮点）结构体进行压栈");
    printLn("  addi sp, sp, -16");
    Depth += 2;

    printLn("  ld t0, 0(a0)");
    printLn("  sd t0, 0(sp)");

    // 计算第二部分在结构体中的偏移量，为两个成员间的最大尺寸
    int Off = MAX(Ty->FSReg1Ty->Size, Ty->FSReg2Ty->Size);
    printLn("  ld t0, %d(a0)", Off);
    printLn("  sd t0, 8(sp)");

    return;
  }
  // 处理只有一个浮点成员的结构体
  // 或者是小于16字节的结构体
  char *Str = isFloNum(Ty->FSReg1Ty) ? "只有一个浮点" : "小于16字节";
  int Sz = alignTo(Ty->Size, 8);
  printLn("  # 为%s的结构体开辟%d字节的空间，", Str, Sz);
  printLn("  addi sp, sp, -%d", Sz);
  Depth += Sz / 8;

  printLn("  # 开辟%d字节的空间，复制%s的内存", Sz, Str);
  for (int I = 0; I < Ty->Size; I++) {
    printLn("  lb t0, %d(a0)", I);
    printLn("  sb t0, %d(sp)", I);
  }
  return;
}

// 将函数实参计算后压入栈中
static void pushArgs2(Node *Args, bool FirstPass) {
  // 参数为空直接返回
  if (!Args)
    return;

  // 递归到最后一个实参进行
  pushArgs2(Args->Next, FirstPass);

  // 第一遍对栈传递的变量进行压栈
  // 第二遍对寄存器传递的变量进行压栈
  if ((FirstPass && !Args->PassByStack) ||
      (!FirstPass && Args->PassByStack))
    return;

  printLn("\n  # ↓对表达式进行计算，然后压栈↓");
  // 计算出表达式
  genExpr(Args);
  // 根据表达式结果的类型进行压栈
  switch (Args->Ty->Kind) {
  case TY_STRUCT:
  case TY_UNION:
    pushStruct(Args->Ty);
    break;
  case TY_FLOAT:
  case TY_DOUBLE:
    pushF();
    break;
  default:
    push();
  }
  printLn("  # ↑结束压栈↑");
}

// 处理参数后进行压栈
static int pushArgs(Node *Nd) {
  int Stack = 0, GP = 0, FP = 0;

  // 如果是超过16字节的结构体，则通过第一个寄存器传递结构体的指针
  if (Nd->RetBuffer && Nd->Ty->Size > 16)
    GP++;

  // 遍历所有参数，优先使用寄存器传递，然后是栈传递
  Type *CurArg = Nd->FuncType->Params;
  for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
    // 如果是可变参数的参数，只使用整型寄存器和栈传递
    if (Nd->FuncType->IsVariadic && CurArg == NULL) {
      int64_t Val = Arg->Val ? Arg->Val : Arg->FVal;
      if (GP < GP_MAX) {
        printLn("  # 可变参数%ld值通过a%d传递", Val, GP);
        GP++;
      } else {
        printLn("  # 可变参数%ld值通过栈传递", Val);
        Arg->PassByStack = true;
        Stack++;
      }
      continue;
    }

    // 遍历相应的实参，用于检查是不是到了可变参数
    CurArg = CurArg->Next;

    // 读取实参的类型
    Type *Ty = Arg->Ty;

    switch (Ty->Kind) {
    case TY_STRUCT:
    case TY_UNION: {
      // 判断结构体的类型
      setFloStMemsTy(&Ty, GP, FP);
      // 处理一或两个浮点成员变量的结构体
      if (isFloNum(Ty->FSReg1Ty) || isFloNum(Ty->FSReg2Ty)) {
        Type *Regs[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
        for (int I = 0; I < 2; ++I) {
          if (isFloNum(Regs[I]))
            FP++;
          if (isInteger(Regs[I]))
            GP++;
        }
        break;
      }

      // 9~16字节整型结构体用两个寄存器，其他字节结构体用一个寄存器
      int Regs = (8 < Ty->Size && Ty->Size <= 16) ? 2 : 1;
      for (int I = 1; I <= Regs; ++I) {
        if (GP < GP_MAX) {
          GP++;
        } else {
          Arg->PassByStack = true;
          Stack++;
        }
      }
      break;
    }
    case TY_FLOAT:
    case TY_DOUBLE:
      // 浮点优先使用FP，而后是GP，最后是栈传递
      if (FP < FP_MAX) {
        printLn("  # 浮点%f值通过fa%d传递", Arg->FVal, FP);
        FP++;
      } else if (GP < GP_MAX) {
        printLn("  # 浮点%f值通过a%d传递", Arg->FVal, GP);
        GP++;
      } else {
        printLn("  # 浮点%f值通过栈传递", Arg->FVal);
        Arg->PassByStack = true;
        Stack++;
      }
      break;
    default:
      // 整型优先使用GP，最后是栈传递
      if (GP < GP_MAX) {
        printLn("  # 整型%ld值通过a%d传递", Arg->Val, GP);
        GP++;
      } else {
        printLn("  # 整型%ld值通过栈传递", Arg->Val);
        Arg->PassByStack = true;
        Stack++;
      }
      break;
    }
  }

  // 对齐栈边界
  if ((Depth + Stack) % 2 == 1) {
    printLn("  # 对齐栈边界到16字节");
    printLn("  addi sp, sp, -8");
    Depth++;
    Stack++;
  }

  // 进行压栈
  // 开辟大于16字节的结构体的栈空间
  int BSStack = createBSSpace(Nd->Args);
  // 第一遍对栈传递的变量进行压栈
  pushArgs2(Nd->Args, true);
  // 第二遍对寄存器传递的变量进行压栈
  pushArgs2(Nd->Args, false);
  // 返回栈传递参数的个数

  if (Nd->RetBuffer && Nd->Ty->Size > 16) {
    printLn("  # 返回类型是大于16字节的结构体，指向其的指针，压入栈顶");
    printLn("  li t0, %d", Nd->RetBuffer->Offset);
    printLn("  add a0, fp, t0");
    push();
  }

  return Stack + BSStack;
}

// 复制结构体返回值到缓冲区中
static void copyRetBuffer(Obj *Var) {
  Type *Ty = Var->Ty;
  int GP = 0, FP = 0;

  setFloStMemsTy(&Ty, GP, FP);

  printLn("  # 拷贝到返回缓冲区");
  printLn("  # 加载struct地址到t0");
  printLn("  li t0, %d", Var->Offset);
  printLn("  add t1, fp, t0");

  // 处理浮点结构体的情况
  if (isFloNum(Ty->FSReg1Ty) || isFloNum(Ty->FSReg2Ty)) {
    int Off = 0;
    Type *RTys[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
    for (int I = 0; I < 2; ++I) {
      switch (RTys[I]->Kind) {
      case TY_FLOAT:
        printLn("  fsw fa%d, %d(t1)", FP++, Off);
        Off = 4;
        break;
      case TY_DOUBLE:
        printLn("  fsd fa%d, %d(t1)", FP++, Off);
        Off = 8;
        break;
      case TY_VOID:
        break;
      default:
        printLn("  sd a%d, %d(t1)", GP++, Off);
        Off = 8;
        break;
      }
    }
    return;
  }

  printLn("  # 复制整型结构体返回值到缓冲区中");
  for (int Off = 0; Off < Ty->Size; Off += 8) {
    switch (Ty->Size - Off) {
    case 1:
      printLn("  sb a%d, %d(t1)", GP++, Off);
      break;
    case 2:
      printLn("  sh a%d, %d(t1)", GP++, Off);
      break;
    case 3:
    case 4:
      printLn("  sw a%d, %d(t1)", GP++, Off);
      break;
    default:
      printLn("  sd a%d, %d(t1)", GP++, Off);
      break;
    }
  }
}

// 拷贝结构体的寄存器
static void copyStructReg(void) {
  Type *Ty = CurrentFn->Ty->ReturnTy;
  int GP = 0, FP = 0;

  printLn("  # 复制结构体寄存器");
  printLn("  # 读取寄存器，写入存有struct地址的0(t1)中");
  printLn("  mv t1, a0");

  setFloStMemsTy(&Ty, GP, FP);

  if (isFloNum(Ty->FSReg1Ty) || isFloNum(Ty->FSReg2Ty)) {
    int Off = 0;
    Type *RTys[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
    for (int I = 0; I < 2; ++I) {
      switch (RTys[I]->Kind) {
      case TY_FLOAT:
        printLn("  flw fa%d, %d(t1)", FP++, Off);
        Off = 4;
        break;
      case TY_DOUBLE:
        printLn("  fld fa%d, %d(t1)", FP++, Off);
        Off = 8;
        break;
      case TY_VOID:
        break;
      default:
        printLn("  ld a%d, %d(t1)", GP++, Off);
        Off = 8;
        break;
      }
    }
    return;
  }

  printLn("  # 复制返回的整型结构体的值");
  for (int Off = 0; Off < Ty->Size; Off += 8) {
    switch (Ty->Size - Off) {
    case 1:
      printLn("  lb a%d, %d(t1)", GP++, Off);
      break;
    case 2:
      printLn("  lh a%d, %d(t1)", GP++, Off);
      break;
    case 3:
    case 4:
      printLn("  lw a%d, %d(t1)", GP++, Off);
      break;
    default:
      printLn("  ld a%d, %d(t1)", GP++, Off);
      break;
    }
  }
}

// 大于16字节的结构体返回值，需要拷贝内存
static void copyStructMem(void) {
  Type *Ty = CurrentFn->Ty->ReturnTy;
  // 第一个参数，调用者的缓冲区指针
  Obj *Var = CurrentFn->Params;

  printLn("  # 复制大于16字节结构体内存");
  printLn("  # 将栈内struct地址存入t1，调用者的结构体的地址");
  printLn("  li t0, %d", Var->Offset);
  printLn("  add t0, fp, t0");
  printLn("  ld t1, 0(t0)");

  printLn("  # 遍历结构体并从a0位置复制所有字节到t1");
  for (int I = 0; I < Ty->Size; I++) {
    printLn("  lb t0, %d(a0)", I);
    printLn("  sb t0, %d(t1)", I);
  }
}

// 生成表达式
static void genExpr(Node *Nd) {
  // .loc 文件编号 行号
  printLn("  .loc %d %d", Nd->Tok->File->FileNo, Nd->Tok->LineNo);

  // 生成各个根节点
  switch (Nd->Kind) {
  // 空表达式
  case ND_NULL_EXPR:
    return;
  // 加载数字到a0

  // float和uint32、double和uint64 共用一份内存空间
  case ND_NUM: {
    union {
      float F32;
      double F64;
      uint32_t U32;
      uint64_t U64;
    } U;

    switch (Nd->Ty->Kind) {
    case TY_FLOAT:
      U.F32 = Nd->FVal;
      printLn("  # 将a0转换到float类型值为%f的fa0中", Nd->FVal);
      printLn("  li a0, %u  # float %f", U.U32, Nd->FVal);
      printLn("  fmv.w.x fa0, a0");
      return;
    case TY_DOUBLE:
      printLn("  # 将a0转换到double类型值为%f的fa0中", Nd->FVal);
      U.F64 = Nd->FVal;
      printLn("  li a0, %lu  # double %f", U.U64, Nd->FVal);
      printLn("  fmv.d.x fa0, a0");
      return;
    default:
      printLn("  # 将%ld加载到a0中", Nd->Val);
      printLn("  li a0, %ld", Nd->Val);
      return;
    }
  }
  // 对寄存器取反
  case ND_NEG:
    // 计算左部的表达式
    genExpr(Nd->LHS);

    switch (Nd->Ty->Kind) {
    case TY_FLOAT:
      printLn("  # 对float类型的fa0值进行取反");
      printLn("  fneg.s fa0, fa0");
      return;
    case TY_DOUBLE:
      printLn("  # 对double类型的fa0值进行取反");
      printLn("  fneg.d fa0, fa0");
      return;
    default:
      // neg a0, a0是sub a0, x0, a0的别名, 即a0=0-a0
      printLn("  # 对a0值进行取反");
      printLn("  neg%s a0, a0", Nd->Ty->Size <= 4 ? "w" : "");
      return;
    }
  // 变量
  case ND_VAR:
    // 计算出变量的地址，然后存入a0
    genAddr(Nd);
    load(Nd->Ty);
    return;
  // 成员变量
  case ND_MEMBER: {
    // 计算出成员变量的地址，然后存入a0
    genAddr(Nd);
    load(Nd->Ty);

    Member *Mem = Nd->Mem;
    if (Mem->IsBitfield) {
      printLn("  # 清除位域的成员变量（%d位）未用到的位", Mem->BitWidth);
      // 清除位域成员变量未用到的高位
      printLn("  slli a0, a0, %d", 64 - Mem->BitWidth - Mem->BitOffset);
      // 清除位域成员变量未用到的低位
      if (Mem->Ty->IsUnsigned)
        printLn("  srli a0, a0, %d", 64 - Mem->BitWidth);
      else
        printLn("  srai a0, a0, %d", 64 - Mem->BitWidth);
    }
    return;
  }
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

    // 如果是位域成员变量，需要先从内存中读取当前值，然后合并到新值中
    if (Nd->LHS->Kind == ND_MEMBER && Nd->LHS->Mem->IsBitfield) {
      printLn("\n  # 位域成员变量进行赋值↓");
      printLn("  # 备份需要赋的a0值");
      printLn("  mv t2, a0");

      printLn("  # 计算位域成员变量的新值：");
      Member *Mem = Nd->LHS->Mem;
      // 将需要赋的值a0存入t1
      printLn("  mv t1, a0");
      // 构造一个和位域成员长度相同，全为1的二进制数
      printLn("  li t0, %ld", (1L << Mem->BitWidth) - 1);
      // 取交之后，位域长度的低位，存储了我们需要的值，其他位都为0
      printLn("  and t1, t1, t0");
      // 然后将该值左移，相应的位偏移量中
      // 此时我们所需要的位域数值已经处于正确的位置，且其他位置都为0
      printLn("  slli t1, t1, %d", Mem->BitOffset);

      printLn("  # 读取位域当前值：");
      // 将位域值保存的地址加载进来
      printLn("  ld a0, 0(sp)");
      // 读取该地址的值
      load(Mem->Ty);

      printLn("  # 写入成员变量新值到位域当前值中：");
      // 位域值对应的掩码，即t1需要写入的位置
      // 掩码位都为1，其余位为0
      long Mask = ((1L << Mem->BitWidth) - 1) << Mem->BitOffset;
      // 对掩码取反，此时，其余位都为1，掩码位都为0
      printLn("  li t0, %ld", ~Mask);
      // 取交，保留除掩码位外所有的位
      printLn("  and a0, a0, t0");
      // 取或，将成员变量的新值写入到掩码位
      printLn("  or a0, a0, t1");

      store(Nd->Ty);
      printLn("  # 恢复需要赋的a0值作为返回值");
      printLn("  mv a0, t2");
      printLn("  # 完成位域成员变量的赋值↑\n");
      return;
    }

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
  // 内存清零
  case ND_MEMZERO: {
    printLn("  # 对%s的内存%d(fp)清零%d位", Nd->Var->Name, Nd->Var->Offset,
            Nd->Var->Ty->Size);
    // 对栈内变量所占用的每个字节都进行清零
    for (int I = 0; I < Nd->Var->Ty->Size; I++) {
      printLn("  li t0, %d", Nd->Var->Offset + I);
      printLn("  add t0, fp, t0");
      printLn("  sb zero, 0(t0)");
    }
    return;
  }
  // 条件运算符
  case ND_COND: {
    int C = count();
    printLn("\n# =====条件运算符%d===========", C);
    genExpr(Nd->Cond);
    notZero(Nd->Cond->Ty);
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
    notZero(Nd->LHS->Ty);
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
    notZero(Nd->LHS->Ty);
    printLn("  # 左部短路操作判断，为0则跳转");
    printLn("  beqz a0, .L.false.%d", C);
    genExpr(Nd->RHS);
    notZero(Nd->RHS->Ty);
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
    notZero(Nd->LHS->Ty);
    // 判断是否为短路操作
    printLn("  # 左部短路操作判断，不为0则跳转");
    printLn("  bnez a0, .L.true.%d", C);
    genExpr(Nd->RHS);
    notZero(Nd->RHS->Ty);
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
    // 计算所有参数的值，正向压栈
    // 此处获取到栈传递参数的数量
    int StackArgs = pushArgs(Nd);
    genExpr(Nd->LHS);
    // 将a0的值存入t5
    printLn("  mv t5, a0");

    // 反向弹栈，a0->参数1，a1->参数2……
    int GP = 0, FP = 0;

    if (Nd->RetBuffer && Nd->Ty->Size > 16) {
      printLn("  # 返回结构体大于16字节，那么第一个参数指向返回缓冲区");
      pop(GP++);
    }

    // 读取函数形参中的参数类型
    Type *CurArg = Nd->FuncType->Params;
    for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
      // 如果是可变参数函数
      // 匹配到空参数（最后一个）的时候，将剩余的整型寄存器弹栈
      if (Nd->FuncType->IsVariadic && CurArg == NULL) {
        if (GP < GP_MAX) {
          printLn("  # a%d传递可变实参", GP);
          pop(GP++);
        }
        continue;
      }

      CurArg = CurArg->Next;
      // 实参的类型
      Type *Ty = Arg->Ty;

      switch (Ty->Kind) {
      case TY_STRUCT:
      case TY_UNION: {
        // 判断结构体的类型
        // 结构体的大小
        int Sz = Ty->Size;

        // 处理一或两个浮点成员变量的结构体
        if (isFloNum(Ty->FSReg1Ty) || isFloNum(Ty->FSReg2Ty)) {
          Type *Regs[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
          for (int I = 0; I < 2; ++I) {
            if (Regs[I]->Kind == TY_FLOAT) {
              printLn("  # %d字节float结构体%d通过fa%d传递", Sz, I, FP);
              printLn("  # 弹栈，将栈顶的值存入fa%d", FP);
              printLn("  flw fa%d, 0(sp)", FP++);
              printLn("  addi sp, sp, 8");
              Depth--;
            }
            if (Regs[I]->Kind == TY_DOUBLE) {
              printLn("  # %d字节double结构体%d通过fa%d传递", Sz, I, FP);
              popF(FP++);
            }
            if (isInteger(Regs[I])) {
              printLn("  # %d字节浮点结构体%d通过a%d传递", Sz, I, GP);
              pop(GP++);
            }
          }
          break;
        }

        // 其他整型结构体或多字节结构体
        // 9~16字节整型结构体用两个寄存器，其他字节结构体用一个结构体
        int Regs = (8 < Sz && Sz <= 16) ? 2 : 1;
        for (int I = 1; I <= Regs; ++I) {
          if (GP < GP_MAX) {
            printLn("  # %d字节的整型结构体%d通过a%d传递", Sz, I, GP);
            pop(GP++);
          }
        }
        break;
      }
      case TY_FLOAT:
      case TY_DOUBLE:
        if (FP < FP_MAX) {
          printLn("  # fa%d传递浮点参数", FP);
          popF(FP++);
        } else if (GP < GP_MAX) {
          printLn("  # a%d传递浮点参数", GP);
          pop(GP++);
        }
        break;
      default:
        if (GP < GP_MAX) {
          printLn("  # a%d传递整型参数", GP);
          pop(GP++);
        }
        break;
      }
    }

    // 调用函数
    printLn("  # 调用函数");
    printLn("  jalr t5");

    // 回收为栈传递的变量开辟的栈空间
    if (StackArgs) {
      // 栈的深度减去栈传递参数的字节数
      Depth -= StackArgs;
      printLn("  # 回收栈传递参数的%d个字节", StackArgs * 8);
      printLn("  addi sp, sp, %d", StackArgs * 8);
      // 清除记录的大结构体的数量
      BSDepth = 0;
    }

    // 清除寄存器中高位无关的数据
    switch (Nd->Ty->Kind) {
    case TY_BOOL:
      printLn("  # 清除bool类型的高位");
      printLn("  slli a0, a0, 63");
      printLn("  srli a0, a0, 63");
      return;
    case TY_CHAR:
      printLn("  # 清除char类型的高位");
      if (Nd->Ty->IsUnsigned) {
        printLn("  slli a0, a0, 56");
        printLn("  srli a0, a0, 56");
      } else {
        printLn("  slli a0, a0, 56");
        printLn("  srai a0, a0, 56");
      }
      return;
    case TY_SHORT:
      printLn("  # 清除short类型的高位");
      if (Nd->Ty->IsUnsigned) {
        printLn("  slli a0, a0, 48");
        printLn("  srli a0, a0, 48");
      } else {
        printLn("  slli a0, a0, 48");
        printLn("  srai a0, a0, 48");
      }
      return;
    default:
      break;
    }

    // 如果返回的结构体小于16字节，直接使用寄存器返回
    if (Nd->RetBuffer && Nd->Ty->Size <= 16) {
      copyRetBuffer(Nd->RetBuffer);
      printLn("  li t0, %d", Nd->RetBuffer->Offset);
      printLn("  add a0, fp, t0");
    }

    return;
  }
  default:
    break;
  }

  // 处理浮点类型
  if (isFloNum(Nd->LHS->Ty)) {
    // 递归到最右节点
    genExpr(Nd->RHS);
    // 将结果压入栈
    pushF();
    // 递归到左节点
    genExpr(Nd->LHS);
    // 将结果弹栈到fa1
    popF(1);

    // 生成各个二叉树节点
    // float对应s(single)后缀，double对应d(double)后缀
    char *Suffix = (Nd->LHS->Ty->Kind == TY_FLOAT) ? "s" : "d";

    switch (Nd->Kind) {
    case ND_ADD:
      printLn("  # fa0+fa1，结果写入fa0");
      printLn("  fadd.%s fa0, fa0, fa1", Suffix);
      return;
    case ND_SUB:
      printLn("  # fa0-fa1，结果写入fa0");
      printLn("  fsub.%s fa0, fa0, fa1", Suffix);
      return;
    case ND_MUL:
      printLn("  # fa0×fa1，结果写入fa0");
      printLn("  fmul.%s fa0, fa0, fa1", Suffix);
      return;
    case ND_DIV:
      printLn("  # fa0÷fa1，结果写入fa0");
      printLn("  fdiv.%s fa0, fa0, fa1", Suffix);
      return;
    case ND_EQ:
      printLn("  # 判断是否fa0=fa1");
      printLn("  feq.%s a0, fa0, fa1", Suffix);
      return;
    case ND_NE:
      printLn("  # 判断是否fa0≠fa1");
      printLn("  feq.%s a0, fa0, fa1", Suffix);
      printLn("  seqz a0, a0");
      return;
    case ND_LT:
      printLn("  # 判断是否fa0<fa1");
      printLn("  flt.%s a0, fa0, fa1", Suffix);
      return;
    case ND_LE:
      printLn("  # 判断是否fa0≤fa1");
      printLn("  fle.%s a0, fa0, fa1", Suffix);
      return;
    default:
      errorTok(Nd->Tok, "invalid expression");
    }
  }

  // 递归到最右节点
  genExpr(Nd->RHS);
  // 将结果压入栈
  push();
  // 递归到左节点
  genExpr(Nd->LHS);
  // 将结果弹栈到a1
  pop(1);

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
    if (Nd->Ty->IsUnsigned)
      printLn("  divu%s a0, a0, a1", Suffix);
    else
      printLn("  div%s a0, a0, a1", Suffix);
    return;
  case ND_MOD: // % a0=a0%a1
    printLn("  # a0%%a1，结果写入a0");
    if (Nd->Ty->IsUnsigned)
      printLn("  remu%s a0, a0, a1", Suffix);
    else
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
    if (Nd->LHS->Ty->IsUnsigned && Nd->LHS->Ty->Kind == TY_INT) {
      printLn("  # 左部是U32类型，需要截断");
      printLn("slli a0, a0, 32");
      printLn("srli a0, a0, 32");
    };
    if (Nd->RHS->Ty->IsUnsigned && Nd->RHS->Ty->Kind == TY_INT) {
      printLn("  # 右部是U32类型，需要截断");
      printLn("slli a1, a1, 32");
      printLn("srli a1, a1, 32");
    };
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
    if (Nd->LHS->Ty->IsUnsigned)
      printLn("  sltu a0, a0, a1");
    else
      printLn("  slt a0, a0, a1");
    return;
  case ND_LE:
    // a0<=a1等价于
    // a0=a1<a0, a0=a0^1
    printLn("  # 判断是否a0≤a1");
    if (Nd->LHS->Ty->IsUnsigned)
      printLn("  sltu a0, a1, a0");
    else
      printLn("  slt a0, a1, a0");
    printLn("  xori a0, a0, 1");
    return;
  case ND_SHL:
    printLn("  # a0逻辑左移a1位");
    printLn("  sll%s a0, a0, a1", Suffix);
    return;
  case ND_SHR:
    printLn("  # a0算术右移a1位");
    if (Nd->Ty->IsUnsigned)
      printLn("  srl%s a0, a0, a1", Suffix);
    else
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
  printLn("  .loc %d %d", Nd->Tok->File->FileNo, Nd->Tok->LineNo);

  switch (Nd->Kind) {
  // 生成if语句
  case ND_IF: {
    // 代码段计数
    int C = count();
    printLn("\n# =====分支语句%d==============", C);
    // 生成条件内语句
    printLn("\n# Cond表达式%d", C);
    genExpr(Nd->Cond);
    notZero(Nd->Cond->Ty);
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
      notZero(Nd->Cond->Ty);
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
  // 生成do while语句
  case ND_DO: {
    int C = count();
    printLn("\n# =====do while语句%d============", C);
    printLn("\n# begin语句%d", C);
    printLn(".L.begin.%d:", C);

    printLn("\n# Then语句%d", C);
    genStmt(Nd->Then);

    printLn("\n# Cond语句%d", C);
    printLn("%s:", Nd->ContLabel);
    genExpr(Nd->Cond);

    notZero(Nd->Cond->Ty);
    printLn("  # 跳转到循环%d的.L.begin.%d段", C, C);
    printLn("  bnez a0, .L.begin.%d", C);

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
    // 不为空返回语句时
    if (Nd->LHS) {
      genExpr(Nd->LHS);

      Type *Ty = Nd->LHS->Ty;
      // 处理结构体作为返回值的情况
      if (Ty->Kind == TY_STRUCT || Ty->Kind == TY_UNION) {
        if (Ty->Size <= 16)
          // 小于16字节拷贝寄存器
          copyStructReg();
        else
          // 大于16字节拷贝内存
          copyStructMem();
      }
    }
    // 无条件跳转语句，跳转到.L.return段
    // j offset是 jal x0, offset的别名指令
    printLn("  # 跳转到.L.return.%s段", CurrentFn->Name);
    printLn("  j .L.return.%s", CurrentFn->Name);
    return;
  // 生成表达式语句
  case ND_EXPR_STMT:
    genExpr(Nd->LHS);
    return;
  case ND_ASM:
    printLn("  # 插入的ASM代码片段");
    printLn("  %s", Nd->AsmStr);
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

    // 反向偏移量
    int ReOffset = 16;

    // 被调用函数将自己的ra、fp也压入栈了，
    // 所以fp+16才是上一级函数的sp顶
    // /             栈保存的N个变量            / N*8
    // /---------------本级函数----------------/ sp
    // /                 ra                  / sp-8
    // /                fp（上一级）           / fp = sp-16

    // 寄存器传递
    int GP = 0, FP = 0;
    // 寄存器传递的参数
    for (Obj *Var = Fn->Params; Var; Var = Var->Next) {
      // 读取形参类型
      Type *Ty = Var->Ty;

      switch (Ty->Kind) {
      case TY_STRUCT:
      case TY_UNION:
        setFloStMemsTy(&Ty, GP, FP);

        // 计算浮点结构体所使用的寄存器
        // 这里一定寄存器可用，所以不判定是否超过寄存器最大值
        if (isFloNum(Ty->FSReg1Ty) || isFloNum(Ty->FSReg2Ty)) {
          Type *Regs[2] = {Ty->FSReg1Ty, Ty->FSReg2Ty};
          for (int I = 0; I < 2; ++I) {
            if (isFloNum(Regs[I]))
              FP++;
            if (isInteger(Regs[I]))
              GP++;
          }
          continue;
        }

        // 9～16字节的结构体要用两个寄存器
        if (8 < Ty->Size && Ty->Size <= 16) {
          // 如果只剩一个寄存器，那么剩余一半通过栈传递
          if (GP == GP_MAX - 1)
            Var->IsHalfByStack = true;
          if (GP < GP_MAX)
            GP++;
        }
        // 所有字节的结构体都在至少使用了一个寄存器（如果可用）
        if (GP < GP_MAX) {
          GP++;
          continue;
        }

        // 没使用寄存器的需要栈传递
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
        if (FP < FP_MAX) {
          printLn(" #  FP%d传递浮点变量%s", FP, Var->Name);
          FP++;
          continue;
        } else if (GP < GP_MAX) {
          printLn(" #  GP%d传递浮点变量%s", GP, Var->Name);
          GP++;
          continue;
        }
        break;
      default:
        if (GP < GP_MAX) {
          printLn(" #  GP%d传递整型变量%s", GP, Var->Name);
          GP++;
          continue;
        }
        break;
      }

      // 栈传递

      // 对齐变量
      ReOffset = alignTo(ReOffset, 8);
      // 为栈传递变量赋一个偏移量，或者说是反向栈地址
      Var->Offset = ReOffset;
      // 栈传递变量计算反向偏移量，传递一半的结构体减去寄存器的部分
      ReOffset += Var->IsHalfByStack ? Var->Ty->Size - 8 : Var->Ty->Size;
      printLn(" #  栈传递变量%s偏移量%d", Var->Name, Var->Offset);
    }

    // 可变参数函数VaArea的偏移量
    if (Fn->VaArea) {
      ReOffset = alignTo(ReOffset, 8);
      Fn->VaArea->Offset = ReOffset;
    }

    int Offset = 0;
    // 读取所有变量
    for (Obj *Var = Fn->Locals; Var; Var = Var->Next) {
      // 栈传递的变量的直接跳过
      if (Var->Offset && !Var->IsHalfByStack)
        continue;

      // 数组超过16字节时，对齐值至少为16字节
      int Align = (Var->Ty->Kind == TY_ARRAY && Var->Ty->Size >= 16)
                      ? MAX(16, Var->Align)
                      : Var->Align;

      // 每个变量分配空间
      Offset += Var->Ty->Size;
      // 对齐变量
      Offset = alignTo(Offset, Align);
      // 为每个变量赋一个偏移量，或者说是栈中地址
      Var->Offset = -Offset;
      printLn(" #  寄存器传递变量%s偏移量%d", Var->Name, Var->Offset);
    }
    // 将栈对齐到16字节
    Fn->StackSize = alignTo(Offset, 16);
  }
}

// 返回2^N的N值
static int simpleLog2(int Num) {
  int N = Num;
  int E = 0;
  while (N > 1) {
    if (N % 2 == 1)
      error("Wrong value %d", Num);
    N /= 2;
    ++E;
  }
  return E;
}

static void emitData(Obj *Prog) {
  for (Obj *Var = Prog; Var; Var = Var->Next) {
    // 跳过是函数或者无定义的变量
    if (Var->IsFunction || !Var->IsDefinition)
      continue;

    if (Var->IsStatic) {
      printLn("\n  # static全局变量%s", Var->Name);
      printLn("  .local %s", Var->Name);
    } else {
      printLn("\n  # 全局变量%s", Var->Name);
      printLn("  .globl %s", Var->Name);
    }

    printLn("  # 对齐全局变量");
    if (!Var->Align)
      error("Align can not be 0!");
    // 数组超过16字节时，对齐值至少为16字节
    int Align = (Var->Ty->Kind == TY_ARRAY && Var->Ty->Size >= 16)
                    ? MAX(16, Var->Align)
                    : Var->Align;
    printLn("  .align %d", simpleLog2(Align));

    // 为试探性的全局变量生成指示
    if (OptFCommon && Var->IsTentative) {
      printLn("  .comm %s, %d, %d", Var->Name, Var->Ty->Size, Align);
      continue;
    }

    // 判断是否有初始值
    // .data 或 .tdata 段
    if (Var->InitData) {
      if (Var->IsTLS) {
        printLn("\n  # TLS数据段标签");
        // a：可加载执行
        // w：可写
        // T：线程局部的
        // progbits：包含程序数据
        printLn("  .section .tdata,\"awT\",@progbits");
      } else {
        printLn("\n  # 数据段标签");
        printLn("  .data");
      }
      printLn("%s:", Var->Name);
      Relocation *Rel = Var->Rel;
      int Pos = 0;
      while (Pos < Var->Ty->Size) {
        if (Rel && Rel->Offset == Pos) {
          // 使用其他变量进行初始化
          printLn("  # %s全局变量", Var->Name);
          printLn("  .quad %s%+ld", Rel->Label, Rel->Addend);
          Rel = Rel->Next;
          Pos += 8;
        } else {
          // 打印出字符串的内容，包括转义字符
          char C = Var->InitData[Pos++];
          if (isprint(C))
            printLn("  .byte %d\t# 字符：%c", C, C);
          else
            printLn("  .byte %d", C);
        }
      }
      continue;
    }

    // bss段未给数据分配空间，只记录数据所需空间的大小
    // .bss 或 .tbss 段
    if (Var->IsTLS) {
      // nobits：不含数据
      printLn("\n  # TLS未初始化的全局变量");
      printLn("  .section .tbss,\"awT\",@nobits");
    } else {
      printLn("\n  # 未初始化的全局变量");
      printLn("  .bss");
    }
    printLn("%s:", Var->Name);
    printLn("  # 全局变量零填充%d字节", Var->Ty->Size);
    printLn("  .zero %d", Var->Ty->Size);
  }
}

// 将浮点寄存器的值存入栈中
static void storeFloat(int Reg, int Offset, int Sz) {
  printLn("  # 将fa%d寄存器的值存入%d(fp)的栈地址", Reg, Offset);
  printLn("  li t0, %d", Offset);
  printLn("  add t0, fp, t0");

  switch (Sz) {
  case 4:
    printLn("  fsw fa%d, 0(t0)", Reg);
    return;
  case 8:
    printLn("  fsd fa%d, 0(t0)", Reg);
    return;
  default:
    unreachable();
  }
}

// 将整形寄存器的值存入栈中
static void storeGeneral(int Reg, int Offset, int Size) {
  printLn("  # 将a%d寄存器的值存入%d(fp)的栈地址", Reg, Offset);
  printLn("  li t0, %d", Offset);
  printLn("  add t0, fp, t0");
  switch (Size) {
  case 1:
    printLn("  sb a%d, 0(t0)", Reg);
    return;
  case 2:
    printLn("  sh a%d, 0(t0)", Reg);
    return;
  case 4:
    printLn("  sw a%d, 0(t0)", Reg);
    return;
  case 8:
    printLn("  sd a%d, 0(t0)", Reg);
    return;
  }
  unreachable();
}

// 存储结构体到栈内开辟的空间
static void storeStruct(int Reg, int Offset, int Size) {
  // t0是结构体的地址，复制t0指向的结构体到栈相应的位置中
  for (int I = 0; I < Size; I++) {
    printLn("  lb t0, %d(a%d)", I, Reg);

    printLn("  li t1, %d", Offset + I);
    printLn("  add t1, fp, t1");
    printLn("  sb t0, 0(t1)");
  }
  return;
}

// 代码生成入口函数，包含代码块的基础信息
void emitText(Obj *Prog) {
  // 为每个函数单独生成代码
  for (Obj *Fn = Prog; Fn; Fn = Fn->Next) {
    if (!Fn->IsFunction || !Fn->IsDefinition)
      continue;

    // 如果未存活，就不生成static inline函数
    if (!Fn->IsLive)
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
    // ------------------------------//
    //        上一级函数的栈传递参数
    // ==============================// sp（本级函数）
    //         VaArea(寄存器可用时)
    // ------------------------------// sp = sp（本级函数）-VaArea
    //              ra
    //-------------------------------// ra = sp-8
    //              fp
    //-------------------------------// fp = sp-16
    //             变量
    //-------------------------------// sp = sp-16-StackSize
    //           表达式计算
    //-------------------------------//

    // Prologue, 前言

    // 为剩余的整型寄存器开辟空间，用于存储可变参数
    int VaSize = 0;
    if (Fn->VaArea) {
      // 遍历正常参数所使用的浮点、整型寄存器
      int GPs = 0, FPs = 0;

      // 可变参数函数，非可变的参数使用寄存器
      for (Obj *Var = Fn->Params; Var; Var = Var->Next) {
        // 计算所有类型所使用的寄存器数量
        Type *Ty = Var->Ty;
        switch (Ty->Kind) {
        case TY_STRUCT:
        case TY_UNION:
          // 对寄存器传递的参数
          if (isFloNum(Ty->FSReg1Ty) || isFloNum(Ty->FSReg2Ty)) {
            // 浮点结构体的第一部分
            isFloNum(Ty->FSReg1Ty) ? FPs++ : GPs++;
            // 浮点结构体的第二部分
            if (Ty->FSReg2Ty->Kind != TY_VOID)
              isFloNum(Ty->FSReg2Ty) ? FPs++ : GPs++;
            break;
          }

          // 小于8字节的结构体、大于16字节的结构体
          // 一半寄存器，一半栈传递的结构体
          if (Ty->Size < 8 || Ty->Size > 16 || Var->IsHalfByStack)
            GPs++;
          // 处理大于8字节，小于16字节的结构体
          else
            GPs += 2;
          break;
        case TY_FLOAT:
        case TY_DOUBLE:
          // 可变参数函数中的浮点参数
          FPs < FP_MAX ? FPs++ : GPs++;
          break;
        default:
          // 可变参数函数中的整型参数
          GPs++;
          break;
        }
      }

      // 需确保使用到了整形寄存器，否则不开辟此空间
      if (GPs < GP_MAX) {
        VaSize = (8 - GPs) * 8;
        printLn("  # VaArea的区域，大小为%d", VaSize);
        printLn("  addi sp, sp, -%d", VaSize);
      }
    }

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
    printLn("  li t0, -%d", Fn->StackSize);
    printLn("  add sp, sp, t0");

    // 正常传递的形参
    // 记录整型寄存器，浮点寄存器使用的数量
    int GP = 0, FP = 0;
    for (Obj *Var = Fn->Params; Var; Var = Var->Next) {
      // 不处理栈传递的形参，栈传递一半的结构体除外
      if (Var->Offset > 0 && !Var->IsHalfByStack)
        continue;

      Type *Ty = Var->Ty;

      // 正常传递的形参
      switch (Ty->Kind) {
      case TY_STRUCT:
      case TY_UNION:
        printLn("  # 对寄存器传递的结构体进行压栈");

        // 处理浮点结构体
        if (isFloNum(Ty->FSReg1Ty) || isFloNum(Ty->FSReg2Ty)) {
          printLn("  # 浮点结构体的第一部分进行压栈");
          // 浮点结构体的第一部分，偏移量为0
          int Sz1 = Var->Ty->FSReg1Ty->Size;
          if (isFloNum(Ty->FSReg1Ty))
            storeFloat(FP++, Var->Offset, Sz1);
          else
            storeGeneral(GP++, Var->Offset, Sz1);

          // 浮点结构体的第二部分
          if (Ty->FSReg2Ty->Kind != TY_VOID) {
            printLn("  # 浮点结构体的第二部分进行压栈");
            int Sz2 = Ty->FSReg2Ty->Size;
            // 结构体内偏移量为两个成员间的最大尺寸
            int Off = MAX(Sz1, Sz2);

            if (isFloNum(Ty->FSReg2Ty))
              storeFloat(FP++, Var->Offset + Off, Sz2);
            else
              storeGeneral(GP++, Var->Offset + Off, Sz2);
          }
          break;
        }

        // 大于16字节的结构体参数，通过访问它的地址，
        // 将原来位置的结构体复制到栈中
        if (Ty->Size > 16) {
          printLn("  # 大于16字节的结构体进行压栈");
          storeStruct(GP++, Var->Offset, Ty->Size);
          break;
        }

        // 一半寄存器、一半栈传递的结构体
        if (Var->IsHalfByStack) {
          printLn("  # 一半寄存器、一半栈传递结构体进行压栈");
          storeGeneral(GP++, Var->Offset, 8);
          // 拷贝栈传递的一半结构体到当前栈中
          for (int I = 0; I != Var->Ty->Size - 8; ++I) {
            printLn("  lb t0, %d(fp)", 16 + I);

            printLn("  li t1, %d", Var->Offset + 8 + I);
            printLn("  add t1, fp, t1");
            printLn("  sb t0, 0(t1)");
          }
          break;
        }

        // 处理小于16字节的结构体
        if (Ty->Size <= 16)
          storeGeneral(GP++, Var->Offset, MIN(8, Ty->Size));
        if (Ty->Size > 8)
          storeGeneral(GP++, Var->Offset + 8, Ty->Size - 8);
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
        // 正常传递的浮点形参
        if (FP < FP_MAX) {
          printLn("  # 将浮点形参%s的寄存器fa%d的值压栈", Var->Name, FP);
          storeFloat(FP++, Var->Offset, Var->Ty->Size);
        } else {
          printLn("  # 将浮点形参%s的寄存器a%d的值压栈", Var->Name, GP);
          storeGeneral(GP++, Var->Offset, Var->Ty->Size);
        }
        break;
      default:
        // 正常传递的整型形参
        printLn("  # 将整型形参%s的寄存器a%d的值压栈", Var->Name, GP);
        storeGeneral(GP++, Var->Offset, Var->Ty->Size);
        break;
      }
    }

    // 可变参数
    if (Fn->VaArea) {
      // 可变参数位置位于本函数的最上方，即sp的位置，也就是fp+16

      // 可变参数存入__va_area__，注意最多为7个
      int Offset = Fn->VaArea->Offset;
      printLn("  # 可变参数VaArea的偏移量为%d", Fn->VaArea->Offset);
      while (GP < GP_MAX) {
        printLn("  # 可变参数，相对%s的偏移量为%d", Fn->VaArea->Name,
                Offset - Fn->VaArea->Offset);
        storeGeneral(GP++, Offset, 8);
        Offset += 8;
      }
    }

    // 生成语句链表的代码
    printLn("# =====%s段主体===============", Fn->Name);
    genStmt(Fn->Body);
    assert(Depth == 0);

    // main默认返回0
    if (strcmp(Fn->Name, "main") == 0)
        printLn("  li a0, 0");

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

    // 归还可变参数寄存器压栈的那一部分
    if (Fn->VaArea && VaSize > 0) {
      printLn("  # 归还VaArea的区域，大小为%d", VaSize);
      printLn("  addi sp, sp, %d", VaSize);
    }

    // 返回
    printLn("  # 返回a0值给系统调用");
    printLn("  ret");
  }
}

void codegen(Obj *Prog, FILE *Out) {
  // 设置目标文件的文件流指针
  OutputFile = Out;

  // 获取所有的输入文件，并输出.file指示
  File **Files = getInputFiles();
  for (int I = 0; Files[I]; I++)
    printLn("  .file %d \"%s\"", Files[I]->FileNo, Files[I]->Name);

  // 计算局部变量的偏移量
  assignLVarOffsets(Prog);
  // 生成数据
  emitData(Prog);
  // 生成代码
  emitText(Prog);
}
