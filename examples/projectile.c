#include <stdio.h>
#include <math.h>

/* Projectile motion - and something to try the debugger on.

   F5 builds and runs it. F9 on a line puts a breakpoint there, F8 starts
   it and stops on that line, and F7 steps a line at a time - watch the
   Debug tab as rad, t_flight, h_max and range are worked out. */

#ifndef M_PI
#define M_PI 3.14   /* MSVC hides the real one behind _USE_MATH_DEFINES */
#endif

int main(void) {
    double v0 = 20.0;     // initial velocity (m/s)
    double angle = 45.0;  // launch angle (degrees)
    double g = 9.81;      // gravity (m/s^2)

    double rad = angle * M_PI / 180.0;
    double t_flight = 2 * v0 * sin(rad) / g;
    double h_max = (v0 * v0 * pow(sin(rad), 2)) / (2 * g);
    double range = (v0 * v0 * sin(2 * rad)) / g;

    printf("Time of flight: %.2f s\n", t_flight);
    printf("Max height: %.2f m\n", h_max);
    printf("Range: %.2f m\n", range);
    return 0;
}
