/*
 * test_main.c — test registry and runner.
 */

#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>

TestCase_t g_tests[TEST_MAX_CASES];
int        g_test_count    = 0;
int        g_checks        = 0;

static int s_case_failures = 0;
static int s_failures      = 0;
static int s_failed_cases  = 0;
static const char* s_current = "";

void test_register(const char* suite, const char* name, TestFn_t fn) {
    if (!suite || !name || !fn) return;
    if (g_test_count >= TEST_MAX_CASES) {
        fprintf(stderr, "test registry full (%d cases)\n", TEST_MAX_CASES);
        exit(2);
    }
    g_tests[g_test_count].suite = suite;
    g_tests[g_test_count].name  = name;
    g_tests[g_test_count].fn    = fn;
    g_test_count++;
}

static void report(const char* file, int line, const char* expr) {
    s_case_failures++;
    s_failures++;
    printf("         %s:%d: %s  [%s]\n", file, line, expr, s_current);
}

void test_fail(const char* file, int line, const char* expr) {
    report(file, line, expr);
}

void test_fail_int(const char* file, int line, const char* expr,
                   long long actual, long long expected) {
    s_case_failures++;
    s_failures++;
    printf("         %s:%d: %s  (actual %lld, expected %lld)  [%s]\n",
           file, line, expr, actual, expected, s_current);
}

void test_fail_near(const char* file, int line, const char* expr,
                    double actual, double expected, double tolerance) {
    s_case_failures++;
    s_failures++;
    printf("         %s:%d: %s  (actual %.6f, expected %.6f ± %.6f)  [%s]\n",
           file, line, expr, actual, expected, tolerance, s_current);
}

int main(void) {
    printf("\n  %d test cases\n\n", g_test_count);

    const char* last_suite = "";
    for (int i = 0; i < g_test_count; i++) {
        const TestCase_t* test = &g_tests[i];
        s_case_failures = 0;

        char label[160];
        snprintf(label, sizeof label, "%s::%s", test->suite, test->name);
        s_current = label;

        if (test->suite != last_suite) {
            printf("  %s\n", test->suite);
            last_suite = test->suite;
        }

        test->fn();

        if (s_case_failures == 0) {
            printf("    ok    %s\n", test->name);
        } else {
            printf("    FAIL  %s\n", test->name);
            s_failed_cases++;
        }
    }

    printf("\n  %d case(s), %d check(s), %d failure(s)\n",
           g_test_count, g_checks, s_failures);
    if (s_failures == 0) {
        printf("  PASS\n\n");
        return 0;
    }
    printf("  FAIL (%d case(s) with failures)\n\n", s_failed_cases);
    return 1;
}
