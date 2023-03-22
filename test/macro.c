int assert(int expected, int actual, char *code);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int strcmp(char *p, char *q);
int memcmp(char *p, char *q, long n);

// [160] 支持 #include "..."
#include "include1.h"

// [159] 支持空指示
#

/* */ #

int main() {
  printf("[160] 支持 #include \"...\"");
  assert(5, include1, "include1");
  assert(7, include2, "include2");

  printf("[163] 支持 #if 和 #endif\n");
#if 0
#include "/no/such/file"
  assert(0, 1, "1");

  // [164] 在值为假的#if语句中，跳过嵌套的 #if 语句
  #if nested
  #endif
#endif

  int m = 0;

#if 1
  m = 5;
#endif
  assert(5, m, "m");

  printf("[165] 支持 #else");
#if 1
# if 0
#  if 1
    foo bar
#  endif
# endif
      m = 3;
#endif
    assert(3, m, "m");

#if 1-1
# if 1
# endif
# if 1
# else
# endif
# if 0
# else
# endif
  m = 2;
#else
# if 1
  m = 3;
# endif
#endif
  assert(3, m, "m");

#if 1
  m = 2;
#else
  m = 3;
#endif
  assert(2, m, "m");

  printf("[166] 支持 #elif\n");
#if 1
  m = 2;
#else
  m = 3;
#endif
  assert(2, m, "m");

#if 0
  m = 1;
#elif 0
  m = 2;
#elif 3 + 5
  m = 3;
#elif 1 * 5
  m = 4;
#endif
  assert(3, m, "m");

#if 1 + 5
  m = 1;
#elif 1
  m = 2;
#elif 3
  m = 2;
#endif
  assert(1, m, "m");

#if 0
  m = 1;
#elif 1
#if 1
  m = 2;
#else
  m = 3;
#endif
#else
  m = 5;
#endif
  assert(2, m, "m");

  printf("OK\n");
  return 0;
}
