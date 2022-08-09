#include "rvcc.h"

// 在解析时，全部的变量实例都被累加到这个列表里。
Obj *Locals;

// program = "{" compoundStmt
// compoundStmt = (declaration | stmt)* "}"
// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
// declspec = "int"
// declarator = "*"* ident
// stmt = "return" expr ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
//        | "{" compoundStmt
//        | exprStmt
// exprStmt = expr? ";"
// expr = assign
// assign = equality ("=" assign)?
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-" | "*" | "&") unary | primary
// primary = "(" expr ")" | ident func-args? | num

// funcall = ident "(" (assign ("," assign)*)? ")"
static Node *compoundStmt(Token **Rest, Token *Tok);
static Node *declaration(Token **Rest, Token *Tok);
static Node *stmt(Token **Rest, Token *Tok);
static Node *exprStmt(Token **Rest, Token *Tok);
static Node *expr(Token **Rest, Token *Tok);
static Node *assign(Token **Rest, Token *Tok);
static Node *equality(Token **Rest, Token *Tok);
static Node *relational(Token **Rest, Token *Tok);
static Node *add(Token **Rest, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *unary(Token **Rest, Token *Tok);
static Node *primary(Token **Rest, Token *Tok);

// 通过名称，查找一个本地变量
static Obj *findVar(Token *Tok) {
  // 查找Locals变量中是否存在同名变量
  for (Obj *Var = Locals; Var; Var = Var->Next)
    // 判断变量名是否和终结符名长度一致，然后逐字比较。
    if (strlen(Var->Name) == Tok->Len &&
        !strncmp(Tok->Loc, Var->Name, Tok->Len))
      return Var;
  return NULL;
}

// 新建一个节点
static Node *newNode(NodeKind Kind, Token *Tok) {
  Node *Nd = calloc(1, sizeof(Node));
  Nd->Kind = Kind;
  Nd->Tok = Tok;
  return Nd;
}

// 新建一个单叉树
static Node *newUnary(NodeKind Kind, Node *Expr, Token *Tok) {
  Node *Nd = newNode(Kind, Tok);
  Nd->LHS = Expr;
  return Nd;
}

// 新建一个二叉树节点
static Node *newBinary(NodeKind Kind, Node *LHS, Node *RHS, Token *Tok) {
  Node *Nd = newNode(Kind, Tok);
  Nd->LHS = LHS;
  Nd->RHS = RHS;
  return Nd;
}

// 新建一个数字节点
static Node *newNum(int Val, Token *Tok) {
  Node *Nd = newNode(ND_NUM, Tok);
  Nd->Val = Val;
  return Nd;
}

// 新变量
static Node *newVarNode(Obj *Var, Token *Tok) {
  Node *Nd = newNode(ND_VAR, Tok);
  Nd->Var = Var;
  return Nd;
}

// 在链表中新增一个变量
static Obj *newLVar(char *Name, Type *Ty) {
  Obj *Var = calloc(1, sizeof(Obj));
  Var->Name = Name;
  Var->Ty = Ty;
  // 将变量插入头部
  Var->Next = Locals;
  Locals = Var;
  return Var;
}

// 获取标识符
static char *getIdent(Token *Tok) {
  if (Tok->Kind != TK_IDENT)
    errorTok(Tok, "expected an identifier");
  return strndup(Tok->Loc, Tok->Len);
}

// declspec = "int"
// declarator specifier
static Type *declspec(Token **Rest, Token *Tok) {
  *Rest = skip(Tok, "int");
  return TyInt;
}

// declarator = "*"* ident
static Type *declarator(Token **Rest, Token *Tok, Type *Ty) {
  // "*"*
  // 构建所有的（多重）指针
  while (consume(&Tok, Tok, "*"))
    Ty = pointerTo(Ty);

  if (Tok->Kind != TK_IDENT)
    errorTok(Tok, "expected a variable name");

  // ident
  // 变量名
  Ty->Name = Tok;
  *Rest = Tok->Next;
  return Ty;
}

// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node *declaration(Token **Rest, Token *Tok) {
  // declspec
  // 声明的 基础类型
  Type *Basety = declspec(&Tok, Tok);

  Node Head = {};
  Node *Cur = &Head;
  // 对变量声明次数计数
  int I = 0;

  // (declarator ("=" expr)? ("," declarator ("=" expr)?)*)?
  while (!equal(Tok, ";")) {
    // 第1个变量不必匹配 ","
    if (I++ > 0)
      Tok = skip(Tok, ",");

    // declarator
    // 声明获取到变量类型，包括变量名
    Type *Ty = declarator(&Tok, Tok, Basety);
    Obj *Var = newLVar(getIdent(Ty->Name), Ty);

    // 如果不存在"="则为变量声明，不需要生成节点，已经存储在Locals中了
    if (!equal(Tok, "="))
      continue;

    // 解析“=”后面的Token
    Node *LHS = newVarNode(Var, Ty->Name);
    // 解析递归赋值语句
    Node *RHS = assign(&Tok, Tok->Next);
    Node *Node = newBinary(ND_ASSIGN, LHS, RHS, Tok);
    // 存放在表达式语句中
    Cur->Next = newUnary(ND_EXPR_STMT, Node, Tok);
    Cur = Cur->Next;
  }

  // 将所有表达式语句，存放在代码块中
  Node *Nd = newNode(ND_BLOCK, Tok);
  Nd->Body = Head.Next;
  *Rest = Tok->Next;
  return Nd;
}

// 解析语句
// stmt = "return" expr ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
//        | "{" compoundStmt
//        | exprStmt
static Node *stmt(Token **Rest, Token *Tok) {
  // "return" expr ";"
  if (equal(Tok, "return")) {
    Node *Nd = newNode(ND_RETURN, Tok);
    Nd->LHS = expr(&Tok, Tok->Next);
    *Rest = skip(Tok, ";");
    return Nd;
  }

  // 解析if语句
  // "if" "(" expr ")" stmt ("else" stmt)?
  if (equal(Tok, "if")) {
    Node *Nd = newNode(ND_IF, Tok);
    // "(" expr ")"，条件内语句
    Tok = skip(Tok->Next, "(");
    Nd->Cond = expr(&Tok, Tok);
    Tok = skip(Tok, ")");
    // stmt，符合条件后的语句
    Nd->Then = stmt(&Tok, Tok);
    // ("else" stmt)?，不符合条件后的语句
    if (equal(Tok, "else"))
      Nd->Els = stmt(&Tok, Tok->Next);
    *Rest = Tok;
    return Nd;
  }

  // "for" "(" exprStmt expr? ";" expr? ")" stmt
  if (equal(Tok, "for")) {
    Node *Nd = newNode(ND_FOR, Tok);
    // "("
    Tok = skip(Tok->Next, "(");

    // exprStmt
    Nd->Init = exprStmt(&Tok, Tok);

    // expr?
    if (!equal(Tok, ";"))
      Nd->Cond = expr(&Tok, Tok);
    // ";"
    Tok = skip(Tok, ";");

    // expr?
    if (!equal(Tok, ")"))
      Nd->Inc = expr(&Tok, Tok);
    // ")"
    Tok = skip(Tok, ")");

    // stmt
    Nd->Then = stmt(Rest, Tok);
    return Nd;
  }

  // "while" "(" expr ")" stmt
  if (equal(Tok, "while")) {
    Node *Nd = newNode(ND_FOR, Tok);
    // "("
    Tok = skip(Tok->Next, "(");
    // expr
    Nd->Cond = expr(&Tok, Tok);
    // ")"
    Tok = skip(Tok, ")");
    // stmt
    Nd->Then = stmt(Rest, Tok);
    return Nd;
  }

  // "{" compoundStmt
  if (equal(Tok, "{"))
    return compoundStmt(Rest, Tok->Next);

  // exprStmt
  return exprStmt(Rest, Tok);
}

// 解析复合语句
// compoundStmt = (declaration | stmt)* "}"
static Node *compoundStmt(Token **Rest, Token *Tok) {
  Node *Nd = newNode(ND_BLOCK, Tok);

  // 这里使用了和词法分析类似的单向链表结构
  Node Head = {};
  Node *Cur = &Head;
  // (declaration | stmt)* "}"
  while (!equal(Tok, "}")) {
    // declaration
    if (equal(Tok, "int"))
      Cur->Next = declaration(&Tok, Tok);
    // stmt
    else
      Cur->Next = stmt(&Tok, Tok);
    Cur = Cur->Next;
    // 构造完AST后，为节点添加类型信息
    addType(Cur);
  }

  // Nd的Body存储了{}内解析的语句
  Nd->Body = Head.Next;
  *Rest = Tok->Next;
  return Nd;
}

// 解析表达式语句
// exprStmt = expr? ";"
static Node *exprStmt(Token **Rest, Token *Tok) {
  // ";"
  if (equal(Tok, ";")) {
    *Rest = Tok->Next;
    return newNode(ND_BLOCK, Tok);
  }

  // expr ";"
  Node *Nd = newNode(ND_EXPR_STMT, Tok);
  Nd->LHS = expr(&Tok, Tok);
  *Rest = skip(Tok, ";");
  return Nd;
}

// 解析表达式
// expr = assign
static Node *expr(Token **Rest, Token *Tok) { return assign(Rest, Tok); }

// 解析赋值
// assign = equality ("=" assign)?
static Node *assign(Token **Rest, Token *Tok) {
  // equality
  Node *Nd = equality(&Tok, Tok);

  // 可能存在递归赋值，如a=b=1
  // ("=" assign)?
  if (equal(Tok, "="))
    return Nd = newBinary(ND_ASSIGN, Nd, assign(Rest, Tok->Next), Tok);
  *Rest = Tok;
  return Nd;
}

// 解析相等性
// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **Rest, Token *Tok) {
  // relational
  Node *Nd = relational(&Tok, Tok);

  // ("==" relational | "!=" relational)*
  while (true) {
    Token *Start = Tok;

    // "==" relational
    if (equal(Tok, "==")) {
      Nd = newBinary(ND_EQ, Nd, relational(&Tok, Tok->Next), Start);
      continue;
    }

    // "!=" relational
    if (equal(Tok, "!=")) {
      Nd = newBinary(ND_NE, Nd, relational(&Tok, Tok->Next), Start);
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析比较关系
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static Node *relational(Token **Rest, Token *Tok) {
  // add
  Node *Nd = add(&Tok, Tok);

  // ("<" add | "<=" add | ">" add | ">=" add)*
  while (true) {
    Token *Start = Tok;

    // "<" add
    if (equal(Tok, "<")) {
      Nd = newBinary(ND_LT, Nd, add(&Tok, Tok->Next), Start);
      continue;
    }

    // "<=" add
    if (equal(Tok, "<=")) {
      Nd = newBinary(ND_LE, Nd, add(&Tok, Tok->Next), Start);
      continue;
    }

    // ">" add
    // X>Y等价于Y<X
    if (equal(Tok, ">")) {
      Nd = newBinary(ND_LT, add(&Tok, Tok->Next), Nd, Start);
      continue;
    }

    // ">=" add
    // X>=Y等价于Y<=X
    if (equal(Tok, ">=")) {
      Nd = newBinary(ND_LE, add(&Tok, Tok->Next), Nd, Start);
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析各种加法
static Node *newAdd(Node *LHS, Node *RHS, Token *Tok) {
  // 为左右部添加类型
  addType(LHS);
  addType(RHS);

  // num + num
  if (isInteger(LHS->Ty) && isInteger(RHS->Ty))
    return newBinary(ND_ADD, LHS, RHS, Tok);

  // 不能解析 ptr + ptr
  if (LHS->Ty->Base && RHS->Ty->Base)
    errorTok(Tok, "invalid operands");

  // 将 num + ptr 转换为 ptr + num
  if (!LHS->Ty->Base && RHS->Ty->Base) {
    Node *Tmp = LHS;
    LHS = RHS;
    RHS = Tmp;
  }

  // ptr + num
  // 指针加法，ptr+1，这里的1不是1个字节，而是1个元素的空间，所以需要 ×8 操作
  RHS = newBinary(ND_MUL, RHS, newNum(8, Tok), Tok);
  return newBinary(ND_ADD, LHS, RHS, Tok);
}

// 解析各种减法
static Node *newSub(Node *LHS, Node *RHS, Token *Tok) {
  // 为左右部添加类型
  addType(LHS);
  addType(RHS);

  // num - num
  if (isInteger(LHS->Ty) && isInteger(RHS->Ty))
    return newBinary(ND_SUB, LHS, RHS, Tok);

  // ptr - num
  if (LHS->Ty->Base && isInteger(RHS->Ty)) {
    RHS = newBinary(ND_MUL, RHS, newNum(8, Tok), Tok);
    addType(RHS);
    Node *Nd = newBinary(ND_SUB, LHS, RHS, Tok);
    // 节点类型为指针
    Nd->Ty = LHS->Ty;
    return Nd;
  }

  // ptr - ptr，返回两指针间有多少元素
  if (LHS->Ty->Base && RHS->Ty->Base) {
    Node *Nd = newBinary(ND_SUB, LHS, RHS, Tok);
    Nd->Ty = TyInt;
    return newBinary(ND_DIV, Nd, newNum(8, Tok), Tok);
  }

  errorTok(Tok, "invalid operands");
  return NULL;
}

// 解析加减
// add = mul ("+" mul | "-" mul)*
static Node *add(Token **Rest, Token *Tok) {
  // mul
  Node *Nd = mul(&Tok, Tok);

  // ("+" mul | "-" mul)*
  while (true) {
    Token *Start = Tok;

    // "+" mul
    if (equal(Tok, "+")) {
      Nd = newAdd(Nd, mul(&Tok, Tok->Next), Start);
      continue;
    }

    // "-" mul
    if (equal(Tok, "-")) {
      Nd = newSub(Nd, mul(&Tok, Tok->Next), Start);
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析乘除
// mul = unary ("*" unary | "/" unary)*
static Node *mul(Token **Rest, Token *Tok) {
  // unary
  Node *Nd = unary(&Tok, Tok);

  // ("*" unary | "/" unary)*
  while (true) {
    Token *Start = Tok;

    // "*" unary
    if (equal(Tok, "*")) {
      Nd = newBinary(ND_MUL, Nd, unary(&Tok, Tok->Next), Start);
      continue;
    }

    // "/" unary
    if (equal(Tok, "/")) {
      Nd = newBinary(ND_DIV, Nd, unary(&Tok, Tok->Next), Start);
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析一元运算
// unary = ("+" | "-" | "*" | "&") unary | primary
static Node *unary(Token **Rest, Token *Tok) {
  // "+" unary
  if (equal(Tok, "+"))
    return unary(Rest, Tok->Next);

  // "-" unary
  if (equal(Tok, "-"))
    return newUnary(ND_NEG, unary(Rest, Tok->Next), Tok);

  // "&" unary
  if (equal(Tok, "&"))
    return newUnary(ND_ADDR, unary(Rest, Tok->Next), Tok);

  // "*" unary
  if (equal(Tok, "*"))
    return newUnary(ND_DEREF, unary(Rest, Tok->Next), Tok);

  // primary
  return primary(Rest, Tok);
}

// 解析函数调用
// funcall = ident "(" (assign ("," assign)*)? ")"
static Node *funCall(Token **Rest, Token *Tok) {
  Token *Start = Tok;
  Tok = Tok->Next->Next;

  Node Head = {};
  Node *Cur = &Head;

  while (!equal(Tok, ")")) {
    if (Cur != &Head)
      Tok = skip(Tok, ",");
    // assign
    Cur->Next = assign(&Tok, Tok);
    Cur = Cur->Next;
  }

  *Rest = skip(Tok, ")");

  Node *Nd = newNode(ND_FUNCALL, Start);
  // ident
  Nd->FuncName = strndup(Start->Loc, Start->Len);
  Nd->Args = Head.Next;
  return Nd;
}

// 解析括号、数字、变量
// primary = "(" expr ")" | ident func-args? | num
static Node *primary(Token **Rest, Token *Tok) {
  // "(" expr ")"
  if (equal(Tok, "(")) {
    Node *Nd = expr(&Tok, Tok->Next);
    *Rest = skip(Tok, ")");
    return Nd;
  }

  // ident args?
  if (Tok->Kind == TK_IDENT) {
    // 函数调用
    if (equal(Tok->Next, "("))
      return funCall(Rest, Tok);

    // ident
    // 查找变量
    Obj *Var = findVar(Tok);
    // 如果变量不存在，就在链表中新增一个变量
    if (!Var)
      errorTok(Tok, "undefined variable");
    *Rest = Tok->Next;
    return newVarNode(Var, Tok);
  }

  // num
  if (Tok->Kind == TK_NUM) {
    Node *Nd = newNum(Tok->Val, Tok);
    *Rest = Tok->Next;
    return Nd;
  }

  errorTok(Tok, "expected an expression");
  return NULL;
}

// 语法解析入口函数
// program = "{" compoundStmt
Function *parse(Token *Tok) {
  // "{"
  Tok = skip(Tok, "{");

  // 函数体存储语句的AST，Locals存储变量
  Function *Prog = calloc(1, sizeof(Function));
  Prog->Body = compoundStmt(&Tok, Tok);
  Prog->Locals = Locals;
  return Prog;
}
