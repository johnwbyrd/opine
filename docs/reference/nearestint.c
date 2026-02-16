/*
 * $Id: nearestint.c 70576 2014-06-22 19:31:05Z vinc17/xvii $
 *
 * Test the nearest integer functions (ISO/IEC 9899:1999, 7.12.9 and F.9.6).
 * For instance, compile with:
 *   gcc -Wall -O2 -std=c99 nearestint.c -o nearestint -lm
 *
 * This test showed a glibc bug on the PowerPC (fixed on 2005-01-06):
 *   https://www.sourceware.org/bugzilla/show_bug.cgi?id=602
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fenv.h>

static double bound, step;

static double myrint (double x)
{
  static const double TWO52 = 4503599627370496.0;

  if (fabs (x) < TWO52)
    {
      if (x > 0.0)
        {
          x += TWO52;
          x -= TWO52;
          if (x == 0.0)
            x = 0.0;
        }
      else if (x < 0.0)
        {
          x -= TWO52;
          x += TWO52;
          if (x == 0.0)
            x = -0.0;
        }
    }

  return x;
}

#define PRNT(R, F, S) do \
  { \
    double x; \
    printf ("%-11s", S); \
    for (x = - bound; x <= bound; x += step) \
      if (x == 0) \
        { \
          printf (R ? "%6.1f" : "%4d  ", F(-0.0)); \
          printf (R ? "%6.1f" : "%4d  ", F(+0.0)); \
        } \
      else \
        { \
          printf (R ? "%6.1f" : "%4d  ", F(x)); \
        } \
    printf ("\n"); \
  } \
  while (0)

#define TEST(R, F) PRNT (R, F, #F)

void tstall (void)
{
  PRNT (1, , "");
  PRNT (0, (int), "casttoint");
  TEST (1, trunc);
  TEST (1, floor);
  TEST (1, ceil);
  TEST (1, round);
  TEST (1, nearbyint);
  TEST (1, myrint);
  TEST (1, rint);
}

int main (int argc, char *argv[])
{
  if (argc != 3)
    {
      fprintf (stderr, "Usage: nearestint <bound> <step>\n");
      exit (1);
    }

  bound = atof (argv[1]);
  if (bound < 0)
    {
      fprintf (stderr, "nearestint: bound must not be negative\n");
      exit (1);
    }

  step = atof (argv[2]);
  if (step <= 0)
    {
      fprintf (stderr, "nearestint: step must be positive\n");
      exit (1);
    }

  printf ("Test of the nearest integer functions "
          "(ISO/IEC 9899:1999, 7.12.9 and F.9.6).\n"
          "__STDC_VERSION__ "
#ifdef __STDC_VERSION__
          "= %lu.\n", __STDC_VERSION__
#else
          "undefined.\n"
#endif
          );

  printf ("\nRounding to nearest\n");
  if (fesetround (FE_TONEAREST))
    printf ("Error\n");
  else
    tstall ();

  printf ("\nRounding toward 0\n");
  if (fesetround (FE_TOWARDZERO))
    printf ("Error\n");
  else
    tstall ();

  printf ("\nRounding toward -oo\n");
  if (fesetround (FE_DOWNWARD))
    printf ("Error\n");
  else
    tstall ();

  printf ("\nRounding toward +oo\n");
  if (fesetround (FE_UPWARD))
    printf ("Error\n");
  else
    tstall ();

  return 0;
}
