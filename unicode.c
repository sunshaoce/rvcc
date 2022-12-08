#include "rvcc.h"

// 将unicode字符编码为UTF8的格式
int encodeUTF8(char *Buf, uint32_t C) {
  // 1字节UTF8编码，可用7位，0~127，与ASCII码兼容
  // 0x7F=0b01111111=127
  if (C <= 0x7F) {
    // 首字节内容为：0xxxxxxx
    Buf[0] = C;
    return 1;
  }

  // 2字节UTF8编码，可用11位，128~2047
  // 0x7FF=0b111 11111111=2047
  if (C <= 0x7FF) {
    // 首字节内容为：110xxxxx
    Buf[0] = 0b11000000 | (C >> 6);
    // 后续字节都为：10xxxxxx
    Buf[1] = 0b10000000 | (C & 0b00111111);
    return 2;
  }

  // 3字节UTF8编码，可用16位，2048~65535
  // 0xFFFF=0b11111111 11111111=65535
  if (C <= 0xFFFF) {
    // 首字节内容为：1110xxxx
    Buf[0] = 0b11100000 | (C >> 12);
    // 后续字节都为：10xxxxxx
    Buf[1] = 0b10000000 | ((C >> 6) & 0b00111111);
    Buf[2] = 0b10000000 | (C & 0b00111111);
    return 3;
  }

  // 4字节UTF8编码，可用21位，65536~1114111
  // 0x10FFFF=1114111
  //
  // 首字节内容为：11110xxx
  Buf[0] = 0b11110000 | (C >> 18);
  // 后续字节都为：10xxxxxx
  Buf[1] = 0b10000000 | ((C >> 12) & 0b00111111);
  Buf[2] = 0b10000000 | ((C >> 6) & 0b00111111);
  Buf[3] = 0b10000000 | (C & 0b00111111);
  return 4;
}

// 将UTF-8的格式解码为unicode字符
uint32_t decodeUTF8(char **NewPos, char *P) {
  // 1字节UTF8编码，0~127，与ASCII码兼容
  if ((unsigned char)*P < 128) {
    *NewPos = P + 1;
    return *P;
  }

  char *Start = P;
  int Len;
  uint32_t C;

  if ((unsigned char)*P >= 0b11110000) {
    // 4字节UTF8编码，首字节内容为：11110xxx
    Len = 4;
    C = *P & 0b111;
  } else if ((unsigned char)*P >= 0b11100000) {
    // 3字节UTF8编码，首字节内容为：1110xxxx
    Len = 3;
    C = *P & 0b1111;
  } else if ((unsigned char)*P >= 0b11000000) {
    // 2字节UTF8编码，首字节内容为：110xxxxx
    Len = 2;
    C = *P & 0b11111;
  } else {
    errorAt(Start, "invalid UTF-8 sequence");
  }

  // 后续字节都为：10xxxxxx
  for (int I = 1; I < Len; I++) {
    if ((unsigned char)P[I] >> 6 != 0b10)
      errorAt(Start, "invalid UTF-8 sequence");
    C = (C << 6) | (P[I] & 0b111111);
  }

  // 前进Len字节
  *NewPos = P + Len;
  // 返回获取到的值
  return C;
}
