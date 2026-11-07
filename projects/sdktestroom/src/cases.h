/* the cases log through zx_log on the Spectrum, printf on the host */
#ifdef __SDCC
#include "zxlog.h"
#define CASE_LOG zx_log
#else
#include <stdio.h>
#define CASE_LOG printf
#endif
extern void cases(void);
