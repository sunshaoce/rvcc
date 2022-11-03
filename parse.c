#include "rvcc.h"

// 局部变量，全局变量，typedef，enum常量的域
typedef struct VarScope VarScope;
struct VarScope {
  VarScope *Next; // 下一变量域
  char *Name;     // 变量域名称
  Obj *Var;       // 对应的变量
  Type *Typedef;  // 别名
  Type *EnumTy;   // 枚举的类型
  int EnumVal;    // 枚举的值
};

// 结构体标签，联合体标签，枚举标签的域
typedef struct TagScope TagScope;
struct TagScope {
  TagScope *Next; // 下一标签域
  char *Name;     // 域名称
  Type *Ty;       // 域类型
};

// 表示一个块域
typedef struct Scope Scope;
struct Scope {
  Scope *Next; // 指向上一级的域

  // C有两个域：变量（或类型别名）域，结构体（或联合体，枚举）标签域
  VarScope *Vars; // 指向当前域内的变量
  TagScope *Tags; // 指向当前域内的结构体标签
};

// 变量属性
typedef struct {
  bool IsTypedef; // 是否为类型别名
  bool IsStatic;  // 是否为文件域内
  bool IsExtern;  // 是否为外部变量
  int Align;      // 对齐量
} VarAttr;

// 可变的初始化器。此处为树状结构。
// 因为初始化器可以是嵌套的，
// 类似于 int x[2][2] = {{1, 2}, {3, 4}} ，
typedef struct Initializer Initializer;
struct Initializer {
  Initializer *Next; // 下一个
  Type *Ty;          // 原始类型
  Token *Tok;        // 终结符
  bool IsFlexible;   // 可调整的，表示需要重新构造

  // 如果不是聚合类型，并且有一个初始化器，Expr 有对应的初始化表达式。
  Node *Expr;

  // 如果是聚合类型（如数组或结构体），Children有子节点的初始化器
  Initializer **Children;
};

// 指派初始化，用于局部变量的初始化器
typedef struct InitDesig InitDesig;
struct InitDesig {
  InitDesig *Next; // 下一个
  int Idx;         // 数组中的索引
  Member *Mem;     // 成员变量
  Obj *Var;        // 对应的变量
};

// 在解析时，全部的变量实例都被累加到这个列表里。
Obj *Locals;  // 局部变量
Obj *Globals; // 全局变量

// 所有的域的链表
static Scope *Scp = &(Scope){};

// 指向当前正在解析的函数
static Obj *CurrentFn;

// 当前函数内的goto和标签列表
static Node *Gotos;
static Node *Labels;

// 当前goto跳转的目标
static char *BrkLabel;
// 当前continue跳转的目标
static char *ContLabel;

// 如果我们正在解析switch语句，则指向表示switch的节点。
// 否则为空。
static Node *CurrentSwitch;

// program = (typedef | functionDefinition* | global-variable)*
// functionDefinition = declspec declarator "(" ")" "{" compoundStmt*
// declspec = ("void" | "_Bool" | char" | "short" | "int" | "long"
//             | "typedef" | "static" | "extern"
//             | "_Alignas" ("(" typeName | constExpr ")")
//             | "signed" | "unsigned"
//             | structDecl | unionDecl | typedefName
//             | enumSpecifier
//             | "const" | "volatile" | "auto" | "register" | "restrict"
//             | "__restrict" | "__restrict__" | "_Noreturn")+
// enumSpecifier = ident? "{" enumList? "}"
//                 | ident ("{" enumList? "}")?
// enumList = ident ("=" constExpr)? ("," ident ("=" constExpr)?)* ","?
// declarator = pointers ("(" ident ")" | "(" declarator ")" | ident) typeSuffix
// pointers = ("*" ("const" | "volatile" | "restrict")*)*
// typeSuffix = "(" funcParams | "[" arrayDimensions | ε
// arrayDimensions = ("static" | "restrict")* constExpr? "]" typeSuffix
// funcParams = ("void" | param ("," param)* ("," "...")?)? ")"
// param = declspec declarator

// compoundStmt = (typedef | declaration | stmt)* "}"
// declaration = declspec (declarator ("=" initializer)?
//                         ("," declarator ("=" initializer)?)*)? ";"
// initializer = stringInitializer | arrayInitializer | structInitializer
//             | unionInitializer |assign
// stringInitializer = stringLiteral

// arrayInitializer = arrayInitializer1 | arrayInitializer2
// arrayInitializer1 = "{" initializer ("," initializer)* ","? "}"
// arrayIntializer2 = initializer ("," initializer)* ","?

// structInitializer = structInitializer1 | structInitializer2
// structInitializer1 = "{" initializer ("," initializer)* ","? "}"
// structIntializer2 = initializer ("," initializer)* ","?

// unionInitializer = "{" initializer "}"
// stmt = "return" expr? ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | "switch" "(" expr ")" stmt
//        | "case" constExpr ":" stmt
//        | "default" ":" stmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
//        | "do" stmt "while" "(" expr ")" ";"
//        | "goto" ident ";"
//        | "break" ";"
//        | "continue" ";"
//        | ident ":" stmt
//        | "{" compoundStmt
//        | exprStmt
// exprStmt = expr? ";"
// expr = assign ("," expr)?
// assign = conditional (assignOp assign)?
// conditional = logOr ("?" expr ":" conditional)?
// logOr = logAnd ("||" logAnd)*
// logAnd = bitOr ("&&" bitOr)*
// bitOr = bitXor ("|" bitXor)*
// bitXor = bitAnd ("^" bitAnd)*
// bitAnd = equality ("&" equality)*
// assignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
//          | "<<=" | ">>="
// equality = relational ("==" relational | "!=" relational)*
// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
// shift = add ("<<" add | ">>" add)*
// add = mul ("+" mul | "-" mul)*
// mul = cast ("*" cast | "/" cast | "%" cast)*
// cast = "(" typeName ")" cast | unary
// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | postfix
// structMembers = (declspec declarator (","  declarator)* ";")*
// structDecl = structUnionDecl
// unionDecl = structUnionDecl
// structUnionDecl = ident? ("{" structMembers)?
// postfix = "(" typeName ")" "{" initializerList "}"
//         = ident "(" funcArgs ")" postfixTail*
//         | primary postfixTail*
//
// postfixTail = "[" expr "]"
//             | "(" funcArgs ")"
//             | "." ident
//             | "->" ident
//             | "++"
//             | "--"
// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" typeName ")"
//         | "sizeof" unary
//         | "_Alignof" "(" typeName ")"
//         | "_Alignof" unary
//         | ident
//         | str
//         | num
// typeName = declspec abstractDeclarator
// abstractDeclarator = pointers ("(" abstractDeclarator ")")? typeSuffix

// funcall = (assign ("," assign)*)? ")"
static bool isTypename(Token *Tok);
static Type *declspec(Token **Rest, Token *Tok, VarAttr *Attr);
static Type *typename(Token **Rest, Token *Tok);
static Type *enumSpecifier(Token **Rest, Token *Tok);
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty);
static Type *declarator(Token **Rest, Token *Tok, Type *Ty);
static Node *declaration(Token **Rest, Token *Tok, Type *BaseTy, VarAttr *Attr);
static void initializer2(Token **Rest, Token *Tok, Initializer *Init);
static Initializer *initializer(Token **Rest, Token *Tok, Type *Ty,
                                Type **NewTy);
static Node *LVarInitializer(Token **Rest, Token *Tok, Obj *Var);
static void GVarInitializer(Token **Rest, Token *Tok, Obj *Var);
static Node *compoundStmt(Token **Rest, Token *Tok);
static Node *stmt(Token **Rest, Token *Tok);
static Node *exprStmt(Token **Rest, Token *Tok);
static Node *expr(Token **Rest, Token *Tok);
static int64_t eval(Node *Nd);
static int64_t eval2(Node *Nd, char **Label);
static int64_t evalRVal(Node *Nd, char **Label);
static double evalDouble(Node *Nd);
static Node *assign(Token **Rest, Token *Tok);
static Node *conditional(Token **Rest, Token *Tok);
static Node *logOr(Token **Rest, Token *Tok);
static Node *logAnd(Token **Rest, Token *Tok);
static Node *bitOr(Token **Rest, Token *Tok);
static Node *bitXor(Token **Rest, Token *Tok);
static Node *bitAnd(Token **Rest, Token *Tok);
static Node *equality(Token **Rest, Token *Tok);
static Node *relational(Token **Rest, Token *Tok);
static Node *shift(Token **Rest, Token *Tok);
static Node *add(Token **Rest, Token *Tok);
static Node *newAdd(Node *LHS, Node *RHS, Token *Tok);
static Node *newSub(Node *LHS, Node *RHS, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *cast(Token **Rest, Token *Tok);
static Type *structDecl(Token **Rest, Token *Tok);
static Type *unionDecl(Token **Rest, Token *Tok);
static Node *unary(Token **Rest, Token *Tok);
static Node *postfix(Token **Rest, Token *Tok);
static Node *funCall(Token **Rest, Token *Tok, Node *Nd);
static Node *primary(Token **Rest, Token *Tok);
static Token *parseTypedef(Token *Tok, Type *BaseTy);
static bool isFunction(Token *Tok);
static Token *function(Token *Tok, Type *BaseTy, VarAttr *Attr);
static Token *globalVariable(Token *Tok, Type *Basety, VarAttr *Attr);

// 进入域
static void enterScope(void) {
  Scope *S = calloc(1, sizeof(Scope));
  // 后来的在链表头部
  // 类似于栈的结构，栈顶对应最近的域
  S->Next = Scp;
  Scp = S;
}

// 结束当前域
static void leaveScope(void) { Scp = Scp->Next; }

// 通过名称，查找一个变量
static VarScope *findVar(Token *Tok) {
  // 此处越先匹配的域，越深层
  for (Scope *S = Scp; S; S = S->Next)
    // 遍历域内的所有变量
    for (VarScope *S2 = S->Vars; S2; S2 = S2->Next)
      if (equal(Tok, S2->Name))
        return S2;
  return NULL;
}

// 通过Token查找标签
static Type *findTag(Token *Tok) {
  for (Scope *S = Scp; S; S = S->Next)
    for (TagScope *S2 = S->Tags; S2; S2 = S2->Next)
      if (equal(Tok, S2->Name))
        return S2->Ty;
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
static Node *newNum(int64_t Val, Token *Tok) {
  Node *Nd = newNode(ND_NUM, Tok);
  Nd->Val = Val;
  return Nd;
}

// 新建一个长整型节点
static Node *newLong(int64_t Val, Token *Tok) {
  Node *Nd = newNode(ND_NUM, Tok);
  Nd->Val = Val;
  Nd->Ty = TyLong;
  return Nd;
}

// 新建一个无符号长整型节点
static Node *newULong(long Val, Token *Tok) {
  Node *node = newNode(ND_NUM, Tok);
  node->Val = Val;
  node->Ty = TyULong;
  return node;
}

// 新变量
static Node *newVarNode(Obj *Var, Token *Tok) {
  Node *Nd = newNode(ND_VAR, Tok);
  Nd->Var = Var;
  return Nd;
}

// 新转换
Node *newCast(Node *Expr, Type *Ty) {
  addType(Expr);

  Node *Nd = calloc(1, sizeof(Node));
  Nd->Kind = ND_CAST;
  Nd->Tok = Expr->Tok;
  Nd->LHS = Expr;
  Nd->Ty = copyType(Ty);
  return Nd;
}

// 将变量存入当前的域中
static VarScope *pushScope(char *Name) {
  VarScope *S = calloc(1, sizeof(VarScope));
  S->Name = Name;
  // 后来的在链表头部
  S->Next = Scp->Vars;
  Scp->Vars = S;
  return S;
}

// 新建初始化器
static Initializer *newInitializer(Type *Ty, bool IsFlexible) {
  Initializer *Init = calloc(1, sizeof(Initializer));
  // 存储原始类型
  Init->Ty = Ty;

  // 处理数组类型
  if (Ty->Kind == TY_ARRAY) {
    // 判断是否需要调整数组元素数并且数组不完整
    if (IsFlexible && Ty->Size < 0) {
      // 设置初始化器为可调整的，之后进行完数组元素数的计算后，再构造初始化器
      Init->IsFlexible = true;
      return Init;
    }

    // 为数组的最外层的每个元素分配空间
    Init->Children = calloc(Ty->ArrayLen, sizeof(Initializer *));
    // 遍历解析数组最外层的每个元素
    for (int I = 0; I < Ty->ArrayLen; ++I)
      Init->Children[I] = newInitializer(Ty->Base, false);
  }

  // 处理结构体和联合体
  if (Ty->Kind == TY_STRUCT || Ty->Kind == TY_UNION) {
    // 计算结构体成员的数量
    int Len = 0;
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next)
      ++Len;

    // 初始化器的子项
    Init->Children = calloc(Len, sizeof(Initializer *));

    // 遍历子项进行赋值
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
      // 判断结构体是否是灵活的，同时成员也是灵活的并且是最后一个
      // 在这里直接构造，避免对于灵活数组的解析
      if (IsFlexible && Ty->IsFlexible && !Mem->Next) {
        Initializer *Child = calloc(1, sizeof(Initializer));
        Child->Ty = Mem->Ty;
        Child->IsFlexible = true;
        Init->Children[Mem->Idx] = Child;
      } else {
        // 对非灵活子项进行赋值
        Init->Children[Mem->Idx] = newInitializer(Mem->Ty, false);
      }
    }
    return Init;
  }

  return Init;
}

// 新建变量
static Obj *newVar(char *Name, Type *Ty) {
  Obj *Var = calloc(1, sizeof(Obj));
  Var->Name = Name;
  Var->Ty = Ty;
  // 设置变量默认的对齐量为类型的对齐量
  Var->Align = Ty->Align;
  pushScope(Name)->Var = Var;
  return Var;
}

// 在链表中新增一个局部变量
static Obj *newLVar(char *Name, Type *Ty) {
  Obj *Var = newVar(Name, Ty);
  Var->IsLocal = true;
  // 将变量插入头部
  Var->Next = Locals;
  Locals = Var;
  return Var;
}

// 在链表中新增一个全局变量
static Obj *newGVar(char *Name, Type *Ty) {
  Obj *Var = newVar(Name, Ty);
  Var->Next = Globals;
  // static全局变量
  Var->IsStatic = true;
  // 存在定义
  Var->IsDefinition = true;
  Globals = Var;
  return Var;
}

// 新增唯一名称
static char *newUniqueName(void) {
  static int Id = 0;
  return format(".L..%d", Id++);
}

// 新增匿名全局变量
static Obj *newAnonGVar(Type *Ty) { return newGVar(newUniqueName(), Ty); }

// 新增字符串字面量
static Obj *newStringLiteral(char *Str, Type *Ty) {
  Obj *Var = newAnonGVar(Ty);
  Var->InitData = Str;
  return Var;
}

// 获取标识符
static char *getIdent(Token *Tok) {
  if (Tok->Kind != TK_IDENT)
    errorTok(Tok, "expected an identifier");
  return strndup(Tok->Loc, Tok->Len);
}

// 查找类型别名
static Type *findTypedef(Token *Tok) {
  // 类型别名是个标识符
  if (Tok->Kind == TK_IDENT) {
    // 查找是否存在于变量域内
    VarScope *S = findVar(Tok);
    if (S)
      return S->Typedef;
  }
  return NULL;
}

static void pushTagScope(Token *Tok, Type *Ty) {
  TagScope *S = calloc(1, sizeof(TagScope));
  S->Name = strndup(Tok->Loc, Tok->Len);
  S->Ty = Ty;
  S->Next = Scp->Tags;
  Scp->Tags = S;
}

// declspec = ("void" | "_Bool" | char" | "short" | "int" | "long"
//             | "typedef" | "static" | "extern"
//             | "_Alignas" ("(" typeName | constExpr ")")
//             | "signed" | "unsigned"
//             | structDecl | unionDecl | typedefName
//             | enumSpecifier
//             | "const" | "volatile" | "auto" | "register" | "restrict"
//             | "__restrict" | "__restrict__" | "_Noreturn")+
// declarator specifier
static Type *declspec(Token **Rest, Token *Tok, VarAttr *Attr) {

  // 类型的组合，被表示为例如：LONG+LONG=1<<9
  // 可知long int和int long是等价的。
  enum {
    VOID = 1 << 0,
    BOOL = 1 << 2,
    CHAR = 1 << 4,
    SHORT = 1 << 6,
    INT = 1 << 8,
    LONG = 1 << 10,
    FLOAT = 1 << 12,
    DOUBLE = 1 << 14,
    OTHER = 1 << 16,
    SIGNED = 1 << 17,
    UNSIGNED = 1 << 18,
  };

  Type *Ty = TyInt;
  int Counter = 0; // 记录类型相加的数值

  // 遍历所有类型名的Tok
  while (isTypename(Tok)) {
    // 处理typedef等关键字
    if (equal(Tok, "typedef") || equal(Tok, "static") || equal(Tok, "extern")) {
      if (!Attr)
        errorTok(Tok, "storage class specifier is not allowed in this context");

      if (equal(Tok, "typedef"))
        Attr->IsTypedef = true;
      else if (equal(Tok, "static"))
        Attr->IsStatic = true;
      else
        Attr->IsExtern = true;

      // typedef不应与static/extern一起使用
      if (Attr->IsTypedef && (Attr->IsStatic || Attr->IsExtern))
        errorTok(Tok, "typedef and static/extern may not be used together");
      Tok = Tok->Next;
      continue;
    }

    // 识别这些关键字并忽略
    if (consume(&Tok, Tok, "const") || consume(&Tok, Tok, "volatile") ||
        consume(&Tok, Tok, "auto") || consume(&Tok, Tok, "register") ||
        consume(&Tok, Tok, "restrict") || consume(&Tok, Tok, "__restrict") ||
        consume(&Tok, Tok, "__restrict__") || consume(&Tok, Tok, "_Noreturn"))
      continue;

    // _Alignas "(" typeName | constExpr ")"
    if (equal(Tok, "_Alignas")) {
      // 不存在变量属性时，无法设置对齐值
      if (!Attr)
        errorTok(Tok, "_Alignas is not allowed in this context");
      Tok = skip(Tok->Next, "(");

      // 判断是类型名，或者常量表达式
      if (isTypename(Tok))
        Attr->Align = typename(&Tok, Tok)->Align;
      else
        Attr->Align = constExpr(&Tok, Tok);
      Tok = skip(Tok, ")");
      continue;
    }

    // 处理用户定义的类型
    Type *Ty2 = findTypedef(Tok);
    if (equal(Tok, "struct") || equal(Tok, "union") || equal(Tok, "enum") ||
        Ty2) {
      if (Counter)
        break;

      if (equal(Tok, "struct")) {
        Ty = structDecl(&Tok, Tok->Next);
      } else if (equal(Tok, "union")) {
        Ty = unionDecl(&Tok, Tok->Next);
      } else if (equal(Tok, "enum")) {
        Ty = enumSpecifier(&Tok, Tok->Next);
      } else {
        // 将类型设为类型别名指向的类型
        Ty = Ty2;
        Tok = Tok->Next;
      }

      Counter += OTHER;
      continue;
    }

    // 对于出现的类型名加入Counter
    // 每一步的Counter都需要有合法值
    if (equal(Tok, "void"))
      Counter += VOID;
    else if (equal(Tok, "_Bool"))
      Counter += BOOL;
    else if (equal(Tok, "char"))
      Counter += CHAR;
    else if (equal(Tok, "short"))
      Counter += SHORT;
    else if (equal(Tok, "int"))
      Counter += INT;
    else if (equal(Tok, "long"))
      Counter += LONG;
    else if (equal(Tok, "float"))
      Counter += FLOAT;
    else if (equal(Tok, "double"))
      Counter += DOUBLE;
    else if (equal(Tok, "signed"))
      Counter |= SIGNED;
    else if (equal(Tok, "unsigned"))
      Counter |= UNSIGNED;
    else
      unreachable();

    // 根据Counter值映射到对应的Type
    switch (Counter) {
    case VOID:
      Ty = TyVoid;
      break;
    case BOOL:
      Ty = TyBool;
      break;
    case SIGNED + CHAR:
      Ty = TyChar;
      break;
    // RISCV当中char是无符号类型的
    case CHAR:
    case UNSIGNED + CHAR:
      Ty = TyUChar;
      break;
    case SHORT:
    case SHORT + INT:
    case SIGNED + SHORT:
    case SIGNED + SHORT + INT:
      Ty = TyShort;
      break;
    case UNSIGNED + SHORT:
    case UNSIGNED + SHORT + INT:
      Ty = TyUShort;
      break;
    case INT:
    case SIGNED:
    case SIGNED + INT:
      Ty = TyInt;
      break;
    case UNSIGNED:
    case UNSIGNED + INT:
      Ty = TyUInt;
      break;
    case LONG:
    case LONG + INT:
    case LONG + LONG:
    case LONG + LONG + INT:
    case SIGNED + LONG:
    case SIGNED + LONG + INT:
    case SIGNED + LONG + LONG:
    case SIGNED + LONG + LONG + INT:
      Ty = TyLong;
      break;
    case UNSIGNED + LONG:
    case UNSIGNED + LONG + INT:
    case UNSIGNED + LONG + LONG:
    case UNSIGNED + LONG + LONG + INT:
      Ty = TyULong;
      break;
    case FLOAT:
      Ty = TyFloat;
      break;
    case DOUBLE:
    case LONG + DOUBLE:
      Ty = TyDouble;
      break;
    default:
      errorTok(Tok, "invalid type");
    }

    Tok = Tok->Next;
  } // while (isTypename(Tok))

  *Rest = Tok;
  return Ty;
}

// funcParams = ("void" | param ("," param)* ("," "...")?)? ")"
// param = declspec declarator
static Type *funcParams(Token **Rest, Token *Tok, Type *Ty) {
  // "void"
  if (equal(Tok, "void") && equal(Tok->Next, ")")) {
    *Rest = Tok->Next->Next;
    return funcType(Ty);
  }

  Type Head = {};
  Type *Cur = &Head;
  bool IsVariadic = false;

  while (!equal(Tok, ")")) {
    // funcParams = param ("," param)*
    // param = declspec declarator
    if (Cur != &Head)
      Tok = skip(Tok, ",");

    // ("," "...")?
    if (equal(Tok, "...")) {
      IsVariadic = true;
      Tok = Tok->Next;
      skip(Tok, ")");
      break;
    }

    Type *Ty2 = declspec(&Tok, Tok, NULL);
    Ty2 = declarator(&Tok, Tok, Ty2);

    // 存储名称
    Token *Name = Ty2->Name;

    // T类型的数组或函数被转换为T*
    if (Ty2->Kind == TY_ARRAY) {
      Ty2 = pointerTo(Ty2->Base);
      Ty2->Name = Name;
    } else if (Ty2->Kind == TY_FUNC) {
      Ty2 = pointerTo(Ty2);
      Ty2->Name = Name;
    }

    // 将类型复制到形参链表一份
    Cur->Next = copyType(Ty2);
    Cur = Cur->Next;
  }

  // 设置空参函数调用为可变的
  if (Cur == &Head)
    IsVariadic = true;

  // 封装一个函数节点
  Ty = funcType(Ty);
  // 传递形参
  Ty->Params = Head.Next;
  // 传递可变参数
  Ty->IsVariadic = IsVariadic;
  *Rest = Tok->Next;
  return Ty;
}

// 数组维数
// arrayDimensions = ("static" | "restrict")* constExpr? "]" typeSuffix
static Type *arrayDimensions(Token **Rest, Token *Tok, Type *Ty) {
  // ("static" | "restrict")*
  while (equal(Tok, "static") || equal(Tok, "restrict"))
    Tok = Tok->Next;

  // "]" 无数组维数的 "[]"
  if (equal(Tok, "]")) {
    Ty = typeSuffix(Rest, Tok->Next, Ty);
    return arrayOf(Ty, -1);
  }

  // 有数组维数的情况
  int Sz = constExpr(&Tok, Tok);
  Tok = skip(Tok, "]");
  Ty = typeSuffix(Rest, Tok, Ty);
  return arrayOf(Ty, Sz);
}

// typeSuffix = "(" funcParams | "[" arrayDimensions | ε
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty) {
  // "(" funcParams
  if (equal(Tok, "("))
    return funcParams(Rest, Tok->Next, Ty);

  // "[" arrayDimensions
  if (equal(Tok, "["))
    return arrayDimensions(Rest, Tok->Next, Ty);

  *Rest = Tok;
  return Ty;
}

// pointers = ("*" ("const" | "volatile" | "restrict")*)*
static Type *pointers(Token **Rest, Token *Tok, Type *Ty) {
  // "*"*
  // 构建所有的（多重）指针
  while (consume(&Tok, Tok, "*")) {
    Ty = pointerTo(Ty);
    // 识别这些关键字并忽略
    while (equal(Tok, "const") || equal(Tok, "volatile") ||
           equal(Tok, "restrict") || equal(Tok, "__restrict") ||
           equal(Tok, "__restrict__"))
      Tok = Tok->Next;
  }
  *Rest = Tok;
  return Ty;
}

// declarator = pointers ("(" ident ")" | "(" declarator ")" | ident) typeSuffix
static Type *declarator(Token **Rest, Token *Tok, Type *Ty) {
  // pointers
  Ty = pointers(&Tok, Tok, Ty);

  // "(" declarator ")"
  if (equal(Tok, "(")) {
    // 记录"("的位置
    Token *Start = Tok;
    Type Dummy = {};
    // 使Tok前进到")"后面的位置
    declarator(&Tok, Start->Next, &Dummy);
    Tok = skip(Tok, ")");
    // 获取到括号后面的类型后缀，Ty为解析完的类型，Rest指向分号
    Ty = typeSuffix(Rest, Tok, Ty);
    // 解析Ty整体作为Base去构造，返回Type的值
    return declarator(&Tok, Start->Next, Ty);
  }

  // 默认名称为空
  Token *Name = NULL;
  // 名称位置指向类型后的区域
  Token *NamePos = Tok;

  // 存在名字则赋值
  if (Tok->Kind == TK_IDENT) {
    Name = Tok;
    Tok = Tok->Next;
  }

  // typeSuffix
  Ty = typeSuffix(Rest, Tok, Ty);
  // ident
  // 变量名 或 函数名
  Ty->Name = Name;
  Ty->NamePos = NamePos;
  return Ty;
}

// abstractDeclarator = pointers ("(" abstractDeclarator ")")? typeSuffix
static Type *abstractDeclarator(Token **Rest, Token *Tok, Type *Ty) {
  // pointers
  Ty = pointers(&Tok, Tok, Ty);

  // ("(" abstractDeclarator ")")?
  if (equal(Tok, "(")) {
    Token *Start = Tok;
    Type Dummy = {};
    // 使Tok前进到")"后面的位置
    abstractDeclarator(&Tok, Start->Next, &Dummy);
    Tok = skip(Tok, ")");
    // 获取到括号后面的类型后缀，Ty为解析完的类型，Rest指向分号
    Ty = typeSuffix(Rest, Tok, Ty);
    // 解析Ty整体作为Base去构造，返回Type的值
    return abstractDeclarator(&Tok, Start->Next, Ty);
  }

  // typeSuffix
  return typeSuffix(Rest, Tok, Ty);
}

// typeName = declspec abstractDeclarator
// 获取类型的相关信息
static Type *typename(Token **Rest, Token *Tok) {
  // declspec
  Type *Ty = declspec(&Tok, Tok, NULL);
  // abstractDeclarator
  return abstractDeclarator(Rest, Tok, Ty);
}

// 判断是否终结符匹配到了结尾
static bool isEnd(Token *Tok) {
  // "}" | ",}"
  return equal(Tok, "}") || (equal(Tok, ",") && equal(Tok->Next, "}"));
}

// 消耗掉结尾的终结符
// "}" | ",}"
static bool consumeEnd(Token **Rest, Token *Tok) {
  // "}"
  if (equal(Tok, "}")) {
    *Rest = Tok->Next;
    return true;
  }

  // ",}"
  if (equal(Tok, ",") && equal(Tok->Next, "}")) {
    *Rest = Tok->Next->Next;
    return true;
  }

  // 没有消耗到指定字符
  return false;
}

// 获取枚举类型信息
// enumSpecifier = ident? "{" enumList? "}"
//               | ident ("{" enumList? "}")?
// enumList      = ident ("=" constExpr)? ("," ident ("=" constExpr)?)* ","?
static Type *enumSpecifier(Token **Rest, Token *Tok) {
  Type *Ty = enumType();

  // 读取标签
  // ident?
  Token *Tag = NULL;
  if (Tok->Kind == TK_IDENT) {
    Tag = Tok;
    Tok = Tok->Next;
  }

  // 处理没有{}的情况
  if (Tag && !equal(Tok, "{")) {
    Type *Ty = findTag(Tag);
    if (!Ty)
      errorTok(Tag, "unknown enum type");
    if (Ty->Kind != TY_ENUM)
      errorTok(Tag, "not an enum tag");
    *Rest = Tok;
    return Ty;
  }

  // "{" enumList? "}"
  Tok = skip(Tok, "{");

  // enumList
  // 读取枚举列表
  int I = 0;   // 第几个枚举常量
  int Val = 0; // 枚举常量的值
  while (!consumeEnd(Rest, Tok)) {
    if (I++ > 0)
      Tok = skip(Tok, ",");

    char *Name = getIdent(Tok);
    Tok = Tok->Next;

    // 判断是否存在赋值
    if (equal(Tok, "="))
      Val = constExpr(&Tok, Tok->Next);

    // 存入枚举常量
    VarScope *S = pushScope(Name);
    S->EnumTy = Ty;
    S->EnumVal = Val++;
  }

  if (Tag)
    pushTagScope(Tag, Ty);
  return Ty;
}

// declaration = declspec (declarator ("=" initializer)?
//                         ("," declarator ("=" initializer)?)*)? ";"
static Node *declaration(Token **Rest, Token *Tok, Type *BaseTy,
                         VarAttr *Attr) {
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
    Type *Ty = declarator(&Tok, Tok, BaseTy);
    if (Ty->Kind == TY_VOID)
      errorTok(Tok, "variable declared void");
    if (!Ty->Name)
      errorTok(Ty->NamePos, "variable name omitted");

    if (Attr && Attr->IsStatic) {
      // 静态局部变量
      Obj *Var = newAnonGVar(Ty);
      pushScope(getIdent(Ty->Name))->Var = Var;
      if (equal(Tok, "="))
        GVarInitializer(&Tok, Tok->Next, Var);
      continue;
    }

    Obj *Var = newLVar(getIdent(Ty->Name), Ty);
    // 读取是否存在变量的对齐值
    if (Attr && Attr->Align)
      Var->Align = Attr->Align;

    // 如果不存在"="则为变量声明，不需要生成节点，已经存储在Locals中了
    if (equal(Tok, "=")) {
      // 解析变量的初始化器
      Node *Expr = LVarInitializer(&Tok, Tok->Next, Var);
      // 存放在表达式语句中
      Cur->Next = newUnary(ND_EXPR_STMT, Expr, Tok);
      Cur = Cur->Next;
    }

    if (Var->Ty->Size < 0)
      errorTok(Ty->Name, "variable has incomplete type");
    if (Var->Ty->Kind == TY_VOID)
      errorTok(Ty->Name, "variable declared void");
  }

  // 将所有表达式语句，存放在代码块中
  Node *Nd = newNode(ND_BLOCK, Tok);
  Nd->Body = Head.Next;
  *Rest = Tok->Next;
  return Nd;
}

// 跳过多余的元素
static Token *skipExcessElement(Token *Tok) {
  if (equal(Tok, "{")) {
    Tok = skipExcessElement(Tok->Next);
    return skip(Tok, "}");
  }

  // 解析并舍弃多余的元素
  assign(&Tok, Tok);
  return Tok;
}

// stringInitializer = stringLiteral
static void stringInitializer(Token **Rest, Token *Tok, Initializer *Init) {
  // 如果是可调整的，就构造一个包含数组的初始化器
  // 字符串字面量在词法解析部分已经增加了'\0'
  if (Init->IsFlexible)
    *Init = *newInitializer(arrayOf(Init->Ty->Base, Tok->Ty->ArrayLen), false);

  // 取数组和字符串的最短长度
  int Len = MIN(Init->Ty->ArrayLen, Tok->Ty->ArrayLen);
  // 遍历赋值
  for (int I = 0; I < Len; I++)
    Init->Children[I]->Expr = newNum(Tok->Str[I], Tok);
  *Rest = Tok->Next;
}

// 计算数组初始化元素个数
static int countArrayInitElements(Token *Tok, Type *Ty) {
  Initializer *Dummy = newInitializer(Ty->Base, false);
  // 项数
  int I = 0;

  // 遍历所有匹配的项
  for (; !consumeEnd(&Tok, Tok); I++) {
    if (I > 0)
      Tok = skip(Tok, ",");
    initializer2(&Tok, Tok, Dummy);
  }
  return I;
}

// arrayInitializer1 = "{" initializer ("," initializer)* ","? "}"
static void arrayInitializer1(Token **Rest, Token *Tok, Initializer *Init) {
  Tok = skip(Tok, "{");

  // 如果数组是可调整的，那么就计算数组的元素数，然后进行初始化器的构造
  if (Init->IsFlexible) {
    int Len = countArrayInitElements(Tok, Init->Ty);
    // 在这里Ty也被重新构造为了数组
    *Init = *newInitializer(arrayOf(Init->Ty->Base, Len), false);
  }

  // 遍历数组
  for (int I = 0; !consumeEnd(Rest, Tok); I++) {
    if (I > 0)
      Tok = skip(Tok, ",");

    // 正常解析元素
    if (I < Init->Ty->ArrayLen)
      initializer2(&Tok, Tok, Init->Children[I]);
    // 跳过多余的元素
    else
      Tok = skipExcessElement(Tok);
  }
}

// arrayIntializer2 = initializer ("," initializer)* ","?
static void arrayInitializer2(Token **Rest, Token *Tok, Initializer *Init) {
  // 如果数组是可调整的，那么就计算数组的元素数，然后进行初始化器的构造
  if (Init->IsFlexible) {
    int Len = countArrayInitElements(Tok, Init->Ty);
    *Init = *newInitializer(arrayOf(Init->Ty->Base, Len), false);
  }

  // 遍历数组
  for (int I = 0; I < Init->Ty->ArrayLen && !isEnd(Tok); I++) {
    if (I > 0)
      Tok = skip(Tok, ",");
    initializer2(&Tok, Tok, Init->Children[I]);
  }
  *Rest = Tok;
}

// structInitializer1 = "{" initializer ("," initializer)* ","? "}"
static void structInitializer1(Token **Rest, Token *Tok, Initializer *Init) {
  Tok = skip(Tok, "{");

  // 成员变量的链表
  Member *Mem = Init->Ty->Mems;

  while (!consumeEnd(Rest, Tok)) {
    // Mem未指向Init->Ty->Mems，则说明Mem进行过Next的操作，就不是第一个
    if (Mem != Init->Ty->Mems)
      Tok = skip(Tok, ",");

    if (Mem) {
      // 处理成员
      initializer2(&Tok, Tok, Init->Children[Mem->Idx]);
      Mem = Mem->Next;
    } else {
      // 处理多余的成员
      Tok = skipExcessElement(Tok);
    }
  }
}

// structIntializer2 = initializer ("," initializer)* ","?
static void structInitializer2(Token **Rest, Token *Tok, Initializer *Init) {
  bool First = true;

  // 遍历所有成员变量
  for (Member *Mem = Init->Ty->Mems; Mem && !isEnd(Tok); Mem = Mem->Next) {
    if (!First)
      Tok = skip(Tok, ",");
    First = false;
    initializer2(&Tok, Tok, Init->Children[Mem->Idx]);
  }
  *Rest = Tok;
}

// unionInitializer = "{" initializer "}"
static void unionInitializer(Token **Rest, Token *Tok, Initializer *Init) {
  // 联合体只接受第一个成员用来初始化
  if (equal(Tok, "{")) {
    // 存在括号的情况
    initializer2(&Tok, Tok->Next, Init->Children[0]);
    // ","?
    consume(&Tok, Tok, ",");
    *Rest = skip(Tok, "}");
  } else {
    // 不存在括号的情况
    initializer2(Rest, Tok, Init->Children[0]);
  }
}

// initializer = stringInitializer | arrayInitializer | structInitializer
//             | unionInitializer |assign
static void initializer2(Token **Rest, Token *Tok, Initializer *Init) {
  // 字符串字面量的初始化
  if (Init->Ty->Kind == TY_ARRAY && Tok->Kind == TK_STR) {
    stringInitializer(Rest, Tok, Init);
    return;
  }

  // 数组的初始化
  if (Init->Ty->Kind == TY_ARRAY) {
    if (equal(Tok, "{"))
      // 存在括号的情况
      arrayInitializer1(Rest, Tok, Init);
    else
      // 不存在括号的情况
      arrayInitializer2(Rest, Tok, Init);
    return;
  }

  // 结构体的初始化
  if (Init->Ty->Kind == TY_STRUCT) {
    // 匹配使用其他结构体来赋值，其他结构体需要先被解析过
    // 存在括号的情况
    if (equal(Tok, "{")) {
      structInitializer1(Rest, Tok, Init);
      return;
    }

    // 不存在括号的情况
    Node *Expr = assign(Rest, Tok);
    addType(Expr);
    if (Expr->Ty->Kind == TY_STRUCT) {
      Init->Expr = Expr;
      return;
    }

    structInitializer2(Rest, Tok, Init);
    return;
  }

  // 联合体的初始化
  if (Init->Ty->Kind == TY_UNION) {
    unionInitializer(Rest, Tok, Init);
    return;
  }

  // 处理标量外的大括号，例如：int x = {3};
  if (equal(Tok, "{")) {
    initializer2(&Tok, Tok->Next, Init);
    *Rest = skip(Tok, "}");
    return;
  }

  // assign
  // 为节点存储对应的表达式
  Init->Expr = assign(Rest, Tok);
}

// 复制结构体的类型
static Type *copyStructType(Type *Ty) {
  // 复制结构体的类型
  Ty = copyType(Ty);

  // 复制结构体成员的类型
  Member Head = {};
  Member *Cur = &Head;
  // 遍历成员
  for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
    Member *M = calloc(1, sizeof(Member));
    *M = *Mem;
    Cur->Next = M;
    Cur = Cur->Next;
  }

  Ty->Mems = Head.Next;
  return Ty;
}

// 初始化器
static Initializer *initializer(Token **Rest, Token *Tok, Type *Ty,
                                Type **NewTy) {
  // 新建一个解析了类型的初始化器
  Initializer *Init = newInitializer(Ty, true);
  // 解析需要赋值到Init中
  initializer2(Rest, Tok, Init);

  if ((Ty->Kind == TY_STRUCT || Ty->Kind == TY_UNION) && Ty->IsFlexible) {
    // 复制结构体类型
    Ty = copyStructType(Ty);

    Member *Mem = Ty->Mems;
    // 遍历到最后一个成员
    while (Mem->Next)
      Mem = Mem->Next;
    // 灵活数组类型替换为实际的数组类型
    Mem->Ty = Init->Children[Mem->Idx]->Ty;
    // 增加结构体的类型大小
    Ty->Size += Mem->Ty->Size;

    // 将新类型传回变量
    *NewTy = Ty;
    return Init;
  }

  // 将新类型传回变量
  *NewTy = Init->Ty;
  return Init;
}

// 指派初始化表达式
static Node *initDesigExpr(InitDesig *Desig, Token *Tok) {
  // 返回Desig中的变量
  if (Desig->Var)
    return newVarNode(Desig->Var, Tok);

  // 返回Desig中的成员变量
  if (Desig->Mem) {
    Node *Nd = newUnary(ND_MEMBER, initDesigExpr(Desig->Next, Tok), Tok);
    Nd->Mem = Desig->Mem;
    return Nd;
  }

  // 需要赋值的变量名
  // 递归到次外层Desig，有此时最外层有Desig->Var或者Desig->Mem
  // 然后逐层计算偏移量
  Node *LHS = initDesigExpr(Desig->Next, Tok);
  // 偏移量
  Node *RHS = newNum(Desig->Idx, Tok);
  // 返回偏移后的变量地址
  return newUnary(ND_DEREF, newAdd(LHS, RHS, Tok), Tok);
}

// 创建局部变量的初始化
static Node *createLVarInit(Initializer *Init, Type *Ty, InitDesig *Desig,
                            Token *Tok) {
  if (Ty->Kind == TY_ARRAY) {
    // 预备空表达式的情况
    Node *Nd = newNode(ND_NULL_EXPR, Tok);
    for (int I = 0; I < Ty->ArrayLen; I++) {
      // 这里next指向了上一级Desig的信息，以及在其中的偏移量。
      InitDesig Desig2 = {Desig, I};
      // 局部变量进行初始化
      Node *RHS = createLVarInit(Init->Children[I], Ty->Base, &Desig2, Tok);
      // 构造一个形如：NULL_EXPR，EXPR1，EXPR2…的二叉树
      Nd = newBinary(ND_COMMA, Nd, RHS, Tok);
    }
    return Nd;
  }

  // 被其他结构体赋过值，则会存在Expr因而不解析
  if (Ty->Kind == TY_STRUCT && !Init->Expr) {
    // 构造结构体的初始化器结构
    Node *Nd = newNode(ND_NULL_EXPR, Tok);

    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
      // Desig2存储了成员变量
      InitDesig Desig2 = {Desig, 0, Mem};
      Node *RHS =
          createLVarInit(Init->Children[Mem->Idx], Mem->Ty, &Desig2, Tok);
      Nd = newBinary(ND_COMMA, Nd, RHS, Tok);
    }
    return Nd;
  }

  if (Ty->Kind == TY_UNION) {
    // Desig2存储了成员变量
    InitDesig Desig2 = {Desig, 0, Ty->Mems};
    // 只处理第一个成员变量
    return createLVarInit(Init->Children[0], Ty->Mems->Ty, &Desig2, Tok);
  }

  // 如果需要作为右值的表达式为空，则设为空表达式
  if (!Init->Expr)
    return newNode(ND_NULL_EXPR, Tok);

  // 变量等可以直接赋值的左值
  Node *LHS = initDesigExpr(Desig, Tok);
  return newBinary(ND_ASSIGN, LHS, Init->Expr, Tok);
}

// 局部变量初始化器
static Node *LVarInitializer(Token **Rest, Token *Tok, Obj *Var) {
  // 获取初始化器，将值与数据结构一一对应
  Initializer *Init = initializer(Rest, Tok, Var->Ty, &Var->Ty);
  // 指派初始化
  InitDesig Desig = {NULL, 0, NULL, Var};

  // 我们首先为所有元素赋0，然后有指定值的再进行赋值
  Node *LHS = newNode(ND_MEMZERO, Tok);
  LHS->Var = Var;

  // 创建局部变量的初始化
  Node *RHS = createLVarInit(Init, Var->Ty, &Desig, Tok);
  // 左部为全部清零，右部为需要赋值的部分
  return newBinary(ND_COMMA, LHS, RHS, Tok);
}

// 临时转换Buf类型对Val进行存储
static void writeBuf(char *Buf, uint64_t Val, int Sz) {
  if (Sz == 1)
    *Buf = Val;
  else if (Sz == 2)
    *(uint16_t *)Buf = Val;
  else if (Sz == 4)
    *(uint32_t *)Buf = Val;
  else if (Sz == 8)
    *(uint64_t *)Buf = Val;
  else
    unreachable();
}

// 对全局变量的初始化器写入数据
static Relocation *writeGVarData(Relocation *Cur, Initializer *Init, Type *Ty,
                                 char *Buf, int Offset) {
  // 处理数组
  if (Ty->Kind == TY_ARRAY) {
    int Sz = Ty->Base->Size;
    for (int I = 0; I < Ty->ArrayLen; I++)
      Cur =
          writeGVarData(Cur, Init->Children[I], Ty->Base, Buf, Offset + Sz * I);
    return Cur;
  }

  // 处理结构体
  if (Ty->Kind == TY_STRUCT) {
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next)
      Cur = writeGVarData(Cur, Init->Children[Mem->Idx], Mem->Ty, Buf,
                          Offset + Mem->Offset);
    return Cur;
  }

  // 处理联合体
  if (Ty->Kind == TY_UNION) {
    return writeGVarData(Cur, Init->Children[0], Ty->Mems->Ty, Buf, Offset);
  }

  // 这里返回，则会使Buf值为0
  if (!Init->Expr)
    return Cur;

  // 处理单精度浮点数
  if (Ty->Kind == TY_FLOAT) {
    // 将缓冲区加上偏移量转换为float*后访问
    *(float *)(Buf + Offset) = evalDouble(Init->Expr);
    return Cur;
  }

  // 处理双精度浮点数
  if (Ty->Kind == TY_DOUBLE) {
    // 将缓冲区加上偏移量转换为double*后访问
    *(double *)(Buf + Offset) = evalDouble(Init->Expr);
    return Cur;
  }

  // 预设使用到的 其他全局变量的名称
  char *Label = NULL;
  uint64_t Val = eval2(Init->Expr, &Label);

  // 如果不存在Label，说明可以直接计算常量表达式的值
  if (!Label) {
    writeBuf(Buf + Offset, Val, Ty->Size);
    return Cur;
  }

  // 存在Label，则表示使用了其他全局变量
  Relocation *Rel = calloc(1, sizeof(Relocation));
  Rel->Offset = Offset;
  Rel->Label = Label;
  Rel->Addend = Val;
  // 压入链表顶部
  Cur->Next = Rel;
  return Cur->Next;
}

// 全局变量在编译时需计算出初始化的值，然后写入.data段。
static void GVarInitializer(Token **Rest, Token *Tok, Obj *Var) {
  // 获取到初始化器
  Initializer *Init = initializer(Rest, Tok, Var->Ty, &Var->Ty);

  // 写入计算过后的数据
  // 新建一个重定向的链表
  Relocation Head = {};
  char *Buf = calloc(1, Var->Ty->Size);
  writeGVarData(&Head, Init, Var->Ty, Buf, 0);
  // 全局变量的数据
  Var->InitData = Buf;
  // Head为空，所以返回Head.Next的数据
  Var->Rel = Head.Next;
}

// 判断是否为类型名
static bool isTypename(Token *Tok) {
  static char *Kw[] = {
      "void",       "_Bool",        "char",      "short",    "int",
      "long",       "struct",       "union",     "typedef",  "enum",
      "static",     "extern",       "_Alignas",  "signed",   "unsigned",
      "const",      "volatile",     "auto",      "register", "restrict",
      "__restrict", "__restrict__", "_Noreturn", "float",    "double",
  };

  for (int I = 0; I < sizeof(Kw) / sizeof(*Kw); ++I) {
    if (equal(Tok, Kw[I]))
      return true;
  }
  // 查找是否为类型别名
  return findTypedef(Tok);
}

// 解析语句
// stmt = "return" expr? ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | "switch" "(" expr ")" stmt
//        | "case" constExpr ":" stmt
//        | "default" ":" stmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
//        | "do" stmt "while" "(" expr ")" ";"
//        | "goto" ident ";"
//        | "break" ";"
//        | "continue" ";"
//        | ident ":" stmt
//        | "{" compoundStmt
//        | exprStmt
static Node *stmt(Token **Rest, Token *Tok) {
  // "return" expr ";"
  if (equal(Tok, "return")) {
    Node *Nd = newNode(ND_RETURN, Tok);

    // 空返回语句
    if (consume(Rest, Tok->Next, ";"))
      return Nd;

    Node *Exp = expr(&Tok, Tok->Next);
    *Rest = skip(Tok, ";");

    addType(Exp);
    // 对于返回值进行类型转换
    Nd->LHS = newCast(Exp, CurrentFn->Ty->ReturnTy);
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

  // "switch" "(" expr ")" stmt
  if (equal(Tok, "switch")) {
    Node *Nd = newNode(ND_SWITCH, Tok);
    Tok = skip(Tok->Next, "(");
    Nd->Cond = expr(&Tok, Tok);
    Tok = skip(Tok, ")");

    // 记录此前的CurrentSwitch
    Node *Sw = CurrentSwitch;
    // 设置当前的CurrentSwitch
    CurrentSwitch = Nd;

    // 存储此前break标签的名称
    char *Brk = BrkLabel;
    // 设置break标签的名称
    BrkLabel = Nd->BrkLabel = newUniqueName();

    // 进入解析各个case
    // stmt
    Nd->Then = stmt(Rest, Tok);

    // 恢复此前CurrentSwitch
    CurrentSwitch = Sw;
    // 恢复此前break标签的名称
    BrkLabel = Brk;
    return Nd;
  }

  // "case" constExpr ":" stmt
  if (equal(Tok, "case")) {
    if (!CurrentSwitch)
      errorTok(Tok, "stray case");

    Node *Nd = newNode(ND_CASE, Tok);
    // case后面的数值
    int Val = constExpr(&Tok, Tok->Next);
    Tok = skip(Tok, ":");
    Nd->Label = newUniqueName();
    // case中的语句
    Nd->LHS = stmt(Rest, Tok);
    // case对应的数值
    Nd->Val = Val;
    // 将旧的CurrentSwitch链表的头部存入Nd的CaseNext
    Nd->CaseNext = CurrentSwitch->CaseNext;
    // 将Nd存入CurrentSwitch的CaseNext
    CurrentSwitch->CaseNext = Nd;
    return Nd;
  }

  // "default" ":" stmt
  if (equal(Tok, "default")) {
    if (!CurrentSwitch)
      errorTok(Tok, "stray default");

    Node *Nd = newNode(ND_CASE, Tok);
    Tok = skip(Tok->Next, ":");
    Nd->Label = newUniqueName();
    Nd->LHS = stmt(Rest, Tok);
    // 存入CurrentSwitch->DefaultCase的默认标签
    CurrentSwitch->DefaultCase = Nd;
    return Nd;
  }

  // "for" "(" exprStmt expr? ";" expr? ")" stmt
  if (equal(Tok, "for")) {
    Node *Nd = newNode(ND_FOR, Tok);
    // "("
    Tok = skip(Tok->Next, "(");

    // 进入for循环域
    enterScope();

    // 存储此前break和continue标签的名称
    char *Brk = BrkLabel;
    char *Cont = ContLabel;
    // 设置break和continue标签的名称
    BrkLabel = Nd->BrkLabel = newUniqueName();
    ContLabel = Nd->ContLabel = newUniqueName();

    // exprStmt
    if (isTypename(Tok)) {
      // 初始化循环变量
      Type *BaseTy = declspec(&Tok, Tok, NULL);
      Nd->Init = declaration(&Tok, Tok, BaseTy, NULL);
    } else {
      // 初始化语句
      Nd->Init = exprStmt(&Tok, Tok);
    }

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
    // 退出for循环域
    leaveScope();
    // 恢复此前的break和continue标签
    BrkLabel = Brk;
    ContLabel = Cont;
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

    // 存储此前break和continue标签的名称
    char *Brk = BrkLabel;
    char *Cont = ContLabel;
    // 设置break和continue标签的名称
    BrkLabel = Nd->BrkLabel = newUniqueName();
    ContLabel = Nd->ContLabel = newUniqueName();
    // stmt
    Nd->Then = stmt(Rest, Tok);
    // 恢复此前的break和continue标签
    BrkLabel = Brk;
    ContLabel = Cont;
    return Nd;
  }

  // "goto" ident ";"
  if (equal(Tok, "goto")) {
    Node *Nd = newNode(ND_GOTO, Tok);
    Nd->Label = getIdent(Tok->Next);
    // 将Nd同时存入Gotos，最后用于解析UniqueLabel
    Nd->GotoNext = Gotos;
    Gotos = Nd;
    *Rest = skip(Tok->Next->Next, ";");
    return Nd;
  }

  // "do" stmt "while" "(" expr ")" ";"
  if (equal(Tok, "do")) {
    Node *Nd = newNode(ND_DO, Tok);

    // 存储此前break和continue标签的名称
    char *Brk = BrkLabel;
    char *Cont = ContLabel;
    // 设置break和continue标签的名称
    BrkLabel = Nd->BrkLabel = newUniqueName();
    ContLabel = Nd->ContLabel = newUniqueName();

    // stmt
    // do代码块内的语句
    Nd->Then = stmt(&Tok, Tok->Next);

    // 恢复此前的break和continue标签
    BrkLabel = Brk;
    ContLabel = Cont;

    // "while" "(" expr ")" ";"
    Tok = skip(Tok, "while");
    Tok = skip(Tok, "(");
    // expr
    // while使用的条件表达式
    Nd->Cond = expr(&Tok, Tok);
    Tok = skip(Tok, ")");
    *Rest = skip(Tok, ";");
    return Nd;
  }

  // "break" ";"
  if (equal(Tok, "break")) {
    if (!BrkLabel)
      errorTok(Tok, "stray break");
    // 跳转到break标签的位置
    Node *Nd = newNode(ND_GOTO, Tok);
    Nd->UniqueLabel = BrkLabel;
    *Rest = skip(Tok->Next, ";");
    return Nd;
  }

  // "continue" ";"
  if (equal(Tok, "continue")) {
    if (!ContLabel)
      errorTok(Tok, "stray continue");
    // 跳转到continue标签的位置
    Node *Nd = newNode(ND_GOTO, Tok);
    Nd->UniqueLabel = ContLabel;
    *Rest = skip(Tok->Next, ";");
    return Nd;
  }

  // ident ":" stmt
  if (Tok->Kind == TK_IDENT && equal(Tok->Next, ":")) {
    Node *Nd = newNode(ND_LABEL, Tok);
    Nd->Label = strndup(Tok->Loc, Tok->Len);
    Nd->UniqueLabel = newUniqueName();
    Nd->LHS = stmt(Rest, Tok->Next->Next);
    // 将Nd同时存入Labels，最后用于goto解析UniqueLabel
    Nd->GotoNext = Labels;
    Labels = Nd;
    return Nd;
  }

  // "{" compoundStmt
  if (equal(Tok, "{"))
    return compoundStmt(Rest, Tok->Next);

  // exprStmt
  return exprStmt(Rest, Tok);
}

// 解析复合语句
// compoundStmt = (typedef | declaration | stmt)* "}"
static Node *compoundStmt(Token **Rest, Token *Tok) {
  Node *Nd = newNode(ND_BLOCK, Tok);

  // 这里使用了和词法分析类似的单向链表结构
  Node Head = {};
  Node *Cur = &Head;

  // 进入新的域
  enterScope();

  // (declaration | stmt)* "}"
  while (!equal(Tok, "}")) {
    // declaration
    if (isTypename(Tok) && !equal(Tok->Next, ":")) {
      VarAttr Attr = {};
      Type *BaseTy = declspec(&Tok, Tok, &Attr);

      // 解析typedef的语句
      if (Attr.IsTypedef) {
        Tok = parseTypedef(Tok, BaseTy);
        continue;
      }

      // 解析函数
      if (isFunction(Tok)) {
        Tok = function(Tok, BaseTy, &Attr);
        continue;
      }

      // 解析外部全局变量
      if (Attr.IsExtern) {
        Tok = globalVariable(Tok, BaseTy, &Attr);
        continue;
      }

      // 解析变量声明语句
      Cur->Next = declaration(&Tok, Tok, BaseTy, &Attr);
    }
    // stmt
    else {
      Cur->Next = stmt(&Tok, Tok);
    }
    Cur = Cur->Next;
    // 构造完AST后，为节点添加类型信息
    addType(Cur);
  }

  // 结束当前的域
  leaveScope();

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
// expr = assign ("," expr)?
static Node *expr(Token **Rest, Token *Tok) {
  Node *Nd = assign(&Tok, Tok);

  if (equal(Tok, ","))
    return newBinary(ND_COMMA, Nd, expr(Rest, Tok->Next), Tok);

  *Rest = Tok;
  return Nd;
}

static int64_t eval(Node *Nd) { return eval2(Nd, NULL); }

// 计算给定节点的常量表达式计算
// 常量表达式可以是数字或者是 ptr±n，ptr是指向全局变量的指针，n是偏移量。
static int64_t eval2(Node *Nd, char **Label) {
  addType(Nd);

  // 处理浮点数
  if (isFloNum(Nd->Ty))
    return evalDouble(Nd);

  switch (Nd->Kind) {
  case ND_ADD:
    return eval2(Nd->LHS, Label) + eval(Nd->RHS);
  case ND_SUB:
    return eval2(Nd->LHS, Label) - eval(Nd->RHS);
  case ND_MUL:
    return eval(Nd->LHS) * eval(Nd->RHS);
  case ND_DIV:
    if (Nd->Ty->IsUnsigned)
      return (uint64_t)eval(Nd->LHS) / eval(Nd->RHS);
    return eval(Nd->LHS) / eval(Nd->RHS);
  case ND_NEG:
    return -eval(Nd->LHS);
  case ND_MOD:
    if (Nd->Ty->IsUnsigned)
      return (uint64_t)eval(Nd->LHS) % eval(Nd->RHS);
    return eval(Nd->LHS) % eval(Nd->RHS);
  case ND_BITAND:
    return eval(Nd->LHS) & eval(Nd->RHS);
  case ND_BITOR:
    return eval(Nd->LHS) | eval(Nd->RHS);
  case ND_BITXOR:
    return eval(Nd->LHS) ^ eval(Nd->RHS);
  case ND_SHL:
    return eval(Nd->LHS) << eval(Nd->RHS);
  case ND_SHR:
    if (Nd->Ty->IsUnsigned && Nd->Ty->Size == 8)
      return (uint64_t)eval(Nd->LHS) >> eval(Nd->RHS);
    return eval(Nd->LHS) >> eval(Nd->RHS);
  case ND_EQ:
    return eval(Nd->LHS) == eval(Nd->RHS);
  case ND_NE:
    return eval(Nd->LHS) != eval(Nd->RHS);
  case ND_LT:
    if (Nd->LHS->Ty->IsUnsigned)
      return (uint64_t)eval(Nd->LHS) < eval(Nd->RHS);
    return eval(Nd->LHS) < eval(Nd->RHS);
  case ND_LE:
    if (Nd->LHS->Ty->IsUnsigned)
      return (uint64_t)eval(Nd->LHS) <= eval(Nd->RHS);
    return eval(Nd->LHS) <= eval(Nd->RHS);
  case ND_COND:
    return eval(Nd->Cond) ? eval2(Nd->Then, Label) : eval2(Nd->Els, Label);
  case ND_COMMA:
    return eval2(Nd->RHS, Label);
  case ND_NOT:
    return !eval(Nd->LHS);
  case ND_BITNOT:
    return ~eval(Nd->LHS);
  case ND_LOGAND:
    return eval(Nd->LHS) && eval(Nd->RHS);
  case ND_LOGOR:
    return eval(Nd->LHS) || eval(Nd->RHS);
  case ND_CAST: {
    int64_t Val = eval2(Nd->LHS, Label);
    if (isInteger(Nd->Ty)) {
      switch (Nd->Ty->Size) {
      case 1:
        return Nd->Ty->IsUnsigned ? (uint8_t)Val : (int8_t)Val;
      case 2:
        return Nd->Ty->IsUnsigned ? (uint16_t)Val : (int16_t)Val;
      case 4:
        return Nd->Ty->IsUnsigned ? (uint32_t)Val : (int32_t)Val;
      }
    }
    return Val;
  }
  case ND_ADDR:
    return evalRVal(Nd->LHS, Label);
  case ND_MEMBER:
    // 未开辟Label的地址，则表明不是表达式常量
    if (!Label)
      errorTok(Nd->Tok, "not a compile-time constant");
    // 不能为数组
    if (Nd->Ty->Kind != TY_ARRAY)
      errorTok(Nd->Tok, "invalid initializer");
    // 返回左部的值（并解析Label），加上成员变量的偏移量
    return evalRVal(Nd->LHS, Label) + Nd->Mem->Offset;
  case ND_VAR:
    // 未开辟Label的地址，则表明不是表达式常量
    if (!Label)
      errorTok(Nd->Tok, "not a compile-time constant");
    // 不能为数组或者函数
    if (Nd->Var->Ty->Kind != TY_ARRAY && Nd->Var->Ty->Kind != TY_FUNC)
      errorTok(Nd->Tok, "invalid initializer");
    *Label = Nd->Var->Name;
    return 0;
  case ND_NUM:
    return Nd->Val;
  default:
    break;
  }

  errorTok(Nd->Tok, "not a compile-time constant");
  return -1;
}

// 计算重定位变量
static int64_t evalRVal(Node *Nd, char **Label) {
  switch (Nd->Kind) {
  case ND_VAR:
    // 局部变量不能参与全局变量的初始化
    if (Nd->Var->IsLocal)
      errorTok(Nd->Tok, "not a compile-time constant");
    *Label = Nd->Var->Name;
    return 0;
  case ND_DEREF:
    // 直接进入到解引用的地址
    return eval2(Nd->LHS, Label);
  case ND_MEMBER:
    // 加上成员变量的偏移量
    return evalRVal(Nd->LHS, Label) + Nd->Mem->Offset;
  default:
    break;
  }

  errorTok(Nd->Tok, "invalid initializer");
  return -1;
}

// 解析常量表达式
int64_t constExpr(Token **Rest, Token *Tok) {
  // 进行常量表达式的构造
  Node *Nd = conditional(Rest, Tok);
  // 进行常量表达式的计算
  return eval(Nd);
}

// 解析浮点表达式
static double evalDouble(Node *Nd) {
  addType(Nd);

  // 处理是整型的情况
  if (isInteger(Nd->Ty)) {
    if (Nd->Ty->IsUnsigned)
      return (unsigned long)eval(Nd);
    return eval(Nd);
  }

  switch (Nd->Kind) {
  case ND_ADD:
    return evalDouble(Nd->LHS) + evalDouble(Nd->RHS);
  case ND_SUB:
    return evalDouble(Nd->LHS) - evalDouble(Nd->RHS);
  case ND_MUL:
    return evalDouble(Nd->LHS) * evalDouble(Nd->RHS);
  case ND_DIV:
    return evalDouble(Nd->LHS) / evalDouble(Nd->RHS);
  case ND_NEG:
    return -evalDouble(Nd->LHS);
  case ND_COND:
    return evalDouble(Nd->Cond) ? evalDouble(Nd->Then) : evalDouble(Nd->Els);
  case ND_COMMA:
    return evalDouble(Nd->RHS);
  case ND_CAST:
    if (isFloNum(Nd->LHS->Ty))
      return evalDouble(Nd->LHS);
    return eval(Nd->LHS);
  case ND_NUM:
    return Nd->FVal;
  default:
    errorTok(Nd->Tok, "not a compile-time constant");
    return -1;
  }
}

// 转换 A op= B为 TMP = &A, *TMP = *TMP op B
static Node *toAssign(Node *Binary) {
  // A
  addType(Binary->LHS);
  // B
  addType(Binary->RHS);
  Token *Tok = Binary->Tok;

  // TMP
  Obj *Var = newLVar("", pointerTo(Binary->LHS->Ty));

  // TMP = &A
  Node *Expr1 = newBinary(ND_ASSIGN, newVarNode(Var, Tok),
                          newUnary(ND_ADDR, Binary->LHS, Tok), Tok);

  // *TMP = *TMP op B
  Node *Expr2 = newBinary(
      ND_ASSIGN, newUnary(ND_DEREF, newVarNode(Var, Tok), Tok),
      newBinary(Binary->Kind, newUnary(ND_DEREF, newVarNode(Var, Tok), Tok),
                Binary->RHS, Tok),
      Tok);

  // TMP = &A, *TMP = *TMP op B
  return newBinary(ND_COMMA, Expr1, Expr2, Tok);
}

// 解析赋值
// assign = conditional (assignOp assign)?
// assignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
//          | "<<=" | ">>="
static Node *assign(Token **Rest, Token *Tok) {
  // conditional
  Node *Nd = conditional(&Tok, Tok);

  // 可能存在递归赋值，如a=b=1
  // ("=" assign)?
  if (equal(Tok, "="))
    return Nd = newBinary(ND_ASSIGN, Nd, assign(Rest, Tok->Next), Tok);

  // ("+=" assign)?
  if (equal(Tok, "+="))
    return toAssign(newAdd(Nd, assign(Rest, Tok->Next), Tok));

  // ("-=" assign)?
  if (equal(Tok, "-="))
    return toAssign(newSub(Nd, assign(Rest, Tok->Next), Tok));

  // ("*=" assign)?
  if (equal(Tok, "*="))
    return toAssign(newBinary(ND_MUL, Nd, assign(Rest, Tok->Next), Tok));

  // ("/=" assign)?
  if (equal(Tok, "/="))
    return toAssign(newBinary(ND_DIV, Nd, assign(Rest, Tok->Next), Tok));

  // ("%=" assign)?
  if (equal(Tok, "%="))
    return toAssign(newBinary(ND_MOD, Nd, assign(Rest, Tok->Next), Tok));

  // ("&=" assign)?
  if (equal(Tok, "&="))
    return toAssign(newBinary(ND_BITAND, Nd, assign(Rest, Tok->Next), Tok));

  // ("|=" assign)?
  if (equal(Tok, "|="))
    return toAssign(newBinary(ND_BITOR, Nd, assign(Rest, Tok->Next), Tok));

  // ("^=" assign)?
  if (equal(Tok, "^="))
    return toAssign(newBinary(ND_BITXOR, Nd, assign(Rest, Tok->Next), Tok));

  // ("<<=" assign)?
  if (equal(Tok, "<<="))
    return toAssign(newBinary(ND_SHL, Nd, assign(Rest, Tok->Next), Tok));

  // (">>=" assign)?
  if (equal(Tok, ">>="))
    return toAssign(newBinary(ND_SHR, Nd, assign(Rest, Tok->Next), Tok));

  *Rest = Tok;
  return Nd;
}

// 解析条件运算符
// conditional = logOr ("?" expr ":" conditional)?
static Node *conditional(Token **Rest, Token *Tok) {
  // logOr
  Node *Cond = logOr(&Tok, Tok);

  // "?"
  if (!equal(Tok, "?")) {
    *Rest = Tok;
    return Cond;
  }

  // expr ":" conditional
  Node *Nd = newNode(ND_COND, Tok);
  Nd->Cond = Cond;
  // expr ":"
  Nd->Then = expr(&Tok, Tok->Next);
  Tok = skip(Tok, ":");
  // conditional，这里不能被解析为赋值式
  Nd->Els = conditional(Rest, Tok);
  return Nd;
}

// 逻辑或
// logOr = logAnd ("||" logAnd)*
static Node *logOr(Token **Rest, Token *Tok) {
  Node *Nd = logAnd(&Tok, Tok);
  while (equal(Tok, "||")) {
    Token *Start = Tok;
    Nd = newBinary(ND_LOGOR, Nd, logAnd(&Tok, Tok->Next), Start);
  }
  *Rest = Tok;
  return Nd;
}

// 逻辑与
// logAnd = bitOr ("&&" bitOr)*
static Node *logAnd(Token **Rest, Token *Tok) {
  Node *Nd = bitOr(&Tok, Tok);
  while (equal(Tok, "&&")) {
    Token *Start = Tok;
    Nd = newBinary(ND_LOGAND, Nd, bitOr(&Tok, Tok->Next), Start);
  }
  *Rest = Tok;
  return Nd;
}

// 按位或
// bitOr = bitXor ("|" bitXor)*
static Node *bitOr(Token **Rest, Token *Tok) {
  Node *Nd = bitXor(&Tok, Tok);
  while (equal(Tok, "|")) {
    Token *Start = Tok;
    Nd = newBinary(ND_BITOR, Nd, bitXor(&Tok, Tok->Next), Start);
  }
  *Rest = Tok;
  return Nd;
}

// 按位异或
// bitXor = bitAnd ("^" bitAnd)*
static Node *bitXor(Token **Rest, Token *Tok) {
  Node *Nd = bitAnd(&Tok, Tok);
  while (equal(Tok, "^")) {
    Token *Start = Tok;
    Nd = newBinary(ND_BITXOR, Nd, bitAnd(&Tok, Tok->Next), Start);
  }
  *Rest = Tok;
  return Nd;
}

// 按位与
// bitAnd = equality ("&" equality)*
static Node *bitAnd(Token **Rest, Token *Tok) {
  Node *Nd = equality(&Tok, Tok);
  while (equal(Tok, "&")) {
    Token *Start = Tok;
    Nd = newBinary(ND_BITAND, Nd, equality(&Tok, Tok->Next), Start);
  }
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
// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(Token **Rest, Token *Tok) {
  // shift
  Node *Nd = shift(&Tok, Tok);

  // ("<" shift | "<=" shift | ">" shift | ">=" shift)*
  while (true) {
    Token *Start = Tok;

    // "<" shift
    if (equal(Tok, "<")) {
      Nd = newBinary(ND_LT, Nd, shift(&Tok, Tok->Next), Start);
      continue;
    }

    // "<=" shift
    if (equal(Tok, "<=")) {
      Nd = newBinary(ND_LE, Nd, shift(&Tok, Tok->Next), Start);
      continue;
    }

    // ">" shift
    // X>Y等价于Y<X
    if (equal(Tok, ">")) {
      Nd = newBinary(ND_LT, shift(&Tok, Tok->Next), Nd, Start);
      continue;
    }

    // ">=" shift
    // X>=Y等价于Y<=X
    if (equal(Tok, ">=")) {
      Nd = newBinary(ND_LE, shift(&Tok, Tok->Next), Nd, Start);
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析位移
// shift = add ("<<" add | ">>" add)*
static Node *shift(Token **Rest, Token *Tok) {
  // add
  Node *Nd = add(&Tok, Tok);

  while (true) {
    Token *Start = Tok;

    // "<<" add
    if (equal(Tok, "<<")) {
      Nd = newBinary(ND_SHL, Nd, add(&Tok, Tok->Next), Start);
      continue;
    }

    // ">>" add
    if (equal(Tok, ">>")) {
      Nd = newBinary(ND_SHR, Nd, add(&Tok, Tok->Next), Start);
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
  if (isNumeric(LHS->Ty) && isNumeric(RHS->Ty))
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
  // 指针加法，ptr+1，1不是1个字节而是1个元素的空间，所以需要×Size操作
  // 指针用long类型存储
  RHS = newBinary(ND_MUL, RHS, newLong(LHS->Ty->Base->Size, Tok), Tok);
  return newBinary(ND_ADD, LHS, RHS, Tok);
}

// 解析各种减法
static Node *newSub(Node *LHS, Node *RHS, Token *Tok) {
  // 为左右部添加类型
  addType(LHS);
  addType(RHS);

  // num - num
  if (isNumeric(LHS->Ty) && isNumeric(RHS->Ty))
    return newBinary(ND_SUB, LHS, RHS, Tok);

  // ptr - num
  if (LHS->Ty->Base && isInteger(RHS->Ty)) {
    // 指针用long类型存储
    RHS = newBinary(ND_MUL, RHS, newLong(LHS->Ty->Base->Size, Tok), Tok);
    addType(RHS);
    Node *Nd = newBinary(ND_SUB, LHS, RHS, Tok);
    // 节点类型为指针
    Nd->Ty = LHS->Ty;
    return Nd;
  }

  // ptr - ptr，返回两指针间有多少元素
  if (LHS->Ty->Base && RHS->Ty->Base) {
    Node *Nd = newBinary(ND_SUB, LHS, RHS, Tok);
    Nd->Ty = TyLong;
    return newBinary(ND_DIV, Nd, newNum(LHS->Ty->Base->Size, Tok), Tok);
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
// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(Token **Rest, Token *Tok) {
  // cast
  Node *Nd = cast(&Tok, Tok);

  // ("*" cast | "/" cast | "%" cast)*
  while (true) {
    Token *Start = Tok;

    // "*" cast
    if (equal(Tok, "*")) {
      Nd = newBinary(ND_MUL, Nd, cast(&Tok, Tok->Next), Start);
      continue;
    }

    // "/" cast
    if (equal(Tok, "/")) {
      Nd = newBinary(ND_DIV, Nd, cast(&Tok, Tok->Next), Start);
      continue;
    }

    // "%" cast
    if (equal(Tok, "%")) {
      Nd = newBinary(ND_MOD, Nd, cast(&Tok, Tok->Next), Start);
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析类型转换
// cast = "(" typeName ")" cast | unary
static Node *cast(Token **Rest, Token *Tok) {
  // cast = "(" typeName ")" cast
  if (equal(Tok, "(") && isTypename(Tok->Next)) {
    Token *Start = Tok;
    Type *Ty = typename(&Tok, Tok->Next);
    Tok = skip(Tok, ")");

    // 复合字面量
    if (equal(Tok, "{"))
      return unary(Rest, Start);

    // 解析嵌套的类型转换
    Node *Nd = newCast(cast(Rest, Tok), Ty);
    Nd->Tok = Start;
    return Nd;
  }

  // unary
  return unary(Rest, Tok);
}

// 解析一元运算
// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | postfix
static Node *unary(Token **Rest, Token *Tok) {
  // "+" cast
  if (equal(Tok, "+"))
    return cast(Rest, Tok->Next);

  // "-" cast
  if (equal(Tok, "-"))
    return newUnary(ND_NEG, cast(Rest, Tok->Next), Tok);

  // "&" cast
  if (equal(Tok, "&"))
    return newUnary(ND_ADDR, cast(Rest, Tok->Next), Tok);

  // "*" cast
  if (equal(Tok, "*"))
    return newUnary(ND_DEREF, cast(Rest, Tok->Next), Tok);

  // "!" cast
  if (equal(Tok, "!"))
    return newUnary(ND_NOT, cast(Rest, Tok->Next), Tok);

  // "~" cast
  if (equal(Tok, "~"))
    return newUnary(ND_BITNOT, cast(Rest, Tok->Next), Tok);

  // 转换 ++i 为 i+=1
  // "++" unary
  if (equal(Tok, "++"))
    return toAssign(newAdd(unary(Rest, Tok->Next), newNum(1, Tok), Tok));

  // 转换 +-i 为 i-=1
  // "--" unary
  if (equal(Tok, "--"))
    return toAssign(newSub(unary(Rest, Tok->Next), newNum(1, Tok), Tok));

  // postfix
  return postfix(Rest, Tok);
}

// structMembers = (declspec declarator (","  declarator)* ";")*
static void structMembers(Token **Rest, Token *Tok, Type *Ty) {
  Member Head = {};
  Member *Cur = &Head;
  // 记录成员变量的索引值
  int Idx = 0;

  while (!equal(Tok, "}")) {
    // declspec
    VarAttr Attr = {};
    Type *BaseTy = declspec(&Tok, Tok, &Attr);
    int First = true;

    while (!consume(&Tok, Tok, ";")) {
      if (!First)
        Tok = skip(Tok, ",");
      First = false;

      Member *Mem = calloc(1, sizeof(Member));
      // declarator
      Mem->Ty = declarator(&Tok, Tok, BaseTy);
      Mem->Name = Mem->Ty->Name;
      // 成员变量对应的索引值
      Mem->Idx = Idx++;
      // 设置对齐值
      Mem->Align = Attr.Align ? Attr.Align : Mem->Ty->Align;
      Cur = Cur->Next = Mem;
    }
  }

  // 解析灵活数组成员，数组大小设为0
  if (Cur != &Head && Cur->Ty->Kind == TY_ARRAY && Cur->Ty->ArrayLen < 0) {
    Cur->Ty = arrayOf(Cur->Ty->Base, 0);
    // 设置类型为灵活的
    Ty->IsFlexible = true;
  }

  *Rest = Tok->Next;
  Ty->Mems = Head.Next;
}

// structUnionDecl = ident? ("{" structMembers)?
static Type *structUnionDecl(Token **Rest, Token *Tok) {
  // 读取标签
  Token *Tag = NULL;
  if (Tok->Kind == TK_IDENT) {
    Tag = Tok;
    Tok = Tok->Next;
  }

  // 构造不完整结构体
  if (Tag && !equal(Tok, "{")) {
    *Rest = Tok;

    Type *Ty = findTag(Tag);
    if (Ty)
      return Ty;

    Ty = structType();
    Ty->Size = -1;
    pushTagScope(Tag, Ty);
    return Ty;
  }

  // ("{" structMembers)?
  Tok = skip(Tok, "{");

  // 构造一个结构体
  Type *Ty = structType();
  structMembers(Rest, Tok, Ty);
  Ty->Align = 1;

  // 如果是重复定义，就覆盖之前的定义。否则有名称就注册结构体类型
  if (Tag) {
    for (TagScope *S = Scp->Tags; S; S = S->Next) {
      if (equal(Tag, S->Name)) {
        *S->Ty = *Ty;
        return S->Ty;
      }
    }

    pushTagScope(Tag, Ty);
  }
  return Ty;
}

// structDecl = structUnionDecl
static Type *structDecl(Token **Rest, Token *Tok) {
  Type *Ty = structUnionDecl(Rest, Tok);
  Ty->Kind = TY_STRUCT;

  // 不完整结构体
  if (Ty->Size < 0)
    return Ty;

  // 计算结构体内成员的偏移量
  int Offset = 0;
  for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
    Offset = alignTo(Offset, Mem->Align);
    Mem->Offset = Offset;
    Offset += Mem->Ty->Size;

    if (Ty->Align < Mem->Align)
      Ty->Align = Mem->Align;
  }
  Ty->Size = alignTo(Offset, Ty->Align);

  return Ty;
}

// unionDecl = structUnionDecl
static Type *unionDecl(Token **Rest, Token *Tok) {
  Type *Ty = structUnionDecl(Rest, Tok);
  Ty->Kind = TY_UNION;

  // 联合体需要设置为最大的对齐量与大小，变量偏移量都默认为0
  for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
    if (Ty->Align < Mem->Align)
      Ty->Align = Mem->Align;
    if (Ty->Size < Mem->Ty->Size)
      Ty->Size = Mem->Ty->Size;
  }
  // 将大小对齐
  Ty->Size = alignTo(Ty->Size, Ty->Align);
  return Ty;
}

// 获取结构体成员
static Member *getStructMember(Type *Ty, Token *Tok) {
  for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next)
    if (Mem->Name->Len == Tok->Len &&
        !strncmp(Mem->Name->Loc, Tok->Loc, Tok->Len))
      return Mem;
  errorTok(Tok, "no such member");
  return NULL;
}

// 构建结构体成员的节点
static Node *structRef(Node *LHS, Token *Tok) {
  addType(LHS);
  if (LHS->Ty->Kind != TY_STRUCT && LHS->Ty->Kind != TY_UNION)
    errorTok(LHS->Tok, "not a struct nor a union");

  Node *Nd = newUnary(ND_MEMBER, LHS, Tok);
  Nd->Mem = getStructMember(LHS->Ty, Tok);
  return Nd;
}

// 转换 A++ 为 `(typeof A)((A += 1) - 1)`
// Increase Decrease
static Node *newIncDec(Node *Nd, Token *Tok, int Addend) {
  addType(Nd);
  return newCast(newAdd(toAssign(newAdd(Nd, newNum(Addend, Tok), Tok)),
                        newNum(-Addend, Tok), Tok),
                 Nd->Ty);
}

// postfix = "(" typeName ")" "{" initializerList "}"
//         = ident "(" funcArgs ")" postfixTail*
//         | primary postfixTail*
//
// postfixTail = "[" expr "]"
//             | "(" funcArgs ")"
//             | "." ident
//             | "->" ident
//             | "++"
//             | "--"
static Node *postfix(Token **Rest, Token *Tok) {
  // "(" typeName ")" "{" initializerList "}"
  if (equal(Tok, "(") && isTypename(Tok->Next)) {
    // 复合字面量
    Token *Start = Tok;
    Type *Ty = typename(&Tok, Tok->Next);
    Tok = skip(Tok, ")");

    if (Scp->Next == NULL) {
      Obj *Var = newAnonGVar(Ty);
      GVarInitializer(Rest, Tok, Var);
      return newVarNode(Var, Start);
    }

    Obj *Var = newLVar("", Ty);
    Node *LHS = LVarInitializer(Rest, Tok, Var);
    Node *RHS = newVarNode(Var, Tok);
    return newBinary(ND_COMMA, LHS, RHS, Start);
  }

  // primary
  Node *Nd = primary(&Tok, Tok);

  // ("[" expr "]")*
  while (true) {
    // ident "(" funcArgs ")"
    // 匹配到函数调用
    if (equal(Tok, "(")) {
      Nd = funCall(&Tok, Tok->Next, Nd);
      continue;
    }

    if (equal(Tok, "[")) {
      // x[y] 等价于 *(x+y)
      Token *Start = Tok;
      Node *Idx = expr(&Tok, Tok->Next);
      Tok = skip(Tok, "]");
      Nd = newUnary(ND_DEREF, newAdd(Nd, Idx, Start), Start);
      continue;
    }

    // "." ident
    if (equal(Tok, ".")) {
      Nd = structRef(Nd, Tok->Next);
      Tok = Tok->Next->Next;
      continue;
    }

    // "->" ident
    if (equal(Tok, "->")) {
      // x->y 等价于 (*x).y
      Nd = newUnary(ND_DEREF, Nd, Tok);
      Nd = structRef(Nd, Tok->Next);
      Tok = Tok->Next->Next;
      continue;
    }

    if (equal(Tok, "++")) {
      Nd = newIncDec(Nd, Tok, 1);
      Tok = Tok->Next;
      continue;
    }

    if (equal(Tok, "--")) {
      Nd = newIncDec(Nd, Tok, -1);
      Tok = Tok->Next;
      continue;
    }

    *Rest = Tok;
    return Nd;
  }
}

// 解析函数调用
// funcall = (assign ("," assign)*)? ")"
static Node *funCall(Token **Rest, Token *Tok, Node *Fn) {
  addType(Fn);

  // 检查函数指针
  if (Fn->Ty->Kind != TY_FUNC &&
      (Fn->Ty->Kind != TY_PTR || Fn->Ty->Base->Kind != TY_FUNC))
    errorTok(Fn->Tok, "not a function");

  // 处理函数的类型设为非指针的类型
  Type *Ty = (Fn->Ty->Kind == TY_FUNC) ? Fn->Ty : Fn->Ty->Base;
  // 函数形参的类型
  Type *ParamTy = Ty->Params;

  Node Head = {};
  Node *Cur = &Head;

  while (!equal(Tok, ")")) {
    if (Cur != &Head)
      Tok = skip(Tok, ",");
    // assign
    Node *Arg = assign(&Tok, Tok);
    addType(Arg);

    if (ParamTy) {
      if (ParamTy->Kind == TY_STRUCT || ParamTy->Kind == TY_UNION)
        errorTok(Arg->Tok, "passing struct or union is not supported yet");
      // 将参数节点的类型进行转换
      Arg = newCast(Arg, ParamTy);
      // 前进到下一个形参类型
      ParamTy = ParamTy->Next;
    } else if (Arg->Ty->Kind == TY_FLOAT) {
      // 若无形参类型，浮点数会被提升为double
      Arg = newCast(Arg, TyDouble);
    }
    // 对参数进行存储
    Cur->Next = Arg;
    Cur = Cur->Next;
    addType(Cur);
  }

  *Rest = skip(Tok, ")");

  // 构造一个函数调用的节点
  Node *Nd = newUnary(ND_FUNCALL, Fn, Tok);

  // 函数类型
  Nd->FuncType = Ty;
  // 读取的返回类型
  Nd->Ty = Ty->ReturnTy;
  Nd->Args = Head.Next;
  return Nd;
}

// 解析括号、数字、变量
// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" typeName ")"
//         | "sizeof" unary
//         | "_Alignof" "(" typeName ")"
//         | "_Alignof" unary
//         | ident
//         | str
//         | num
static Node *primary(Token **Rest, Token *Tok) {
  Token *Start = Tok;

  // "(" "{" stmt+ "}" ")"
  if (equal(Tok, "(") && equal(Tok->Next, "{")) {
    // This is a GNU statement expresssion.
    Node *Nd = newNode(ND_STMT_EXPR, Tok);
    Nd->Body = compoundStmt(&Tok, Tok->Next->Next)->Body;
    *Rest = skip(Tok, ")");
    return Nd;
  }

  // "(" expr ")"
  if (equal(Tok, "(")) {
    Node *Nd = expr(&Tok, Tok->Next);
    *Rest = skip(Tok, ")");
    return Nd;
  }

  // "sizeof" "(" typeName ")"
  if (equal(Tok, "sizeof") && equal(Tok->Next, "(") &&
      isTypename(Tok->Next->Next)) {
    Type *Ty = typename(&Tok, Tok->Next->Next);
    *Rest = skip(Tok, ")");
    return newULong(Ty->Size, Start);
  }

  // "sizeof" unary
  if (equal(Tok, "sizeof")) {
    Node *Nd = unary(Rest, Tok->Next);
    addType(Nd);
    return newULong(Nd->Ty->Size, Tok);
  }

  // "_Alignof" "(" typeName ")"
  // 读取类型的对齐值
  if (equal(Tok, "_Alignof") && equal(Tok->Next, "(") &&
      isTypename(Tok->Next->Next)) {
    Type *Ty = typename(&Tok, Tok->Next->Next);
    *Rest = skip(Tok, ")");
    return newULong(Ty->Align, Tok);
  }

  // "_Alignof" unary
  // 读取变量的对齐值
  if (equal(Tok, "_Alignof")) {
    Node *Nd = unary(Rest, Tok->Next);
    addType(Nd);
    return newULong(Nd->Ty->Align, Tok);
  }

  // ident
  if (Tok->Kind == TK_IDENT) {
    // 查找变量（或枚举常量）
    VarScope *S = findVar(Tok);
    *Rest = Tok->Next;

    if (S) {
      // 是否为变量
      if (S->Var)
        return newVarNode(S->Var, Tok);
      // 否则为枚举常量
      if (S->EnumTy)
        return newNum(S->EnumVal, Tok);
    }

    if (equal(Tok->Next, "("))
      errorTok(Tok, "implicit declaration of a function");
    errorTok(Tok, "undefined variable");
  }

  // str
  if (Tok->Kind == TK_STR) {
    Obj *Var = newStringLiteral(Tok->Str, Tok->Ty);
    *Rest = Tok->Next;
    return newVarNode(Var, Tok);
  }

  // num
  if (Tok->Kind == TK_NUM) {
    Node *Nd;
    if (isFloNum(Tok->Ty)) {
      // 浮点数节点
      Nd = newNode(ND_NUM, Tok);
      Nd->FVal = Tok->FVal;
    } else {
      // 整型节点
      Nd = newNum(Tok->Val, Tok);
    }

    // 设置类型为终结符的类型
    Nd->Ty = Tok->Ty;
    *Rest = Tok->Next;
    return Nd;
  }

  errorTok(Tok, "expected an expression");
  return NULL;
}

// 解析类型别名
static Token *parseTypedef(Token *Tok, Type *BaseTy) {
  bool First = true;

  while (!consume(&Tok, Tok, ";")) {
    if (!First)
      Tok = skip(Tok, ",");
    First = false;

    Type *Ty = declarator(&Tok, Tok, BaseTy);
    if (!Ty->Name)
      errorTok(Ty->NamePos, "typedef name omitted");
    // 类型别名的变量名存入变量域中，并设置类型
    pushScope(getIdent(Ty->Name))->Typedef = Ty;
  }
  return Tok;
}

// 将形参添加到Locals
static void createParamLVars(Type *Param) {
  if (Param) {
    // 递归到形参最底部
    // 先将最底部的加入Locals中，之后的都逐个加入到顶部，保持顺序不变
    createParamLVars(Param->Next);
    if (!Param->Name)
      errorTok(Param->NamePos, "parameter name omitted");
    // 添加到Locals中
    newLVar(getIdent(Param->Name), Param);
  }
}

// 匹配goto和标签
// 因为标签可能会出现在goto后面，所以要在解析完函数后再进行goto和标签的解析
static void resolveGotoLabels(void) {
  // 遍历使goto对应上label
  for (Node *X = Gotos; X; X = X->GotoNext) {
    for (Node *Y = Labels; Y; Y = Y->GotoNext) {
      if (!strcmp(X->Label, Y->Label)) {
        X->UniqueLabel = Y->UniqueLabel;
        break;
      }
    }

    if (X->UniqueLabel == NULL)
      errorTok(X->Tok->Next, "use of undeclared label");
  }

  Gotos = NULL;
  Labels = NULL;
}

// functionDefinition = declspec declarator "(" ")" "{" compoundStmt*
static Token *function(Token *Tok, Type *BaseTy, VarAttr *Attr) {
  Type *Ty = declarator(&Tok, Tok, BaseTy);
  if (!Ty->Name)
    errorTok(Ty->NamePos, "function name omitted");

  Obj *Fn = newGVar(getIdent(Ty->Name), Ty);
  Fn->IsFunction = true;
  Fn->IsDefinition = !consume(&Tok, Tok, ";");
  Fn->IsStatic = Attr->IsStatic;

  // 判断是否没有函数定义
  if (!Fn->IsDefinition)
    return Tok;

  CurrentFn = Fn;
  // 清空全局变量Locals
  Locals = NULL;
  // 进入新的域
  enterScope();
  // 函数参数
  createParamLVars(Ty->Params);
  Fn->Params = Locals;

  // 判断是否为可变参数
  if (Ty->IsVariadic)
    Fn->VaArea = newLVar("__va_area__", arrayOf(TyChar, 64));

  Tok = skip(Tok, "{");
  // 函数体存储语句的AST，Locals存储变量
  Fn->Body = compoundStmt(&Tok, Tok);
  Fn->Locals = Locals;
  // 结束当前域
  leaveScope();
  // 处理goto和标签
  resolveGotoLabels();
  return Tok;
}

// 构造全局变量
static Token *globalVariable(Token *Tok, Type *Basety, VarAttr *Attr) {
  bool First = true;

  while (!consume(&Tok, Tok, ";")) {
    if (!First)
      Tok = skip(Tok, ",");
    First = false;

    Type *Ty = declarator(&Tok, Tok, Basety);
    if (!Ty->Name)
      errorTok(Ty->NamePos, "variable name omitted");
    // 全局变量初始化
    Obj *Var = newGVar(getIdent(Ty->Name), Ty);
    // 是否具有定义
    Var->IsDefinition = !Attr->IsExtern;
    // 传递是否为static
    Var->IsStatic = Attr->IsStatic;
    // 若有设置，则覆盖全局变量的对齐值
    if (Attr->Align)
      Var->Align = Attr->Align;

    if (equal(Tok, "="))
      GVarInitializer(&Tok, Tok->Next, Var);
  }
  return Tok;
}

// 区分 函数还是全局变量
static bool isFunction(Token *Tok) {
  if (equal(Tok, ";"))
    return false;

  // 虚设变量，用于调用declarator
  Type Dummy = {};
  Type *Ty = declarator(&Tok, Tok, &Dummy);
  return Ty->Kind == TY_FUNC;
}

// 语法解析入口函数
// program = (typedef | functionDefinition* | global-variable)*
Obj *parse(Token *Tok) {
  Globals = NULL;

  while (Tok->Kind != TK_EOF) {
    VarAttr Attr = {};
    Type *BaseTy = declspec(&Tok, Tok, &Attr);

    // typedef
    if (Attr.IsTypedef) {
      Tok = parseTypedef(Tok, BaseTy);
      continue;
    }

    // 函数
    if (isFunction(Tok)) {
      Tok = function(Tok, BaseTy, &Attr);
      continue;
    }

    // 全局变量
    Tok = globalVariable(Tok, BaseTy, &Attr);
  }

  return Globals;
}
