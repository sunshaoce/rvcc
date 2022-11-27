#ifndef __STDFLOAT_H
#define __STDFLOAT_H

// 浮点数=(+-)精度*基数^指数

#define DECIMAL_DIG 36 // 浮点类型转换为21位十进制数，而值不变
#define FLT_EVAL_METHOD 0 // 所用类型的范围和精度进行所有运算和常量求值
#define FLT_RADIX 2  // 三种浮点类型指数的基数
#define FLT_ROUNDS 1 // 浮点算术运算向最近的数字进行舍入

#define FLT_DIG 6           // float有效数字位数
#define FLT_EPSILON 0x1p-23 // float的最小差值
#define FLT_MANT_DIG 24 // 能无精度损失地表示成float的2进制最大位数
#define FLT_MAX 0x1.fffffep+127 // float最大值
#define FLT_MAX_10_EXP 38       // float的10进制指数的最大值
#define FLT_MAX_EXP 128         // float的2进制指数的最大值
#define FLT_MIN 0x1p-126        // float的最小值
#define FLT_MIN_10_EXP -37      // float的10进制指数的最大值
#define FLT_MIN_EXP -125        // float的2进制指数的最小值
#define FLT_TRUE_MIN 0x1p-149   // float的最小正值

#define DBL_DIG 15          // double有效数字位数
#define DBL_EPSILON 0x1p-52 // 1.0和double的最小差值
#define DBL_MANT_DIG 53 // 能无精度损失地表示成double的2进制最大位数
#define DBL_MAX 0x1.fffffffffffffp+1023 // double最大值
#define DBL_MAX_10_EXP 308              // double的10进制指数的最大值
#define DBL_MAX_EXP 1024                // double的2进制指数的最大值
#define DBL_MIN 0x1p-1022               // double的最小值
#define DBL_MIN_10_EXP -307             // double的10进制指数的最大值
#define DBL_MIN_EXP -1021               // double的2进制指数的最小值
#define DBL_TRUE_MIN 0x0.0000000000001p-1022 // double的最小正值

#define LDBL_DIG 33           // long double有效数字位数
#define LDBL_EPSILON 0x1p-112 // 1.0和long double的最小差值
#define LDBL_MANT_DIG 113 // 能无精度损失地表示成long double的2进制最大位数
#define LDBL_MAX 0x1.ffffffffffffffffffffffffffffp+16383 // long double最大值
#define LDBL_MAX_10_EXP 4932  // long double的10进制指数的最大值
#define LDBL_MAX_EXP 16384    // long double的2进制指数的最大值
#define LDBL_MIN 0x1p-16382   // long double的最小值
#define LDBL_MIN_10_EXP -4931 // long double的10进制指数的最大值
#define LDBL_MIN_EXP -16381   // long double的2进制指数的最小值
// long double的最小正值
#define LDBL_TRUE_MIN 0x0.0000000000000000000000000001p-16382

#endif
