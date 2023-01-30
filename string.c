#include "rvcc.h"

// 压入字符串数组
void strArrayPush(StringArray *Arr, char *S) {
  // 如果为空，没有数据
  if (!Arr->Data) {
    // 开辟8个字符串的位置
    Arr->Data = calloc(8, sizeof(char *));
    // 将容量设为8
    Arr->Capacity = 8;
  }

  // 如果存满了，开辟一倍的空间
  if (Arr->Capacity == Arr->Len) {
    // 再开辟当前容量一倍的空间
    Arr->Data = realloc(Arr->Data, sizeof(char *) * Arr->Capacity * 2);
    // 容量翻倍
    Arr->Capacity *= 2;
    // 清空新开辟的空间
    for (int I = Arr->Len; I < Arr->Capacity; I++)
      Arr->Data[I] = NULL;
  }

  // 存入字符串
  Arr->Data[Arr->Len++] = S;
}

// 格式化后返回字符串
char *format(char *Fmt, ...) {
  char *Buf;
  size_t BufLen;
  // 将字符串对应的内存作为I/O流
  FILE *Out = open_memstream(&Buf, &BufLen);

  va_list VA;
  va_start(VA, Fmt);
  // 向流中写入数据
  vfprintf(Out, Fmt, VA);
  va_end(VA);

  fclose(Out);
  return Buf;
}
