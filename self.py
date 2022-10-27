#!/usr/bin/python3
import re
import sys

# 预处理一些类型和函数
print("""
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;
typedef unsigned long size_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef struct FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

struct stat {
  char _[512];
};

typedef void* va_list;

void *malloc(long size);
void *calloc(long nmemb, long size);
void *realloc(void *buf, long size);
int *__errno_location();
char *strerror(int errnum);
FILE *fopen(char *pathname, char *mode);
FILE *open_memstream(char **ptr, size_t *sizeloc);
long fread(void *ptr, long size, long nmemb, FILE *stream);
size_t fwrite(void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);
int fclose(FILE *fp);
int fputc(int c, FILE *stream);
int feof(FILE *stream);
static void assert() {}
int strcmp(char *s1, char *s2);
int strncasecmp(char *s1, char *s2, long n);
int memcmp(char *s1, char *s2, long n);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int fprintf(FILE *fp, char *fmt, ...);
int vfprintf(FILE *fp, char *fmt, va_list ap);
long strlen(char *p);
int strncmp(char *p, char *q, long n);
void *memcpy(char *dst, char *src, long n);
char *strdup(char *p);
char *strndup(char *p, long n);
int isspace(int c);
int ispunct(int c);
int isdigit(int c);
int isxdigit(int c);
int isprint(int);
char *strstr(char *haystack, char *needle);
char *strchr(char *s, int c);
double strtod(char *nptr, char **endptr);
static void va_end(va_list ap) {}
long strtoul(char *nptr, char **endptr, int base);
void exit(int code);
double log2(double);
extern void* __va_area__;
""")

# 对文件内容进行替换
for Path in sys.argv[1:]:
    with open(Path) as File:
        # 读取文件
        S = File.read()
        # 删除 空白注释的一行
        S = re.sub(r'\\\n', '', S)
        # 删除 预处理的一行
        S = re.sub(r'^\s*#.*', '', S, flags=re.MULTILINE)
        # 删除 只有换行的一行
        S = re.sub(r'"\n\s*"', '', S)
        # 替换 布尔类型
        S = re.sub(r'\bbool\b', '_Bool', S)
        # 替换 errorno
        S = re.sub(r'\berrno\b', '*__errno_location()', S)
        # 替换 true
        S = re.sub(r'\btrue\b', '1', S)
        # 替换 false
        S = re.sub(r'\bfalse\b', '0', S)
        # 替换 NULL
        S = re.sub(r'\bNULL\b', '0', S)
        # 替换 va_list和其后的变量
        S = re.sub(r'\bva_list ([a-zA-Z_]+)[ ]*;',
                   'va_list \\1 = __va_area__;', S)
        # 删除 va_start
        S = re.sub(r'\bva_start\(([^)]*),([^)]*)\);',
                   '', S)
        # 替换 unreachable()
        S = re.sub(r'\bunreachable\(\)', 'error("unreachable")', S)
        # 替换 MIN宏
        S = re.sub(r'\bMIN\(([^)]*),([^)]*)\)', '((\\1)<(\\2)?(\\1):(\\2))', S)
        print(S)
