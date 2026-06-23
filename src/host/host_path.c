#include "confit/host.h"

#include <string.h>

char confit_host_path_separator(void) {
#if defined(_WIN32)
  return '\\';
#else
  return '/';
#endif
}

static int confit_host_is_path_separator(char value) {
  return value == '/' || value == '\\';
}

ConfitStatus confit_host_path_join(char *out, size_t out_size,
                                   const char *left, const char *right,
                                   ConfitDiagnostic *diagnostic) {
  const char separator = confit_host_path_separator();
  const size_t left_size = left != 0 ? strlen(left) : 0U;
  const size_t right_size = right != 0 ? strlen(right) : 0U;
  const int needs_separator =
      left_size > 0U && right_size > 0U &&
      !confit_host_is_path_separator(left[left_size - 1U]);
  const size_t total_size =
      left_size + right_size + (needs_separator ? 1U : 0U);
  size_t offset;

  if (out == 0 || out_size == 0U || left == 0 || right == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid path join argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  if (total_size + 1U > out_size) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "path join buffer is too small");
    out[0] = '\0';
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  offset = 0U;
  if (left_size > 0U) {
    memcpy(out + offset, left, left_size);
    offset += left_size;
  }

  if (needs_separator) {
    out[offset] = separator;
    offset += 1U;
  }

  if (right_size > 0U) {
    memcpy(out + offset, right, right_size);
    offset += right_size;
  }

  out[offset] = '\0';
  return CONFIT_OK;
}
