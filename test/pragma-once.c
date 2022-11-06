#include "test.h"

// [299] [GNU] 支持#pragma once
#pragma once

#include "test/pragma-once.c"

int main() {
  printf("OK\n");
  return 0;
}
