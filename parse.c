#include "rvcc.h"

// 在解析时，全部的变量实例都被累加到这个列表里。
Obj *Locals;

// program = "{" compoundStmt
// compoundStmt = stmt* "}"
// stmt = "return" expr ";" | "{" compoundStmt | exprStmt
// exprStmt = expr? ";"
// expr = assign
// assign = equality ("=" assign)?
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | ident | num
static Node *compoundStmt(Token **rest, Token *tok);
static Node *expr(Token **Rest, Token *Tok);
static Node *exprStmt(Token **Rest, Token *Tok);
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
static Node *newNode(NodeKind Kind) {
  Node *Nd = calloc(1, sizeof(Node));
  Nd->Kind = Kind;
  return Nd;
}

// 新建一个单叉树
static Node *newUnary(NodeKind Kind, Node *Expr) {
  Node *Nd = newNode(Kind);
  Nd->LHS = Expr;
  return Nd;
}

// 新建一个二叉树节点
static Node *newBinary(NodeKind Kind, Node *LHS, Node *RHS) {
  Node *Nd = newNode(Kind);
  Nd->LHS = LHS;
  Nd->RHS = RHS;
  return Nd;
}

// 新建一个数字节点
static Node *newNum(int Val) {
  Node *Nd = newNode(ND_NUM);
  Nd->Val = Val;
  return Nd;
}

// 新变量
static Node *newVarNode(Obj *Var) {
  Node *Nd = newNode(ND_VAR);
  Nd->Var = Var;
  return Nd;
}

// 在链表中新增一个变量
static Obj *newLVar(char *Name) {
  Obj *Var = calloc(1, sizeof(Obj));
  Var->Name = Name;
  // 将变量插入头部
  Var->Next = Locals;
  Locals = Var;
  return Var;
}

// 解析语句
// stmt = "return" expr ";" | "{" compoundStmt | exprStmt
static Node *stmt(Token **Rest, Token *Tok) {
  // "return" expr ";"
  if (equal(Tok, "return")) {
    Node *Nd = newUnary(ND_RETURN, expr(&Tok, Tok->Next));
    *Rest = skip(Tok, ";");
    return Nd;
  }

  // "{" compoundStmt
  if (equal(Tok, "{"))
    return compoundStmt(Rest, Tok->Next);

  // exprStmt
  return exprStmt(Rest, Tok);
}

// 解析复合语句
// compoundStmt = stmt* "}"
static Node *compoundStmt(Token **Rest, Token *Tok) {
  // 这里使用了和词法分析类似的单向链表结构
  Node Head = {};
  Node *Cur = &Head;
  // stmt* "}"
  while (!equal(Tok, "}")) {
    Cur->Next = stmt(&Tok, Tok);
    Cur = Cur->Next;
  }

  Node *Nd = newNode(ND_BLOCK);
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
    return newNode(ND_BLOCK);
  }

  // expr ";"
  Node *Nd = newUnary(ND_EXPR_STMT, expr(&Tok, Tok));
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
    Nd = newBinary(ND_ASSIGN, Nd, assign(&Tok, Tok->Next));
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
    // "==" relational
    if (equal(Tok, "==")) {
      Nd = newBinary(ND_EQ, Nd, relational(&Tok, Tok->Next));
      continue;
    }

    // "!=" relational
    if (equal(Tok, "!=")) {
      Nd = newBinary(ND_NE, Nd, relational(&Tok, Tok->Next));
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
    // "<" add
    if (equal(Tok, "<")) {
      Nd = newBinary(ND_LT, Nd, add(&Tok, Tok->Next));
      continue;
    }

    // "<=" add
    if (equal(Tok, "<=")) {
      Nd = newBinary(ND_LE, Nd, add(&Tok, Tok->Next));
      continue;
    }

    // ">" add
    // X>Y等价于Y<X
    if (equal(Tok, ">")) {
      Nd = newBinary(ND_LT, add(&Tok, Tok->Next), Nd);
      continue;
    }

    // ">=" add
    // X>=Y等价于Y<=X
    if (equal(Tok, ">=")) {
      Nd = newBinary(ND_LE, add(&Tok, Tok->Next), Nd);
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析加减
// add = mul ("+" mul | "-" mul)*
static Node *add(Token **Rest, Token *Tok) {
  // mul
  Node *Nd = mul(&Tok, Tok);

  // ("+" mul | "-" mul)*
  while (true) {
    // "+" mul
    if (equal(Tok, "+")) {
      Nd = newBinary(ND_ADD, Nd, mul(&Tok, Tok->Next));
      continue;
    }

    // "-" mul
    if (equal(Tok, "-")) {
      Nd = newBinary(ND_SUB, Nd, mul(&Tok, Tok->Next));
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
    // "*" unary
    if (equal(Tok, "*")) {
      Nd = newBinary(ND_MUL, Nd, unary(&Tok, Tok->Next));
      continue;
    }

    // "/" unary
    if (equal(Tok, "/")) {
      Nd = newBinary(ND_DIV, Nd, unary(&Tok, Tok->Next));
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析一元运算
// unary = ("+" | "-") unary | primary
static Node *unary(Token **Rest, Token *Tok) {
  // "+" unary
  if (equal(Tok, "+"))
    return unary(Rest, Tok->Next);

  // "-" unary
  if (equal(Tok, "-"))
    return newUnary(ND_NEG, unary(Rest, Tok->Next));

  // primary
  return primary(Rest, Tok);
}

// 解析括号、数字、变量
// primary = "(" expr ")" | ident｜ num
static Node *primary(Token **Rest, Token *Tok) {
  // "(" expr ")"
  if (equal(Tok, "(")) {
    Node *Nd = expr(&Tok, Tok->Next);
    *Rest = skip(Tok, ")");
    return Nd;
  }

  // ident
  if (Tok->Kind == TK_IDENT) {
    // 查找变量
    Obj *Var = findVar(Tok);
    // 如果变量不存在，就在链表中新增一个变量
    if (!Var)
      // strndup复制N个字符
      Var = newLVar(strndup(Tok->Loc, Tok->Len));
    *Rest = Tok->Next;
    return newVarNode(Var);
  }

  // num
  if (Tok->Kind == TK_NUM) {
    Node *Nd = newNum(Tok->Val);
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
