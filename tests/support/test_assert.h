#ifndef CONFIT_TEST_ASSERT_H
#define CONFIT_TEST_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

void confit_test_fail_at(const char *file, int line, const char *message);
void confit_test_assert_true_at(const char *file, int line, int condition,
                                const char *expression);
void confit_test_assert_equal_int_at(const char *file, int line, int expected,
                                     int actual, const char *expression);
void confit_test_assert_contains_at(const char *file, int line,
                                    const char *text, const char *needle);
void confit_test_assert_not_contains_at(const char *file, int line,
                                        const char *text, const char *needle);

#define CONFIT_TEST_FAIL(message) \
  confit_test_fail_at(__FILE__, __LINE__, (message))

#define CONFIT_TEST_ASSERT(expression) \
  confit_test_assert_true_at(__FILE__, __LINE__, (expression), #expression)

#define CONFIT_TEST_ASSERT_EQ_INT(expected, actual) \
  confit_test_assert_equal_int_at(__FILE__, __LINE__, (expected), (actual), \
                                  #actual)

#define CONFIT_TEST_ASSERT_CONTAINS(text, needle) \
  confit_test_assert_contains_at(__FILE__, __LINE__, (text), (needle))

#define CONFIT_TEST_ASSERT_NOT_CONTAINS(text, needle) \
  confit_test_assert_not_contains_at(__FILE__, __LINE__, (text), (needle))

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_TEST_ASSERT_H */
