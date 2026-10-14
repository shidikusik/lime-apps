#ifndef LIME_CALC_ENGINE_H
#define LIME_CALC_ENGINE_H
#include <stdbool.h>
#include <stddef.h>

/* Evaluate ASCII math without invoking a shell or executing external code.
 * Values are doubles. Percent is postfix (50% == 0.5); powers associate right.
 * On failure, result is untouched and error receives a short explanation. */
bool lime_calc_evaluate(const char *expression, double *result,
                        char *error, size_t error_size);
#endif
