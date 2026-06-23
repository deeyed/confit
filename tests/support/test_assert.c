#include "test_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void confit_test_fail_at(const char *file, int line, const char *message) {
  fprintf(stderr, "%s:%d: test failure: %s\n", file, line,
          message != 0 ? message : "(null)");
  exit(1);
}

void confit_test_assert_true_at(const char *file, int line, int condition,
                                const char *expression) {
  if (!condition) {
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line,
            expression != 0 ? expression : "(null)");
    exit(1);
  }
}

void confit_test_assert_equal_int_at(const char *file, int line, int expected,
                                     int actual, const char *expression) {
  if (expected != actual) {
    fprintf(stderr, "%s:%d: assertion failed: %s == %d, got %d\n", file, line,
            expression != 0 ? expression : "(null)", expected, actual);
    exit(1);
  }
}

void confit_test_assert_contains_at(const char *file, int line,
                                    const char *text, const char *needle) {
  if (text == 0 || needle == 0 || strstr(text, needle) == 0) {
    fprintf(stderr, "%s:%d: expected text to contain: %s\n", file, line,
            needle != 0 ? needle : "(null)");
    exit(1);
  }
}

void confit_test_assert_not_contains_at(const char *file, int line,
                                        const char *text, const char *needle) {
  if (text != 0 && needle != 0 && strstr(text, needle) != 0) {
    fprintf(stderr, "%s:%d: expected text not to contain: %s\n", file, line,
            needle);
    exit(1);
  }
}
