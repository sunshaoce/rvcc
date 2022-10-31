#include "test.h"

int main() {

  printf("[246] 支持#line\n");
#line 500 "foo"
  ASSERT(501, __LINE__);
  ASSERT(0, strcmp(__FILE__, "foo"));

#line 800 "bar"
  ASSERT(801, __LINE__);
  ASSERT(0, strcmp(__FILE__, "bar"));

#line 1
  ASSERT(2, __LINE__);

  printf("[247] [GNU] 支持行标记指示\n");
# 200 "xyz" 2 3
  ASSERT(201, __LINE__);
  ASSERT(0, strcmp(__FILE__, "xyz"));

  printf("OK\n");
  return 0;
}
