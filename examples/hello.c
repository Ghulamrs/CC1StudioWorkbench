/* The C example, for cc1 - which is what the editor was written to drive.
   Ctrl-B builds it, Ctrl-T changes which architecture the assembly is for. */

#include <stdio.h>

static int factorial(int n)
{
    if (n < 2)
        return 1;
    return n * factorial(n - 1);
}

int main(void)
{
    switch (factorial(5)) {
    case 120:
        puts("as expected");
        break;
    default:
        puts("something is wrong");
    }
    return 0;
}
