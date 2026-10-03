/**
 * @file test_main.c
 * @brief Test runner.
 */
#include <stdio.h>

#include "test_util.h"

int main(void) {
    printf("clipilot tests\n");

    suite_parse();
    suite_dispatch();
    suite_edit();
    suite_complete();

    printf("\n%d checks, %d failures\n", test_checks, test_failures);
    return (test_failures == 0) ? 0 : 1;
}
