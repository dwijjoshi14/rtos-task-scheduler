#pragma once
/*
 * test_framework.h — minimal self-registering test harness.
 *
 * No external dependency: cases register themselves with a constructor and the
 * runner in test_main.c executes them in order and reports a non-zero exit
 * status if anything failed, which is all CI needs.
 */

#include <stddef.h>
#include <string.h>

#define TEST_MAX_CASES 256

typedef void (*TestFn_t)(void);

typedef struct {
    const char* suite;
    const char* name;
    TestFn_t    fn;
} TestCase_t;

extern int g_checks;

void test_register(const char* suite, const char* name, TestFn_t fn);
void test_fail(const char* file, int line, const char* expr);
void test_fail_int(const char* file, int line, const char* expr,
                   long long actual, long long expected);
void test_fail_near(const char* file, int line, const char* expr,
                    double actual, double expected, double tolerance);

#define TEST_CASE(suite_name, case_name)                                        \
    static void suite_name##_##case_name(void);                                 \
    static void __attribute__((constructor))                                    \
    register_##suite_name##_##case_name(void) {                                 \
        test_register(#suite_name, #case_name, suite_name##_##case_name);       \
    }                                                                           \
    static void suite_name##_##case_name(void)

#define CHECK(cond)                                                             \
    do {                                                                        \
        g_checks++;                                                             \
        if (!(cond)) test_fail(__FILE__, __LINE__, #cond);                       \
    } while (0)

#define CHECK_INT(actual, expected)                                             \
    do {                                                                        \
        long long a_ = (long long)(actual);                                      \
        long long e_ = (long long)(expected);                                    \
        g_checks++;                                                             \
        if (a_ != e_) test_fail_int(__FILE__, __LINE__, #actual, a_, e_);        \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                  \
    do {                                                                        \
        double a_ = (double)(actual);                                            \
        double e_ = (double)(expected);                                          \
        double t_ = (double)(tolerance);                                         \
        double d_ = a_ - e_;                                                     \
        if (d_ < 0.0) d_ = -d_;                                                  \
        g_checks++;                                                             \
        if (!(d_ <= t_))                                                         \
            test_fail_near(__FILE__, __LINE__, #actual, a_, e_, t_);             \
    } while (0)

#define CHECK_STR(actual, expected)                                              \
    do {                                                                         \
        const char* a_ = (actual);                                               \
        const char* e_ = (expected);                                             \
        g_checks++;                                                             \
        if (!a_ || !e_ || strcmp(a_, e_) != 0)                                   \
            test_fail(__FILE__, __LINE__, #actual " == " #expected);             \
    } while (0)
