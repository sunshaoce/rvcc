#include "rvcc.h"

// 预处理器入口函数
Token *preprocess(Token *Tok) {
  // 将所有关键字的终结符，都标记为KEYWORD
  convertKeywords(Tok);
  return Tok;
}
