#include "rvcc.h"

// 是否行首是#号
static bool isHash(Token *Tok) { return Tok->AtBOL && equal(Tok, "#"); }

// 遍历终结符，处理宏和指示
static Token *preprocess2(Token *Tok) {
  Token Head = {};
  Token *Cur = &Head;

  // 遍历终结符
  while (Tok->Kind != TK_EOF) {
    // 如果不是#号开头则前进
    if (!isHash(Tok)) {
      Cur->Next = Tok;
      Cur = Cur->Next;
      Tok = Tok->Next;
      continue;
    }

    // 下一终结符
    Tok = Tok->Next;

    // 支持空指示
    if (Tok->AtBOL)
      continue;

    errorTok(Tok, "invalid preprocessor directive");
  }

  Cur->Next = Tok;
  return Head.Next;
}

// 预处理器入口函数
Token *preprocess(Token *Tok) {
  // 处理宏和指示
  Tok = preprocess2(Tok);
  // 将所有关键字的终结符，都标记为KEYWORD
  convertKeywords(Tok);
  return Tok;
}
