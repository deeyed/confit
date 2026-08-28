#include "confit/schema.h"

#include "test_assert.h"
#include "test_fs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_PATH_BYTES 4096U
#define TEST_DECL_BYTES 16384U

static void join_path(char *out, const char *root, const char *relative) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, TEST_PATH_BYTES, root, relative));
}

static void write_text(const char *root, const char *relative,
                       const char *text) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static void write_one(const char *root, const char *type,
                      const char *fields) {
  char text[TEST_DECL_BYTES];
  const int length = snprintf(text, sizeof(text),
                              "[[config]]\n"
                              "symbol = \"OPTION\"\n"
                              "type = \"%s\"\n"
                              "prompt = \"Option\"\n"
                              "help = \"Exercise one typed option.\"\n"
                              "%s",
                              type, fields);
  CONFIT_TEST_ASSERT(length > 0 && (size_t)length < sizeof(text));
  write_text(root, "types/leaf.toml", text);
}

static void expect_failure(ConfitHostRoot *host, const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitSchemaProject *project = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         host, "types/Confit.toml", 0, &project,
                         &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(project == 0 && diagnostic.message != 0);
  if (message != 0 && strcmp(diagnostic.message, message) != 0) {
    fprintf(stderr, "expected type error '%s', actual '%s' at %s:%zu:%zu\n",
            message, diagnostic.message,
            diagnostic.path != 0 ? diagnostic.path : "(null)",
            diagnostic.line, diagnostic.column);
  }
  if (message != 0)
    CONFIT_TEST_ASSERT(strcmp(diagnostic.message, message) == 0);
  CONFIT_TEST_ASSERT(diagnostic.path != 0 &&
                     strcmp(diagnostic.path, "types/leaf.toml") == 0);
}

static ConfitSchemaProject *load_one(ConfitHostRoot *host) {
  ConfitDiagnostic diagnostic;
  ConfitSchemaProject *project = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         host, "types/Confit.toml", 0, &project,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(project != 0 &&
                     confit_schema_project_config_count(project) == 1U);
  return project;
}

static void assert_canonical(const ConfitValue *value, const char *expected) {
  ConfitDiagnostic diagnostic;
  char buffer[256];
  size_t size = 0U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_value_format_canonical(
                         value, buffer, sizeof(buffer), &size,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(size == strlen(expected) &&
                     strcmp(buffer, expected) == 0);
}

static void test_valid_types(const char *root, ConfitHostRoot *host) {
  ConfitSchemaProject *project;
  ConfitSchemaConfigView view;

  write_one(root, "bool", "");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.kind == CONFIT_VALUE_BOOL &&
                     view.default_value->data.boolean == 0 && !view.has_range);
  assert_canonical(view.default_value, "bool:false");
  confit_schema_project_destroy(project);

  write_one(root, "bool", "default = true\n");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.default_value->data.boolean == 1);
  assert_canonical(view.default_value, "bool:true");
  confit_schema_project_destroy(project);

  write_one(root, "int", "");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.default_value->data.integer == INT64_C(0));
  assert_canonical(view.default_value, "int:0");
  confit_schema_project_destroy(project);

  write_one(root, "int",
            "default = -12\nrange = { min = -9223372036854775808, max = 64 }\n");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.kind == CONFIT_VALUE_INT && view.has_range &&
                     view.default_value->data.integer == INT64_C(-12) &&
                     view.range_minimum->data.integer == INT64_MIN &&
                     view.range_maximum->data.integer == INT64_C(64));
  assert_canonical(view.default_value, "int:-12");
  confit_schema_project_destroy(project);

  write_one(root, "int", "default = 9223372036854775807\n");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.default_value->data.integer == INT64_MAX);
  assert_canonical(view.default_value, "int:9223372036854775807");
  confit_schema_project_destroy(project);

  write_one(root, "hex", "");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.default_value->data.hexadecimal == UINT64_C(0));
  assert_canonical(view.default_value, "hex:0x0");
  confit_schema_project_destroy(project);

  write_one(root, "hex",
            "default = 0x7fff_ffff_ffff_ffff\n"
            "range = { min = 0x0, max = 0x7fff_ffff_ffff_ffff }\n");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.kind == CONFIT_VALUE_HEX && view.has_range &&
                     view.default_value->data.hexadecimal ==
                         (uint64_t)INT64_MAX &&
                     view.range_minimum->data.hexadecimal == UINT64_C(0));
  assert_canonical(view.default_value, "hex:0x7fffffffffffffff");
  confit_schema_project_destroy(project);

  write_one(root, "string", "");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.kind == CONFIT_VALUE_STRING &&
                     view.default_value->data.text.size == 0U);
  assert_canonical(view.default_value, "string:0:");
  confit_schema_project_destroy(project);

  write_one(root, "enum",
            "values = [\"quiet\", \"normal\", \"verbose\"]\n"
            "default = \"normal\"\n");
  project = load_one(host);
  CONFIT_TEST_ASSERT(confit_schema_project_config_at(project, 0U, &view));
  CONFIT_TEST_ASSERT(view.kind == CONFIT_VALUE_ENUM &&
                     view.enum_value_count == 3U &&
                     strcmp(view.enum_values[2], "verbose") == 0);
  assert_canonical(view.default_value, "enum:6:normal");
  confit_schema_project_destroy(project);
}

static void test_wrong_native_types(const char *root, ConfitHostRoot *host) {
  write_one(root, "bool", "default = 1\n");
  expect_failure(host, "bool default must be a native TOML boolean");
  write_one(root, "int", "default = \"12\"\n");
  expect_failure(host, "int default must be a native TOML integer");
  write_one(root, "hex", "default = \"0x10\"\n");
  expect_failure(host,
                 "hex default must use native nonnegative TOML hexadecimal spelling");
  write_one(root, "hex", "default = 16\n");
  expect_failure(host,
                 "hex default must use native nonnegative TOML hexadecimal spelling");
  write_one(root, "hex", "default = -0x1\n");
  expect_failure(host, 0);
  write_one(root, "string", "default = true\n");
  expect_failure(host, "string default must be a bounded safe TOML string");
  write_one(root, "enum", "values = [\"yes\"]\ndefault = true\n");
  expect_failure(host, "enum default must be a member of its string atom domain");

  write_one(root, "int", "default = 9223372036854775808\n");
  expect_failure(host, 0);
  write_one(root, "hex", "default = 0x8000_0000_0000_0000\n");
  expect_failure(host, 0);
}

static void test_field_applicability(const char *root,
                                     ConfitHostRoot *host) {
  static const char *const scalar_types[] = {"bool", "int", "hex", "string"};
  size_t index;
  for (index = 0U; index < sizeof(scalar_types) / sizeof(scalar_types[0]);
       ++index) {
    write_one(root, scalar_types[index], "values = [\"x\"]\n");
    expect_failure(host, "values is valid only for enum");
  }
  write_one(root, "bool", "range = { min = false, max = true }\n");
  expect_failure(host, "range is valid only for int and hex");
  write_one(root, "string", "range = { min = \"a\", max = \"z\" }\n");
  expect_failure(host, "range is valid only for int and hex");
  write_one(root, "enum",
            "values = [\"x\"]\ndefault = \"x\"\n"
            "range = { min = \"x\", max = \"x\" }\n");
  expect_failure(host, "range is valid only for int and hex");
}

static void test_ranges(const char *root, ConfitHostRoot *host) {
  write_one(root, "int", "default = 4\nrange = { min = 8, max = 1 }\n");
  expect_failure(host, "range min must not exceed max");
  write_one(root, "int", "default = 9\nrange = { min = 1, max = 8 }\n");
  expect_failure(host, "default must lie within the inclusive range");
  write_one(root, "int", "range = { min = 1 }\n");
  expect_failure(host, "range must contain exactly min and max");
  write_one(root, "int", "range = { min = 0, max = 8, step = 1 }\n");
  expect_failure(host, "range must contain exactly min and max");
  write_one(root, "hex",
            "default = 0x10\nrange = { min = 0, max = 0xff }\n");
  expect_failure(host, "range min must use the declared native TOML type");
}

static void test_enum_domains(const char *root, ConfitHostRoot *host) {
  write_one(root, "enum", "default = \"x\"\n");
  expect_failure(host, "enum requires a values array");
  write_one(root, "enum", "values = [\"x\"]\n");
  expect_failure(host, "enum requires an explicit default");
  write_one(root, "enum", "values = []\ndefault = \"x\"\n");
  expect_failure(host,
                 "enum values must be a nonempty bounded array of unique string atoms");
  write_one(root, "enum",
            "values = [\"x\", \"x\"]\ndefault = \"x\"\n");
  expect_failure(host,
                 "enum values must be a nonempty bounded array of unique string atoms");
  write_one(root, "enum",
            "values = [\"bad value\", \"good\"]\ndefault = \"good\"\n");
  expect_failure(host,
                 "enum values must be a nonempty bounded array of unique string atoms");
  write_one(root, "enum", "values = [\"x\"]\ndefault = \"y\"\n");
  expect_failure(host,
                 "enum default must be a member of its string atom domain");
  write_one(root, "enum", "values = [{ label = \"x\" }]\ndefault = \"x\"\n");
  expect_failure(host,
                 "enum values must be a nonempty bounded array of unique string atoms");
}

static void test_string_and_domain_bounds(const char *root,
                                          ConfitHostRoot *host) {
  static const char *const excluded[] = {
      "tristate", "placement", "uint", "float", "path", "file",
      "directory", "object", "target", "driver", "module"};
  char fields[TEST_DECL_BYTES];
  ConfitSchemaProject *project;
  size_t index;

  write_one(root, "string", "default = \"bad\\u001bvalue\"\n");
  expect_failure(host, "string default must be a bounded safe TOML string");
  write_one(root, "string", "default = \"bad\\u0000value\"\n");
  expect_failure(host, "string default must be a bounded safe TOML string");
  fields[0] = '\0';
  CONFIT_TEST_ASSERT(strlen("default = \"") + CONFIT_LIMIT_STRING_BYTES + 2U <
                     sizeof(fields));
  memcpy(fields, "default = \"", strlen("default = \""));
  memset(fields + strlen("default = \""), 'a', CONFIT_LIMIT_STRING_BYTES);
  memcpy(fields + strlen("default = \"") + CONFIT_LIMIT_STRING_BYTES,
         "\"\n", 3U);
  write_one(root, "string", fields);
  project = load_one(host);
  confit_schema_project_destroy(project);

  fields[0] = '\0';
  CONFIT_TEST_ASSERT(strlen("default = \"") + CONFIT_LIMIT_STRING_BYTES + 3U <
                     sizeof(fields));
  memcpy(fields, "default = \"", strlen("default = \""));
  memset(fields + strlen("default = \""), 'a', CONFIT_LIMIT_STRING_BYTES + 1U);
  memcpy(fields + strlen("default = \"") + CONFIT_LIMIT_STRING_BYTES + 1U,
         "\"\n", 3U);
  write_one(root, "string", fields);
  expect_failure(host, "string default must be a bounded safe TOML string");

  {
    size_t pass;
    for (pass = 0U; pass < 2U; ++pass) {
      const size_t value_count = CONFIT_LIMIT_ENUM_VALUES + pass;
      size_t cursor = 0U;
      const size_t prefix_size = strlen("values = [");
      memcpy(fields + cursor, "values = [", prefix_size);
      cursor += prefix_size;
      for (index = 0U; index < value_count; ++index) {
        const int written = snprintf(fields + cursor, sizeof(fields) - cursor,
                                     "%s\"v%zu\"",
                                     index == 0U ? "" : ",", index);
        CONFIT_TEST_ASSERT(written > 0 &&
                           (size_t)written < sizeof(fields) - cursor);
        cursor += (size_t)written;
      }
      CONFIT_TEST_ASSERT(cursor + strlen("]\ndefault = \"v0\"\n") + 1U <
                         sizeof(fields));
      memcpy(fields + cursor, "]\ndefault = \"v0\"\n",
             strlen("]\ndefault = \"v0\"\n") + 1U);
      write_one(root, "enum", fields);
      if (pass == 0U) {
        project = load_one(host);
        confit_schema_project_destroy(project);
      } else {
        expect_failure(
            host,
            "enum values must be a nonempty bounded array of unique string atoms");
      }
    }
  }

  {
    const size_t prefix_size = strlen("values = [\"");
    memcpy(fields, "values = [\"", prefix_size);
    memset(fields + prefix_size, 'a', CONFIT_LIMIT_ENUM_ATOM_BYTES);
    memcpy(fields + prefix_size + CONFIT_LIMIT_ENUM_ATOM_BYTES,
           "\"]\ndefault = \"", strlen("\"]\ndefault = \""));
    memset(fields + prefix_size + CONFIT_LIMIT_ENUM_ATOM_BYTES +
               strlen("\"]\ndefault = \""),
           'a', CONFIT_LIMIT_ENUM_ATOM_BYTES);
    memcpy(fields + prefix_size + CONFIT_LIMIT_ENUM_ATOM_BYTES +
               strlen("\"]\ndefault = \"") + CONFIT_LIMIT_ENUM_ATOM_BYTES,
           "\"\n", 3U);
    write_one(root, "enum", fields);
    project = load_one(host);
    confit_schema_project_destroy(project);
  }

  {
    char atom[CONFIT_LIMIT_ENUM_ATOM_BYTES + 2U];
    memset(atom, 'a', CONFIT_LIMIT_ENUM_ATOM_BYTES + 1U);
    atom[CONFIT_LIMIT_ENUM_ATOM_BYTES + 1U] = '\0';
    CONFIT_TEST_ASSERT(snprintf(fields, sizeof(fields),
                                "values = [\"%s\", \"good\"]\n"
                                "default = \"good\"\n",
                                atom) > 0);
    write_one(root, "enum", fields);
    expect_failure(host,
                   "enum values must be a nonempty bounded array of unique string atoms");
  }

  for (index = 0U; index < sizeof(excluded) / sizeof(excluded[0]); ++index) {
    write_one(root, excluded[index], "");
    expect_failure(host,
                   "configuration type must be bool, int, hex, string, or enum");
  }
}

int main(void) {
  char raw_root[TEST_PATH_BYTES];
  char root_path[TEST_PATH_BYTES];
  char types_path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostRoot *host = 0;

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(raw_root, sizeof(raw_root),
                                                  "confit-r09-types"));
  CONFIT_TEST_ASSERT(realpath(raw_root, root_path) != 0);
  join_path(types_path, root_path, "types");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(types_path));
  write_text(root_path, "types/Confit.toml",
             "schema_version = 6\n"
             "mainmenu = \"Type tests\"\n"
             "source = [\"types/leaf.toml\"]\n");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         root_path, 0, &host, &diagnostic) == CONFIT_OK);

  test_valid_types(root_path, host);
  test_wrong_native_types(root_path, host);
  test_field_applicability(root_path, host);
  test_ranges(root_path, host);
  test_enum_domains(root_path, host);
  test_string_and_domain_bounds(root_path, host);

  confit_host_root_destroy(host);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
  return 0;
}
