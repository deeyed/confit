#include "confit/input.h"

#include "confit/digest.h"
#include "input_internal.h"
#include "test_assert.h"
#include "test_fs.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_PATH_BYTES 4096U

typedef struct FailingAllocatorState {
  size_t attempt;
  size_t fail_at;
  size_t outstanding;
} FailingAllocatorState;

static void *failing_allocate(void *context, size_t size) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  void *pointer;
  if (state->attempt++ == state->fail_at) {
    return 0;
  }
  pointer = malloc(size);
  if (pointer != 0) {
    state->outstanding += 1U;
  }
  return pointer;
}

static void failing_deallocate(void *context, void *pointer) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  CONFIT_TEST_ASSERT(pointer != 0 && state->outstanding > 0U);
  state->outstanding -= 1U;
  free(pointer);
}

static void join_path(char *out, const char *root, const char *leaf) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, TEST_PATH_BYTES, root, leaf));
}

static void write_bytes(const char *path, const void *bytes, size_t size) {
  const unsigned char *source = (const unsigned char *)bytes;
  size_t offset = 0U;
  int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  CONFIT_TEST_ASSERT(descriptor >= 0);
  while (offset < size) {
    const ssize_t amount = write(descriptor, source + offset, size - offset);
    CONFIT_TEST_ASSERT(amount > 0);
    offset += (size_t)amount;
  }
  CONFIT_TEST_ASSERT(close(descriptor) == 0);
}

static int expect_string(const ConfitTomlValue *value, const char *expected) {
  const char *text = 0;
  size_t size = 0U;
  return confit_toml_value_string(value, &text, &size) &&
         size == strlen(expected) && memcmp(text, expected, size) == 0;
}

static void test_loaded_image(const char *root_path, ConfitHostRoot *root) {
  static const char source[] =
      "name = \"initial\"\n"
      "count = 7\n";
  char digest[65];
  char path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostFileIdentity identity;
  ConfitInputImage *image = 0;
  const ConfitTomlDocument *document;
  const ConfitTomlValue *value;
  const unsigned char *bytes;
  const char *count;
  size_t byte_size = 0U;
  size_t column = 0U;
  size_t line = 0U;
  size_t total = 123U;

  join_path(path, root_path, "valid.toml");
  write_bytes(path, source, sizeof(source) - 1U);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_input_load_toml(
                         root, "valid.toml", 0, &image,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(image != 0);
  CONFIT_TEST_ASSERT(strcmp(confit_input_image_path(image), "valid.toml") ==
                     0);
  bytes = confit_input_image_bytes(image, &byte_size);
  CONFIT_TEST_ASSERT(bytes != 0 && byte_size == sizeof(source) - 1U);
  CONFIT_TEST_ASSERT(memcmp(bytes, source, byte_size) == 0);
  confit_sha256_bytes(source, sizeof(source) - 1U, digest);
  CONFIT_TEST_ASSERT(strcmp(confit_input_image_digest(image), digest) == 0);
  CONFIT_TEST_ASSERT(confit_input_image_identity(image, &identity));
  CONFIT_TEST_ASSERT(identity.size == byte_size);
  CONFIT_TEST_ASSERT(confit_input_image_line_count(image) == 3U);
  count = strstr((const char *)bytes, "count");
  CONFIT_TEST_ASSERT(count != 0);
  CONFIT_TEST_ASSERT(confit_input_image_locate(
      image, (size_t)(count - (const char *)bytes), &line, &column));
  CONFIT_TEST_ASSERT(line == 2U && column == 1U);
  CONFIT_TEST_ASSERT(confit_input_image_locate(image, byte_size, &line,
                                               &column));
  CONFIT_TEST_ASSERT(line == 3U && column == 1U);
  line = 99U;
  column = 88U;
  CONFIT_TEST_ASSERT(!confit_input_image_locate(image, byte_size + 1U, &line,
                                                &column));
  CONFIT_TEST_ASSERT(line == 99U && column == 88U);

  document = confit_input_image_document(image);
  value = confit_toml_table_find(confit_toml_document_root(document), "name");
  CONFIT_TEST_ASSERT(expect_string(value, "initial"));
  CONFIT_TEST_ASSERT(confit_toml_document_source_text(document) ==
                     (const char *)bytes);
  CONFIT_TEST_ASSERT(confit_toml_document_source_size(document) == byte_size);

  CONFIT_TEST_ASSERT(confit_input_image_accumulate(
                         CONFIT_LIMIT_TOTAL_INPUT_BYTES - byte_size, image,
                         &total, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(total == CONFIT_LIMIT_TOTAL_INPUT_BYTES);
  CONFIT_TEST_ASSERT(confit_input_image_accumulate(
                         CONFIT_LIMIT_TOTAL_INPUT_BYTES - byte_size + 1U,
                         image, &total, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(total == CONFIT_LIMIT_TOTAL_INPUT_BYTES);
  confit_input_image_destroy(image);
}

static void test_absolute_loaded_image(const char *root_path) {
  static const char source[] = "value = 42\n";
  char path[TEST_PATH_BYTES];
  char symlink_path[TEST_PATH_BYTES];
  char invalid_path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitInputImage *image = 0;

  join_path(path, root_path, "absolute.toml");
  join_path(symlink_path, root_path, "absolute-link.toml");
  write_bytes(path, source, sizeof(source) - 1U);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_input_load_toml_absolute(
                         path, 0, &image, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(image != 0 &&
                     strcmp(confit_input_image_path(image), path) == 0);
  confit_input_image_destroy(image);
  image = 0;

  CONFIT_TEST_ASSERT(symlink(path, symlink_path) == 0);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_input_load_toml_absolute(
                         symlink_path, 0, &image,
                         &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(image == 0);

  CONFIT_TEST_ASSERT(snprintf(invalid_path, sizeof(invalid_path), "%s//bad.toml",
                              root_path) > 0);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_input_load_toml_absolute(
                         invalid_path, 0, &image,
                         &diagnostic) == CONFIT_ERR_USAGE);
  CONFIT_TEST_ASSERT(image == 0);
}

static void test_invalid_inputs(const char *root_path, ConfitHostRoot *root) {
  static const char malformed[] =
      "[table]\n"
      "  value = \"unterminated\n";
  static const unsigned char embedded_nul[] = {
      'a', ' ', '=', ' ', '1', '\n', 'b', ' ', '=', ' ', 0U, '2', '\n'};
  static const unsigned char invalid_utf8[] = {
      'a', ' ', '=', ' ', '"', 0xFFU, '"', '\n'};
  char path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitInputImage *image = 0;

  confit_diagnostic_init(&diagnostic);
  join_path(path, root_path, "nul.toml");
  write_bytes(path, embedded_nul, sizeof(embedded_nul));
  CONFIT_TEST_ASSERT(confit_input_load_toml(root, "nul.toml", 0, &image,
                                            &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(image == 0 && diagnostic.line == 2U &&
                     diagnostic.column == 5U);

  join_path(path, root_path, "utf8.toml");
  write_bytes(path, invalid_utf8, sizeof(invalid_utf8));
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_input_load_toml(root, "utf8.toml", 0, &image,
                                            &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(image == 0 && diagnostic.line == 1U &&
                     diagnostic.column == 6U);

  join_path(path, root_path, "malformed.toml");
  write_bytes(path, malformed, sizeof(malformed) - 1U);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_input_load_toml(
                         root, "malformed.toml", 0, &image,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(image == 0 && diagnostic.line == 3U &&
                     diagnostic.column == 1U);
}

static void test_empty_and_file_limit(const char *root_path,
                                      ConfitHostRoot *root) {
  char path[TEST_PATH_BYTES];
  unsigned char *maximum;
  ConfitDiagnostic diagnostic;
  ConfitInputImage *image = 0;
  size_t size = 0U;

  confit_diagnostic_init(&diagnostic);
  join_path(path, root_path, "empty.toml");
  write_bytes(path, "", 0U);
  CONFIT_TEST_ASSERT(confit_input_load_toml(root, "empty.toml", 0, &image,
                                            &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_input_image_bytes(image, &size) != 0 && size == 0U);
  CONFIT_TEST_ASSERT(strcmp(
      confit_input_image_digest(image),
      "e3b0c44298fc1c149afbf4c8996fb924"
      "27ae41e4649b934ca495991b7852b855") == 0);
  CONFIT_TEST_ASSERT(confit_input_image_line_count(image) == 1U);
  confit_input_image_destroy(image);
  image = 0;

  maximum = (unsigned char *)malloc(CONFIT_LIMIT_TOML_FILE_BYTES + 1U);
  CONFIT_TEST_ASSERT(maximum != 0);
  memset(maximum, 'a', CONFIT_LIMIT_TOML_FILE_BYTES + 1U);
  maximum[0] = '#';
  maximum[CONFIT_LIMIT_TOML_FILE_BYTES - 1U] = '\n';
  join_path(path, root_path, "maximum.toml");
  write_bytes(path, maximum, CONFIT_LIMIT_TOML_FILE_BYTES);
  CONFIT_TEST_ASSERT(confit_input_load_toml(root, "maximum.toml", 0, &image,
                                            &diagnostic) == CONFIT_OK);
  confit_input_image_destroy(image);
  image = 0;

  join_path(path, root_path, "over.toml");
  write_bytes(path, maximum, CONFIT_LIMIT_TOML_FILE_BYTES + 1U);
  CONFIT_TEST_ASSERT(confit_input_load_toml(root, "over.toml", 0, &image,
                                            &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(image == 0);
  free(maximum);
}

static void test_replaced_path_same_image(const char *root_path,
                                          ConfitHostRoot *root) {
  static const char initial[] = "value = \"initial\"\n";
  static const char replacement[] = "value = \"replacement\"\n";
  char digest[65];
  char path[TEST_PATH_BYTES];
  char replacement_path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostBuffer buffer;
  ConfitHostFile *file = 0;
  ConfitInputImage *initial_image = 0;
  ConfitInputImage *replacement_image = 0;
  const ConfitTomlValue *value;

  confit_diagnostic_init(&diagnostic);
  confit_host_buffer_init(&buffer);
  join_path(path, root_path, "race.toml");
  join_path(replacement_path, root_path, "race-new.toml");
  write_bytes(path, initial, sizeof(initial) - 1U);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "race.toml", &file,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_file_read(file, CONFIT_LIMIT_TOML_FILE_BYTES,
                                           0, &buffer,
                                           &diagnostic) == CONFIT_OK);
  confit_host_file_destroy(file);
  write_bytes(replacement_path, replacement, sizeof(replacement) - 1U);
  CONFIT_TEST_ASSERT(rename(replacement_path, path) == 0);

  CONFIT_TEST_ASSERT(confit_input_image_from_host_buffer(
                         "race.toml", &buffer, 0, &initial_image,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(buffer.bytes == 0 && buffer.size == 0U);
  value = confit_toml_table_find(
      confit_toml_document_root(confit_input_image_document(initial_image)),
      "value");
  CONFIT_TEST_ASSERT(expect_string(value, "initial"));
  confit_sha256_bytes(initial, sizeof(initial) - 1U, digest);
  CONFIT_TEST_ASSERT(strcmp(confit_input_image_digest(initial_image), digest) ==
                     0);

  CONFIT_TEST_ASSERT(confit_input_load_toml(
                         root, "race.toml", 0, &replacement_image,
                         &diagnostic) == CONFIT_OK);
  value = confit_toml_table_find(
      confit_toml_document_root(
          confit_input_image_document(replacement_image)),
      "value");
  CONFIT_TEST_ASSERT(expect_string(value, "replacement"));
  CONFIT_TEST_ASSERT(strcmp(confit_input_image_digest(initial_image),
                            confit_input_image_digest(replacement_image)) != 0);
  confit_input_image_destroy(replacement_image);
  confit_input_image_destroy(initial_image);
  confit_host_buffer_destroy(&buffer);
}

static void test_transactional_allocation_failure(const char *root_path,
                                                  ConfitHostRoot *root) {
  static const char source[] = "value = 1\n";
  char path[TEST_PATH_BYTES];
  ConfitAllocator allocator;
  ConfitDiagnostic diagnostic;
  ConfitHostBuffer buffer;
  ConfitHostFile *file = 0;
  ConfitInputImage *image = 0;
  FailingAllocatorState state;
  size_t fail_at;

  join_path(path, root_path, "allocation.toml");
  write_bytes(path, source, sizeof(source) - 1U);
  confit_diagnostic_init(&diagnostic);
  confit_host_buffer_init(&buffer);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "allocation.toml", &file,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_file_read(file, CONFIT_LIMIT_TOML_FILE_BYTES,
                                           0, &buffer,
                                           &diagnostic) == CONFIT_OK);
  confit_host_file_destroy(file);

  allocator.context = &state;
  allocator.allocate = failing_allocate;
  allocator.deallocate = failing_deallocate;
  for (fail_at = 0U; fail_at < 3U; ++fail_at) {
    memset(&state, 0, sizeof(state));
    state.fail_at = fail_at;
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_input_image_from_host_buffer(
                           "allocation.toml", &buffer, &allocator, &image,
                           &diagnostic) == CONFIT_ERR_INTERNAL);
    CONFIT_TEST_ASSERT(image == 0 && buffer.bytes != 0 &&
                       buffer.size == sizeof(source) - 1U &&
                       state.outstanding == 0U);
  }
  CONFIT_TEST_ASSERT(confit_input_image_from_host_buffer(
                         "allocation.toml", &buffer, 0, &image,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(buffer.bytes == 0);
  confit_input_image_destroy(image);
  confit_host_buffer_destroy(&buffer);
}

int main(void) {
  char raw_root[TEST_PATH_BYTES];
  char root_path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostRoot *root = 0;

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(raw_root, sizeof(raw_root),
                                                  "confit-r06"));
  CONFIT_TEST_ASSERT(realpath(raw_root, root_path) != 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         root_path, 0, &root, &diagnostic) == CONFIT_OK);
  test_loaded_image(root_path, root);
  test_absolute_loaded_image(root_path);
  test_invalid_inputs(root_path, root);
  test_empty_and_file_limit(root_path, root);
  test_replaced_path_same_image(root_path, root);
  test_transactional_allocation_failure(root_path, root);
  confit_host_root_destroy(root);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
  return 0;
}
