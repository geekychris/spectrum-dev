/* the host build of the cases (test.py compiles it; not part of the .tap) */
#ifndef __SDCC
#include "cases.h"
int main(void) { cases(); return 0; }
#else
typedef int host_main_only;   /* not an empty file for zsdcc */
#endif
