#ifndef __STDARG_H
#define __STDARG_H

typedef void *va_list;

#define va_start(ap, last)                                                     \
  do {                                                                         \
    ap = __va_area__;                                                          \
  } while (0)

#define va_end(ap)

// [196] 支持 va_arg()
#define va_arg(ap, type)                                                       \
  ({                                                                           \
    type val = *(type *)ap;                                                    \
    ap += 8;                                                                   \
    val;                                                                       \
  })

#define va_copy(dest, src) (dest = src)

#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

#endif
