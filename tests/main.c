#include "test.h"
#include <stdio.h>

int tests_passed = 0;
int tests_failed = 0;

void test_fuzzy(void);
void test_args(void);

int main(void) {
    printf("\nrunning swordfish tests\n");

    test_fuzzy();
    test_args();

    int total = tests_passed + tests_failed;
    if (tests_failed == 0) {
        printf("ok -- %d/%d tests passed\n\n", tests_passed, total);
        return 0;
    } else {
        printf("FAILED -- %d/%d tests passed, %d failed\n\n",
               tests_passed, total, tests_failed);
        return 1;
    }
}
