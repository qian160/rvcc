#include "test.h"

int main() {
  printf("[256] 支持_Generic\n");
  ASSERT(1, _Generic(100.0, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(2, _Generic((int *)0, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(2, _Generic((int[3]){}, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(3, _Generic(100, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(4, _Generic(100f, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(5, _Generic(100f, double : 1, int * : 2, int : 3, default : 5));

  printf("OK\n");
  return 0;
}
