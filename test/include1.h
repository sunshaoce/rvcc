#include "include2.h"

// [189] 支持 __FILE__ 和 __LINE__
char *include1_filename = __FILE__;
int include1_line = __LINE__;

// [160] 支持 #include "..."
int include1 = 5;
