#include "confit/host.h"

#include <stdio.h>

static ConfitStatus confit_host_write_to(FILE *stream, const char *text) {
  if (stream == 0 || text == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  if (fputs(text, stream) == EOF) {
    return CONFIT_ERR_INTERNAL;
  }

  return CONFIT_OK;
}

static ConfitStatus confit_host_write_line_to(FILE *stream, const char *text) {
  ConfitStatus status;

  status = confit_host_write_to(stream, text);
  if (status != CONFIT_OK) {
    return status;
  }

  return confit_host_write_to(stream, "\n");
}

ConfitStatus confit_host_stdout_write(const char *text) {
  return confit_host_write_to(stdout, text);
}

ConfitStatus confit_host_stdout_write_line(const char *text) {
  return confit_host_write_line_to(stdout, text);
}

ConfitStatus confit_host_stderr_write(const char *text) {
  return confit_host_write_to(stderr, text);
}

ConfitStatus confit_host_stderr_write_line(const char *text) {
  return confit_host_write_line_to(stderr, text);
}
