#define __CRT__NO_INLINE
#define __declspec /* hacks */

#include <time.h>
#undef time

time_t time(time_t *tloc)
{
  time_t res;
  if(!tloc) *tloc=res;
  return res;
}

