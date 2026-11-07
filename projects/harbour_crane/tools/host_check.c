/* The port's rules (src/hc_core.c on the SDK's fix16) against the
   original's tests (tools/orig/test_cases.c, its long autopilot game's
   hash in tools/orig/vectors.h), on the host:
     sh tools/check_core.sh */
#include <stdio.h>
#include "hc.h"

static int pass, fail;
static void check(int cond, const char *name)
{
    if (cond) pass++;
    else { fail++; printf("  FAIL %s\n", name); }
}

int main(void)
{
    run_tests(check);
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
