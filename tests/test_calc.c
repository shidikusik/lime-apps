#include "calc_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void expect(const char *input, double expected)
{
    double value = 12345;
    char error[160];
    ++checks;
    if (!lime_calc_evaluate(input, &value, error, sizeof error) ||
        fabs(value - expected) > 1e-12 * fmax(fabs(expected), 1e-300)) {
        fprintf(stderr, "FAIL %s: got %.17g, expected %.17g; %s\n",
                input, value, expected, error);
        exit(EXIT_FAILURE);
    }
}

static void reject(const char *input)
{
    double value = 12345;
    char error[160];
    ++checks;
    if (lime_calc_evaluate(input, &value, error, sizeof error) ||
        !error[0] || value != 12345) {
        fprintf(stderr, "FAIL accepted invalid expression: %s\n", input ? input : "(null)");
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    expect("2+3*4", 14);
    expect("(2+3)*4", 20);
    expect("2^3^2", 512);
    expect("-2^2", -4);
    expect("(-2)^2", 4);
    expect("2^-2", .25);
    expect("2^-2^2", .0625);
    expect("  +3 + -2 ", 1);
    expect("1--2", 3);
    expect("sqrt(81)+sqrt(2^4)", 13);
    expect("200*15%", 30);
    expect("50%%", .005);
    expect("100+10%", 100.1);
    expect("pi*2", 6.283185307179586);
    expect("e^0", 1);
    expect(".5+1.", 1.5);
    expect("1.25e2 + 2E-2", 125.02);
    expect("0.000000000000000000001", 1e-21);
    expect("12345678901234567890", 1.2345678901234568e19);
    expect("0e99999", 0);
    expect("1.234567890123456789e-200", 1.2345678901234568e-200);
    expect(".0000000000000000000000123456789", 1.23456789e-23);
    expect("sqrt(0)", 0);
    expect("(-2)^-3", -.125);
    expect("4/2*3", 6);
    expect("5-3-1", 1);
    const char *invalid[] = {
        "", " ", ".", "1+", "1/0", "0/0", "1/(-0)", "1/(2-2)",
        "sqrt(-1)", "(-1)^.5", "1e309", "1e-99999", "10^1000",
        "2(3)", "1 2", "sqrt 4", "sqrt()", "(1+2", "1+2)",
        "foo(2)", "nan", "inf", "1e", "1e+", "1..2", "%2", "2**3",
        "1; echo bad", "$(id)", NULL
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) reject(invalid[i]);
    char nested[512];
    memset(nested, '(', 200);
    nested[200] = '1';
    memset(nested + 201, ')', 200);
    nested[401] = '\0';
    reject(nested);
    memset(nested, '-', 200);
    nested[200] = '1';
    nested[201] = '\0';
    reject(nested);
    char large[4100];
    memset(large, '1', sizeof large - 1);
    large[sizeof large - 1] = '\0';
    reject(large);
    char error[160];
    ++checks;
    if (lime_calc_evaluate("1", NULL, error, sizeof error)) return EXIT_FAILURE;
    ++checks;
    double result = 0;
    if (!lime_calc_evaluate("2+2", &result, NULL, 0) || result != 4) return EXIT_FAILURE;
    printf("LimeCalc: %u parser checks passed\n", checks);
    return EXIT_SUCCESS;
}
