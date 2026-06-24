#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/generator.h"
#include "confit/host.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit/status.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static int join_fixture(char *out, size_t out_size, const char *fixture) {
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR, fixture,
                               &diagnostic) == CONFIT_OK;
}

static int read_fixture_text(const char *fixture, char **out_text) {
  ConfitDiagnostic diagnostic;
  size_t text_size;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  text_size = 0U;
  return confit_host_read_text_file(path, out_text, &text_size, &diagnostic) ==
         CONFIT_OK;
}

static int load_project(ConfitProject **out_project) {
  ConfitDiagnostic diagnostic;
  char path[512];

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema/valid/basic")) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  *out_project = 0;
  return confit_schema_load_project(path, out_project, &diagnostic) ==
         CONFIT_OK;
}

static int expect_serialized_value(const ConfitValue *value,
                                   ConfitOptionType option_type,
                                   ConfitGeneratorValueFormat format,
                                   const char *expected) {
  ConfitDiagnostic diagnostic;
  char *text;
  int ok;

  confit_diagnostic_init(&diagnostic);
  text = 0;
  if (confit_generator_serialize_value(value, option_type, format, &text,
                                       &diagnostic) != CONFIT_OK) {
    return 0;
  }

  ok = text != 0 && strcmp(text, expected) == 0;
  if (!ok) {
    fprintf(stderr, "serialized value mismatch\nexpected: %s\nactual:   %s\n",
            expected, text != 0 ? text : "(null)");
  }
  confit_generator_string_free(text);
  return ok;
}

static int expect_serialized_record(const ConfitResolvedValue *value,
                                    ConfitOptionType option_type,
                                    ConfitGeneratorValueFormat format,
                                    const char *expected) {
  ConfitDiagnostic diagnostic;
  char *text;
  int ok;

  confit_diagnostic_init(&diagnostic);
  text = 0;
  if (confit_generator_serialize_resolved_value(value, option_type, format,
                                                &text,
                                                &diagnostic) != CONFIT_OK) {
    return 0;
  }

  ok = text != 0 && strcmp(text, expected) == 0;
  if (!ok) {
    fprintf(stderr, "serialized record mismatch\nexpected: %s\nactual:   %s\n",
            expected, text != 0 ? text : "(null)");
  }
  confit_generator_string_free(text);
  return ok;
}

static int check_value_serialization_helpers(void) {
  ConfitValue value;
  ConfitResolvedValue resolved;
  int ok;

  ok = 1;
  confit_value_init(&value);

  confit_value_set_bool(&value, 1);
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_BOOL,
                                     CONFIT_GENERATOR_VALUE_TEXT, "true");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_BOOL,
                                     CONFIT_GENERATOR_VALUE_LUA, "true");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_BOOL,
                                     CONFIT_GENERATOR_VALUE_CMAKE, "\"true\"");
  confit_value_clear(&value);

  confit_value_set_int(&value, -42);
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_INT,
                                     CONFIT_GENERATOR_VALUE_TEXT, "-42");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_INT,
                                     CONFIT_GENERATOR_VALUE_LUA, "-42");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_INT,
                                     CONFIT_GENERATOR_VALUE_CMAKE, "\"-42\"");
  confit_value_clear(&value);

  confit_value_set_uint(&value, UINT64_C(255));
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_UINT,
                                     CONFIT_GENERATOR_VALUE_TEXT, "255");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_HEX,
                                     CONFIT_GENERATOR_VALUE_TEXT,
                                     "0x000000FF");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_HEX,
                                     CONFIT_GENERATOR_VALUE_LUA, "255");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_HEX,
                                     CONFIT_GENERATOR_VALUE_CMAKE,
                                     "\"0x000000FF\"");
  confit_value_clear(&value);

  confit_value_set_float(&value, 3.125);
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_FLOAT,
                                     CONFIT_GENERATOR_VALUE_TEXT, "3.125");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_FLOAT,
                                     CONFIT_GENERATOR_VALUE_LUA, "3.125");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_FLOAT,
                                     CONFIT_GENERATOR_VALUE_CMAKE, "\"3.125\"");
  confit_value_clear(&value);

  ok = ok && confit_value_set_string(&value, "hello\nquote\"slash\\semi;$") ==
                 CONFIT_OK;
  ok = ok && expect_serialized_value(
                 &value, CONFIT_OPTION_TYPE_STRING,
                 CONFIT_GENERATOR_VALUE_TEXT,
                 "hello\\nquote\\\"slash\\\\semi;$");
  ok = ok && expect_serialized_value(
                 &value, CONFIT_OPTION_TYPE_STRING,
                 CONFIT_GENERATOR_VALUE_LUA,
                 "\"hello\\nquote\\\"slash\\\\semi;$\"");
  ok = ok && expect_serialized_value(
                 &value, CONFIT_OPTION_TYPE_STRING,
                 CONFIT_GENERATOR_VALUE_CMAKE,
                 "\"hello\\nquote\\\"slash\\\\semi\\;\\$\"");
  confit_value_clear(&value);

  ok = ok && confit_value_set_enum(&value, "fast") == CONFIT_OK;
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_ENUM,
                                     CONFIT_GENERATOR_VALUE_TEXT, "fast");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_ENUM,
                                     CONFIT_GENERATOR_VALUE_LUA, "\"fast\"");
  confit_value_clear(&value);

  ok = ok && confit_value_set_path(&value, "drivers/uart") == CONFIT_OK;
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_PATH,
                                     CONFIT_GENERATOR_VALUE_TEXT,
                                     "drivers/uart");
  ok = ok && expect_serialized_value(&value, CONFIT_OPTION_TYPE_PATH,
                                     CONFIT_GENERATOR_VALUE_CMAKE,
                                     "\"drivers/uart\"");
  confit_value_clear(&value);

  confit_value_set_uint(&value, 4096U);
  resolved.option_id = "delos.trace.capacity";
  resolved.value = value;
  resolved.source = "profiles/release.toml";
  ok = ok && expect_serialized_record(
                 &resolved, CONFIT_OPTION_TYPE_UINT,
                 CONFIT_GENERATOR_VALUE_LUA,
                 "{ type = \"uint\", value = 4096, text = \"4096\", "
                 "source = \"profiles/release.toml\" }");
  ok = ok && expect_serialized_record(
                 &resolved, CONFIT_OPTION_TYPE_UINT,
                 CONFIT_GENERATOR_VALUE_CMAKE,
                 "set(DELOS_CONFIG_TRACE_CAPACITY_TYPE \"uint\")\n"
                 "set(DELOS_CONFIG_TRACE_CAPACITY_VALUE \"4096\")\n"
                 "set(DELOS_CONFIG_TRACE_CAPACITY_TEXT \"4096\")\n"
                 "set(DELOS_CONFIG_TRACE_CAPACITY_SOURCE "
                 "\"profiles/release.toml\")\n");
  ok = ok && expect_serialized_record(
                 &resolved, CONFIT_OPTION_TYPE_UINT,
                 CONFIT_GENERATOR_VALUE_TEXT,
                 "delos.trace.capacity type=uint value=4096 text=4096 "
                 "source=profiles/release.toml");
  confit_value_clear(&value);

  confit_value_set_uint(&value, UINT64_C(0x08000000));
  resolved.option_id = "delos.memory.flash_origin";
  resolved.value = value;
  resolved.source = "targets/renode-nucleo-h753zi.toml";
  ok = ok && expect_serialized_record(
                 &resolved, CONFIT_OPTION_TYPE_HEX,
                 CONFIT_GENERATOR_VALUE_LUA,
                 "{ type = \"hex\", value = 134217728, text = "
                 "\"0x08000000\", source = "
                 "\"targets/renode-nucleo-h753zi.toml\" }");
  confit_value_clear(&value);

  return ok;
}

int main(void) {
  ConfitConfigHeaderOptions options;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitResolvedConfig *config;
  ConfitBuildIntegrationOptions build_options;
  char *header;
  char *header_again;
  char *cmake_fragment;
  char *qstar_config_module;
  char *qstar_manifest;
  char *golden;
  char *cmake_golden;
  char *qstar_config_golden;
  char *qstar_golden;
  int ok;

  if (!load_project(&project)) {
    return 1;
  }

  confit_diagnostic_init(&diagnostic);
  config = 0;
  if (confit_resolver_resolve(project, "sim-dsh", 0, 0, 0U, &config,
                              &diagnostic) != CONFIT_OK) {
    confit_project_free(project);
    return 2;
  }

  options.profile_name = "sim-dsh";
  options.target_name = "host-sim";
  build_options.profile_name = "sim-dsh";
  build_options.target_name = "host-sim";
  build_options.header_path = "config.h";
  build_options.report_json_path = "config.report.json";
  build_options.explain_text_path = "config.explain.txt";
  build_options.graph_json_path = "config.graph.json";
  build_options.inputs_json_path = "config.inputs.json";
  header = 0;
  header_again = 0;
  cmake_fragment = 0;
  qstar_config_module = 0;
  qstar_manifest = 0;
  golden = 0;
  cmake_golden = 0;
  qstar_config_golden = 0;
  qstar_golden = 0;
  if (confit_generate_config_header(project, config, &options, &header,
                                    &diagnostic) != CONFIT_OK ||
      confit_generate_config_header(project, config, &options, &header_again,
                                    &diagnostic) != CONFIT_OK ||
      confit_generate_cmake_fragment(project, config, &build_options,
                                     &cmake_fragment, &diagnostic) !=
          CONFIT_OK ||
      confit_generate_qstar_config_module(project, config, &build_options,
                                          &qstar_config_module,
                                          &diagnostic) != CONFIT_OK ||
      confit_generate_qstar_manifest(project, config, &build_options,
                                     &qstar_manifest, &diagnostic) !=
          CONFIT_OK ||
      !read_fixture_text("tests/golden/generator/sim-dsh-config.h", &golden) ||
      !read_fixture_text("tests/golden/generator/sim-dsh-config.cmake",
                         &cmake_golden) ||
      !read_fixture_text("tests/golden/generator/sim-dsh-config.qsm",
                         &qstar_config_golden) ||
      !read_fixture_text("tests/golden/generator/sim-dsh-config.qst",
                         &qstar_golden)) {
    confit_generator_string_free(header);
    confit_generator_string_free(header_again);
    confit_generator_string_free(cmake_fragment);
    confit_generator_string_free(qstar_config_module);
    confit_generator_string_free(qstar_manifest);
    free(golden);
    free(cmake_golden);
    free(qstar_config_golden);
    free(qstar_golden);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 3;
  }

  ok = check_value_serialization_helpers() &&
       strcmp(header, golden) == 0 && strcmp(header, header_again) == 0 &&
       strcmp(cmake_fragment, cmake_golden) == 0 &&
       strcmp(qstar_config_module, qstar_config_golden) == 0 &&
       strcmp(qstar_manifest, qstar_golden) == 0 &&
       strstr(header, "timestamp") == 0 &&
       strstr(header, "/Users/") == 0 &&
       strstr(cmake_fragment, "timestamp") == 0 &&
       strstr(qstar_config_module, "/Users/") == 0 &&
       strstr(qstar_manifest, "/Users/") == 0;

  confit_generator_string_free(header);
  confit_generator_string_free(header_again);
  confit_generator_string_free(cmake_fragment);
  confit_generator_string_free(qstar_config_module);
  confit_generator_string_free(qstar_manifest);
  free(golden);
  free(cmake_golden);
  free(qstar_config_golden);
  free(qstar_golden);
  confit_resolved_config_free(config);
  confit_project_free(project);
  return ok ? 0 : 4;
}
