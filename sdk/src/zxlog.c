/* zx_log: printf-style debug log (see include/zxlog.h). */
#include <stdarg.h>
#include <stdio.h>
#include "zxlog.h"

#ifndef ZX_NO_LOG
void zx_log(const char *fmt, ...)
{
    static char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    zx_log_s(buf);
}
#endif
