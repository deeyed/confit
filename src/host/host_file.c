#include "confit/host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ConfitStatus confit_host_file_error(ConfitDiagnostic *diagnostic,
                                           const char *path,
                                           const char *message) {
  confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, path, 0, 0, message);
  return CONFIT_ERR_PARSE;
}

ConfitStatus confit_host_read_text_file(const char *path, char **out_text,
                                        size_t *out_size,
                                        ConfitDiagnostic *diagnostic) {
  FILE *file;
  long length;
  char *buffer;
  size_t bytes_read;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0, 0,
                          "missing output text pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_text = 0;
  if (out_size != 0) {
    *out_size = 0;
  }

  if (path == 0 || path[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0, 0,
                          "missing input path");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  file = fopen(path, "rb");
  if (file == 0) {
    return confit_host_file_error(diagnostic, path, "failed to open file");
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return confit_host_file_error(diagnostic, path, "failed to seek file");
  }

  length = ftell(file);
  if (length < 0) {
    fclose(file);
    return confit_host_file_error(diagnostic, path, "failed to measure file");
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return confit_host_file_error(diagnostic, path, "failed to rewind file");
  }

  buffer = (char *)malloc((size_t)length + 1U);
  if (buffer == 0) {
    fclose(file);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                          "failed to allocate file buffer");
    return CONFIT_ERR_INTERNAL;
  }

  bytes_read = fread(buffer, 1U, (size_t)length, file);
  if (bytes_read != (size_t)length) {
    free(buffer);
    fclose(file);
    return confit_host_file_error(diagnostic, path, "failed to read file");
  }

  if (fclose(file) != 0) {
    free(buffer);
    return confit_host_file_error(diagnostic, path, "failed to close file");
  }

  buffer[bytes_read] = '\0';
  *out_text = buffer;
  if (out_size != 0) {
    *out_size = bytes_read;
  }

  return CONFIT_OK;
}

ConfitStatus confit_host_write_text_file(const char *path, const char *text,
                                         ConfitDiagnostic *diagnostic) {
  FILE *file;
  size_t text_size;
  size_t bytes_written;

  if (path == 0 || path[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0, 0,
                          "missing output path");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0, 0,
                          "missing output text");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  file = fopen(path, "wb");
  if (file == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                          "failed to open output file");
    return CONFIT_ERR_GENERATION;
  }

  text_size = strlen(text);
  bytes_written = fwrite(text, 1U, text_size, file);
  if (bytes_written != text_size) {
    fclose(file);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                          "failed to write output file");
    return CONFIT_ERR_GENERATION;
  }

  if (fclose(file) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                          "failed to close output file");
    return CONFIT_ERR_GENERATION;
  }

  return CONFIT_OK;
}

void confit_host_free(void *allocation) { free(allocation); }
