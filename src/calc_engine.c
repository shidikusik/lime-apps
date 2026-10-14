#include "calc_engine.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_EXPRESSION 4096
#define MAX_DEPTH 128

struct parser {
    const char *start;
    const char *cursor;
    const char *error;
    unsigned depth;
};

static double expression(struct parser *p);
static double unary(struct parser *p);

static void fail(struct parser *p, const char *message)
{
    if (!p->error) p->error = message;
}

static void spaces(struct parser *p)
{
    while (isspace((unsigned char)*p->cursor)) ++p->cursor;
}

static int consume(struct parser *p, char token)
{
    spaces(p);
    if (*p->cursor != token) return 0;
    ++p->cursor;
    return 1;
}

static double finite_value(struct parser *p, double value)
{
    if (!isfinite(value)) fail(p, "Result is not a real, finite number");
    return value;
}

/* Parse a period decimal independently of the process/desktop locale. Keep
 * eighteen significant digits and account for the remainder in the exponent. */
static double number(struct parser *p)
{
    long double mantissa = 0.0L;
    unsigned significant = 0;
    int exponent = 0, fractional = 0, seen_digit = 0;
    int after_point = 0;
    while (1) {
        unsigned char c = (unsigned char)*p->cursor;
        if (c >= '0' && c <= '9') {
            seen_digit = 1;
            if (after_point) ++fractional;
            if (significant || c != '0') {
                if (significant < 18) {
                    mantissa = mantissa * 10.0L + (c - '0');
                    ++significant;
                } else {
                    ++exponent;
                }
            }
            ++p->cursor;
        } else if (c == '.' && !after_point) {
            after_point = 1;
            ++p->cursor;
        } else break;
    }
    if (!seen_digit) {
        fail(p, "Expected a number");
        return 0;
    }
    exponent -= fractional;
    if (*p->cursor == 'e' || *p->cursor == 'E') {
        ++p->cursor;
        int sign = 1, explicit_exponent = 0;
        if (*p->cursor == '+' || *p->cursor == '-') {
            if (*p->cursor == '-') sign = -1;
            ++p->cursor;
        }
        if (!isdigit((unsigned char)*p->cursor)) {
            fail(p, "Expected digits after the exponent");
            return 0;
        }
        while (isdigit((unsigned char)*p->cursor)) {
            if (explicit_exponent < 100000)
                explicit_exponent = explicit_exponent * 10 + (*p->cursor - '0');
            ++p->cursor;
        }
        exponent += sign * explicit_exponent;
    }
    if (mantissa == 0.0L) return 0;
    double value = (double)(mantissa * powl(10.0L, exponent));
    if (value == 0.0) fail(p, "Number is too small to represent");
    return finite_value(p, value);
}

static double primary(struct parser *p)
{
    spaces(p);
    if (consume(p, '(')) {
        double value = expression(p);
        if (!consume(p, ')')) fail(p, "Expected a closing parenthesis");
        return value;
    }
    if (isalpha((unsigned char)*p->cursor)) {
        const char *name = p->cursor;
        while (isalpha((unsigned char)*p->cursor)) ++p->cursor;
        size_t length = (size_t)(p->cursor - name);
        if (length == 2 && strncmp(name, "pi", 2) == 0)
            return 3.14159265358979323846;
        if (length == 1 && *name == 'e')
            return 2.71828182845904523536;
        if (length == 4 && strncmp(name, "sqrt", 4) == 0) {
            if (!consume(p, '(')) {
                fail(p, "Use sqrt(number)");
                return 0;
            }
            double value = expression(p);
            if (!consume(p, ')')) fail(p, "Expected a closing parenthesis");
            if (value < 0) {
                fail(p, "Square root needs a nonnegative number");
                return 0;
            }
            return finite_value(p, sqrt(value));
        }
        fail(p, "Unknown name; use pi, e, or sqrt");
        return 0;
    }
    return number(p);
}

static double power(struct parser *p)
{
    double value = primary(p);
    while (!p->error && consume(p, '%')) value /= 100.0;
    if (!p->error && consume(p, '^')) value = finite_value(p, pow(value, unary(p)));
    return value;
}

static double unary(struct parser *p)
{
    if (++p->depth > MAX_DEPTH) {
        fail(p, "Expression is nested too deeply");
        --p->depth;
        return 0;
    }
    double value;
    if (consume(p, '+')) value = unary(p);
    else if (consume(p, '-')) value = -unary(p);
    else value = power(p);
    --p->depth;
    return value;
}

static double term(struct parser *p)
{
    double value = unary(p);
    while (!p->error) {
        if (consume(p, '*')) value = finite_value(p, value * unary(p));
        else if (consume(p, '/')) {
            double divisor = unary(p);
            if (divisor == 0.0) fail(p, "Cannot divide by zero");
            else value = finite_value(p, value / divisor);
        } else break;
    }
    return value;
}

static double expression(struct parser *p)
{
    double value = term(p);
    while (!p->error) {
        if (consume(p, '+')) value = finite_value(p, value + term(p));
        else if (consume(p, '-')) value = finite_value(p, value - term(p));
        else break;
    }
    return value;
}

bool lime_calc_evaluate(const char *input, double *result,
                        char *error, size_t error_size)
{
    if (error && error_size) error[0] = '\0';
    if (!input || !result) {
        if (error && error_size) snprintf(error, error_size, "Missing expression or result");
        return false;
    }
    /* A bounded scan also avoids traversing arbitrarily long CLI input. */
    size_t length = 0;
    while (length <= MAX_EXPRESSION && input[length]) ++length;
    if (length > MAX_EXPRESSION) {
        if (error && error_size) snprintf(error, error_size, "Expression exceeds 4096 characters");
        return false;
    }
    struct parser p = { input, input, NULL, 0 };
    double value = expression(&p);
    spaces(&p);
    if (!p.error && *p.cursor) fail(&p, "Unexpected character");
    if (!p.error) finite_value(&p, value);
    if (p.error) {
        if (error && error_size)
            snprintf(error, error_size, "%s (column %zu)", p.error,
                     (size_t)(p.cursor - p.start) + 1);
        return false;
    }
    *result = value == 0.0 ? 0.0 : value;
    return true;
}
