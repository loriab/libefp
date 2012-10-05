#include "swf.h"

/* switching functions */

/*
 * swf(r) = 1 - 10 * D ^ 3 + 15 * D ^ 4 - 6 * D ^ 5
 *
 * where
 *
 * D = (r ^ 2 - start ^ 2) / (cutoff ^ 2 - start ^ 2)
 *
 * and
 *
 * start = 0.8 * cutoff
 */
double get_swf(double r, double cutoff)
{
	double start = 0.8 * cutoff;

	if (r < start)
		return 1.0;

	if (r > cutoff)
		return 0.0;

	double a = 1.0 / (cutoff * cutoff - start * start);
	double a3 = a * a * a;
	double a4 = a3 * a;
	double a5 = a4 * a;

	double b = r * r - start * start;
	double b3 = b * b * b;
	double b4 = b3 * b;
	double b5 = b4 * b;

	return 1.0 - 10.0 * a3 * b3 + 15.0 * a4 * b4 - 6.0 * a5 * b5;
}

/*
 * derivative d/dr of the above swf(r)
 */
double get_dswf(double r, double cutoff)
{
	double start = 0.8 * cutoff;

	if (r < start || r > cutoff)
		return 0.0;

	double a = 1.0 / (cutoff * cutoff - start * start);
	double a3 = a * a * a;
	double a4 = a3 * a;
	double a5 = a4 * a;

	double b = r * r - start * start;
	double b2 = b * b;
	double b3 = b2 * b;
	double b4 = b3 * b;

	return -60.0 * a3 * b2 + 120.0 * a4 * b3 - 60.0 * a5 * b4;
}
