#include <stdio.h>
#include "mathutil.h"

long lpow(long x, long y) {
  long r = 1;
  
  while (y != 0) {
    if (y & 1) {
      r *= x;
    }
    y >>= 1;
    x *= x;
  }

  return r;
}
