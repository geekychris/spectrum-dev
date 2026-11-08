/* Host builds of the original's C (tools/orig): its `long` is 32 bits, as on
   the 3DO, the Amiga and the Z80 (zsdcc). The system headers come first, then
   long becomes int for the original's files. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define long int
