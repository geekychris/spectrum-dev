/* host_main.c - runs the original's run_tests() (tools/host_test.sh) */
#include <stdio.h>
#include "bb.h"

static int pass, fail;

static void check(int cond, const char *name)
{
    if (cond) pass++;
    else { fail++; printf("FAIL %s\n", name); }
}

int main(void)
{
    run_tests(check);
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
