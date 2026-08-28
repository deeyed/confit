#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "confit/config.h"
#include "confit/emitter.h"
#include "confit/schema.h"
#include "confit/snapshot.h"

#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_fs.h"
#include "test_process.h"

#ifndef CONFIT_TEST_HOST_CC
#define CONFIT_TEST_HOST_CC "/usr/bin/clang"
#endif

#ifndef CONFIT_TEST_BMAKE_TOOL
#define CONFIT_TEST_BMAKE_TOOL "/usr/bin/bmake"
#endif

#define TEST_PATH_BYTES 4096U

static const char kEntry[] =
    "schema_version = 6\n"
    "mainmenu = \"Emitter integration\"\n"
    "source = [\"config/options.toml\"]\n";

static const char kOptions[] =
    "[menu]\n"
    "prompt = \"Runtime\"\n"
    "help = \"Configure generic runtime values.\"\n"
    "\n"
    "[[config]]\n"
    "symbol = \"ENABLE_METRICS\"\n"
    "type = \"bool\"\n"
    "prompt = \"Enable metrics\"\n"
    "help = \"Enable generic metrics data.\"\n"
    "default = false\n"
    "\n"
    "[[config]]\n"
    "symbol = \"WORKER_COUNT\"\n"
    "type = \"int\"\n"
    "prompt = \"Worker count\"\n"
    "help = \"Set a bounded worker count.\"\n"
    "default = 4\n"
    "range = { min = 1, max = 64 }\n"
    "\n"
    "[[config]]\n"
    "symbol = \"MINIMUM_VALUE\"\n"
    "type = \"int\"\n"
    "prompt = \"Minimum signed value\"\n"
    "help = \"Exercise the complete signed integer output domain.\"\n"
    "default = -9223372036854775808\n"
    "\n"
    "[[config]]\n"
    "symbol = \"DEVICE_ID\"\n"
    "type = \"hex\"\n"
    "prompt = \"Device identifier\"\n"
    "help = \"Set one generic hexadecimal identifier.\"\n"
    "default = 0x10e8\n"
    "range = { min = 0x0, max = 0xffff }\n"
    "\n"
    "[[config]]\n"
    "symbol = \"LOG_LEVEL\"\n"
    "type = \"enum\"\n"
    "prompt = \"Log level\"\n"
    "help = \"Select a generic log level.\"\n"
    "values = [\"quiet\", \"normal\", \"verbose\"]\n"
    "default = \"normal\"\n";

static const char kUser[] =
    "schema_version = 6\n"
    "\n"
    "[values]\n"
    "ENABLE_METRICS = true\n"
    "WORKER_COUNT = 8\n"
    "LOG_LEVEL = \"verbose\"\n";

static void join_path(char *out, const char *left, const char *right) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, TEST_PATH_BYTES, left, right));
}

static void make_directory(const char *root, const char *relative) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(path));
}

static void write_text(const char *root, const char *relative,
                       const char *text) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static ConfitHostRoot *open_root(const char *path) {
  ConfitHostRoot *root = 0;
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(path, 0, &root,
                                                    &diagnostic) == CONFIT_OK);
  return root;
}

static ConfitResolution *load_and_resolve(
    ConfitHostRoot *project_root, ConfitSchemaProject **out_project,
    ConfitUserConfig **out_config) {
  ConfitResolution *resolution = 0;
  const ConfitAssignment *assignments;
  ConfitDiagnostic diagnostic;
  size_t count = 0U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         project_root, "Confit.toml", 0, out_project,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_user_config_load_relative(
                         project_root, "configs/development.toml",
                         confit_schema_project_catalog(*out_project), 0,
                         out_config, &diagnostic) == CONFIT_OK);
  assignments = confit_user_config_assignments(*out_config, &count);
  CONFIT_TEST_ASSERT(confit_resolve(
                         confit_schema_project_catalog(*out_project),
                         confit_schema_project_dependency_plan(*out_project),
                         assignments, count, 0, &resolution,
                         &diagnostic) == CONFIT_OK);
  return resolution;
}

static void write_artifact(const char *directory,
                           const ConfitEmittedArtifactView *artifact) {
  char path[TEST_PATH_BYTES];
  join_path(path, directory, artifact->name);
  CONFIT_TEST_ASSERT(artifact->bytes != 0 &&
                     artifact->bytes[artifact->size] == '\0');
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path, (const char *)artifact->bytes));
}

static void run_c_header_consumer(const char *consumer_directory,
                                  const ConfitEmittedArtifactView *header) {
  static const char source[] =
      "#include \"values.h\"\n"
      "#if CONFIG_ENABLE_METRICS != 1\n"
      "#error bad bool projection\n"
      "#endif\n"
      "#if CONFIG_WORKER_COUNT != 8\n"
      "#error bad int projection\n"
      "#endif\n"
      "#if CONFIG_DEVICE_ID != 0x10e8\n"
      "#error bad hex projection\n"
      "#endif\n"
      "_Static_assert(CONFIG_MINIMUM_VALUE == "
      "(-9223372036854775807LL - 1LL), \"bad minimum int\");\n"
      "int confit_emitted_values(void) { return CONFIG_LOG_LEVEL[0] == 'v' "
      "? 0 : 1; }\n";
  ConfitTestProcessResult result;
  const char *arguments[10];
  char source_path[TEST_PATH_BYTES];
  char object_path[TEST_PATH_BYTES];
  char stdout_path[TEST_PATH_BYTES];
  char stderr_path[TEST_PATH_BYTES];
  write_artifact(consumer_directory, header);
  join_path(source_path, consumer_directory, "consumer.c");
  join_path(object_path, consumer_directory, "consumer.o");
  join_path(stdout_path, consumer_directory, "clang.stdout");
  join_path(stderr_path, consumer_directory, "clang.stderr");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(source_path, source));
  arguments[0] = CONFIT_TEST_HOST_CC;
  arguments[1] = "-std=c17";
  arguments[2] = "-Wall";
  arguments[3] = "-Wextra";
  arguments[4] = "-Werror";
  arguments[5] = "-pedantic";
  arguments[6] = "-c";
  arguments[7] = source_path;
  arguments[8] = "-o";
  arguments[9] = 0;
  {
    const char *complete_arguments[11];
    size_t index;
    for (index = 0U; index < 9U; ++index)
      complete_arguments[index] = arguments[index];
    complete_arguments[9] = object_path;
    complete_arguments[10] = 0;
    memset(&result, 0, sizeof(result));
    CONFIT_TEST_ASSERT(confit_test_process_run(
        complete_arguments, consumer_directory, stdout_path, stderr_path,
        &result));
  }
  CONFIT_TEST_ASSERT(result.exit_code == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(object_path));
  confit_test_process_result_clear(&result);
}

static void run_bmake_consumer(const char *consumer_directory,
                               const ConfitEmittedArtifactView *make) {
  static const char makefile[] =
      ".include \"values.mk\"\n"
      ".if ${CONFIG_ENABLE_METRICS} != \"true\"\n"
      ".error bad bool assignment\n"
      ".endif\n"
      ".if ${CONFIG_WORKER_COUNT} != \"8\"\n"
      ".error bad int assignment\n"
      ".endif\n"
      ".if ${CONFIG_DEVICE_ID} != \"0x10e8\"\n"
      ".error bad hex assignment\n"
      ".endif\n"
      ".if ${CONFIG_LOG_LEVEL} != \"verbose\"\n"
      ".error bad enum assignment\n"
      ".endif\n"
      ".if ${CONFIG_MINIMUM_VALUE} != \"-9223372036854775808\"\n"
      ".error bad minimum int assignment\n"
      ".endif\n"
      ".PHONY: all\n"
      "all:\n"
      "\t@:\n";
  ConfitTestProcessResult result;
  const char *arguments[5];
  char stdout_path[TEST_PATH_BYTES];
  char stderr_path[TEST_PATH_BYTES];
  write_artifact(consumer_directory, make);
  write_text(consumer_directory, "Makefile", makefile);
  join_path(stdout_path, consumer_directory, "bmake.stdout");
  join_path(stderr_path, consumer_directory, "bmake.stderr");
  arguments[0] = CONFIT_TEST_BMAKE_TOOL;
  arguments[1] = "-f";
  arguments[2] = "Makefile";
  arguments[3] = "all";
  arguments[4] = 0;
  memset(&result, 0, sizeof(result));
  CONFIT_TEST_ASSERT(confit_test_process_run(
      arguments, consumer_directory, stdout_path, stderr_path, &result));
  CONFIT_TEST_ASSERT(result.exit_code == 0);
  confit_test_process_result_clear(&result);
}

static void run_hostile_string_c_consumer(
    const char *consumer_directory,
    const ConfitEmittedArtifactView *header) {
  static const char source[] =
      "#include \"values.h\"\n"
      "_Static_assert(sizeof(CONFIG_INSTANCE_LABEL) > 8, "
      "\"string projection was truncated\");\n"
      "int confit_string_value(void) { return CONFIG_INSTANCE_LABEL[0] == 'l' "
      "? 0 : 1; }\n";
  ConfitTestProcessResult result;
  const char *arguments[11];
  char source_path[TEST_PATH_BYTES];
  char object_path[TEST_PATH_BYTES];
  char stdout_path[TEST_PATH_BYTES];
  char stderr_path[TEST_PATH_BYTES];
  write_artifact(consumer_directory, header);
  join_path(source_path, consumer_directory, "string-consumer.c");
  join_path(object_path, consumer_directory, "string-consumer.o");
  join_path(stdout_path, consumer_directory, "string-clang.stdout");
  join_path(stderr_path, consumer_directory, "string-clang.stderr");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(source_path, source));
  arguments[0] = CONFIT_TEST_HOST_CC;
  arguments[1] = "-std=c17";
  arguments[2] = "-Wall";
  arguments[3] = "-Wextra";
  arguments[4] = "-Werror";
  arguments[5] = "-pedantic";
  arguments[6] = "-trigraphs";
  arguments[7] = "-c";
  arguments[8] = source_path;
  arguments[9] = "-o";
  arguments[10] = 0;
  {
    const char *complete_arguments[12];
    size_t index;
    for (index = 0U; index < 10U; ++index)
      complete_arguments[index] = arguments[index];
    complete_arguments[10] = object_path;
    complete_arguments[11] = 0;
    memset(&result, 0, sizeof(result));
    CONFIT_TEST_ASSERT(confit_test_process_run(
        complete_arguments, consumer_directory, stdout_path, stderr_path,
        &result));
  }
  CONFIT_TEST_ASSERT(result.exit_code == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(object_path));
  confit_test_process_result_clear(&result);
}

static void test_hostile_string_header_compile(void) {
  static const char hostile[] =
      "line\n.include ${VALUE} # ?" "?/ \"quote\"\\caf\303\251";
  char root_path[TEST_PATH_BYTES];
  ConfitCatalog *catalog = 0;
  ConfitDependencyPlan *plan = 0;
  ConfitResolution *resolution = 0;
  ConfitValue default_value;
  ConfitSourceFragmentSpec fragment;
  ConfitConfigSpec spec;
  ConfitEmitRequest request;
  ConfitEmission *emission = 0;
  ConfitEmittedArtifactView header;
  ConfitDiagnostic diagnostic;
  confit_value_init(&default_value);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      root_path, sizeof(root_path), "confit-c-string"));
  CONFIT_TEST_ASSERT(confit_value_set_string(
                         &default_value, hostile, strlen(hostile), 0,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_create(0, &catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(
                         catalog, "String emitter", &diagnostic) == CONFIT_OK);
  fragment.path = "config/string.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                 &diagnostic) == CONFIT_OK);
  memset(&spec, 0, sizeof(spec));
  spec.fragment = 0U;
  spec.menu = CONFIT_INDEX_NONE;
  spec.symbol = "INSTANCE_LABEL";
  spec.kind = CONFIT_VALUE_STRING;
  spec.prompt = "Instance label";
  spec.help = "Compile hostile but valid string data as a C literal.";
  spec.default_value = &default_value;
  spec.declaration.path = "config/string.toml";
  spec.declaration.line = 1U;
  spec.declaration.column = 1U;
  CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &spec, 0,
                                               &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, 0, 0U, 0, &resolution,
                                    &diagnostic) == CONFIT_OK);
  memset(&request, 0, sizeof(request));
  request.emit_c_header = 1;
  CONFIT_TEST_ASSERT(confit_emit(resolution, &request, 0, &emission,
                                 &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_emission_find_artifact(
      emission, CONFIT_EMITTER_C_HEADER, &header));
  run_hostile_string_c_consumer(root_path, &header);
  confit_emission_destroy(emission);
  confit_resolution_destroy(resolution);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
  confit_value_destroy(&default_value);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
}

static void verify_artifact(ConfitHostRoot *project_root,
                            ConfitHostRoot *output_root, const char *name,
                            int expected_success) {
  ConfitSnapshotVerifyRequest request;
  ConfitDiagnostic diagnostic;
  char path[TEST_PATH_BYTES];
  ConfitStatus status;
  memset(&request, 0, sizeof(request));
  request.project_root = project_root;
  request.output_root = output_root;
  request.expected_entry_path = "Confit.toml";
  request.artifact_name = name;
  confit_diagnostic_init(&diagnostic);
  path[0] = '\0';
  status = confit_snapshot_verify(&request, 0, path, sizeof(path),
                                  &diagnostic);
  if (expected_success) {
    CONFIT_TEST_ASSERT(status == CONFIT_OK && path[0] != '\0');
  } else {
    CONFIT_TEST_ASSERT(status == CONFIT_ERR_STALE && path[0] == '\0');
  }
}

static void test_snapshot_roles_and_native_consumers(void) {
  char raw_root[TEST_PATH_BYTES];
  char root_path[TEST_PATH_BYTES];
  char project_path[TEST_PATH_BYTES];
  char output_path[TEST_PATH_BYTES];
  char output_c_only_path[TEST_PATH_BYTES];
  char consumer_path[TEST_PATH_BYTES];
  ConfitHostRoot *project_root;
  ConfitHostRoot *output_root;
  ConfitHostRoot *output_c_only_root;
  ConfitSchemaProject *project = 0;
  ConfitUserConfig *config = 0;
  ConfitResolution *resolution;
  ConfitEmitRequest emit_request;
  ConfitEmission *emission = 0;
  ConfitEmission *c_only = 0;
  ConfitEmittedArtifactView make;
  ConfitEmittedArtifactView header;
  ConfitEmittedArtifactView json;
  ConfitSnapshotArtifactSpec optional[2];
  ConfitSnapshotArtifactSpec c_optional;
  ConfitSnapshotPublishRequest publish;
  ConfitSnapshotPublication publication;
  ConfitDiagnostic diagnostic;
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      raw_root, sizeof(raw_root), "confit-emitter"));
  CONFIT_TEST_ASSERT(realpath(raw_root, root_path) != 0);
  make_directory(root_path, "project/config");
  make_directory(root_path, "project/configs");
  make_directory(root_path, "output");
  make_directory(root_path, "output-c-only");
  make_directory(root_path, "consumer");
  write_text(root_path, "project/Confit.toml", kEntry);
  write_text(root_path, "project/config/options.toml", kOptions);
  write_text(root_path, "project/configs/development.toml", kUser);
  join_path(project_path, root_path, "project");
  join_path(output_path, root_path, "output");
  join_path(output_c_only_path, root_path, "output-c-only");
  join_path(consumer_path, root_path, "consumer");
  project_root = open_root(project_path);
  output_root = open_root(output_path);
  output_c_only_root = open_root(output_c_only_path);
  resolution = load_and_resolve(project_root, &project, &config);

  memset(&emit_request, 0, sizeof(emit_request));
  emit_request.emit_make = 1;
  emit_request.emit_c_header = 1;
  emit_request.emit_json = 1;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_emit(resolution, &emit_request, 0, &emission,
                                 &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_emission_artifact_count(emission) == 3U);
  CONFIT_TEST_ASSERT(confit_emission_find_artifact(
      emission, CONFIT_EMITTER_MAKE, &make));
  CONFIT_TEST_ASSERT(confit_emission_find_artifact(
      emission, CONFIT_EMITTER_C_HEADER, &header));
  CONFIT_TEST_ASSERT(confit_emission_find_artifact(
      emission, CONFIT_EMITTER_JSON, &json));
  run_c_header_consumer(consumer_path, &header);
  run_bmake_consumer(consumer_path, &make);

  optional[0].role = make.role;
  optional[0].name = make.name;
  optional[0].bytes = make.bytes;
  optional[0].size = make.size;
  optional[0].printable = make.printable;
  optional[1].role = header.role;
  optional[1].name = header.name;
  optional[1].bytes = header.bytes;
  optional[1].size = header.size;
  optional[1].printable = header.printable;
  memset(&publish, 0, sizeof(publish));
  publish.project = project;
  publish.user_config = config;
  publish.resolution = resolution;
  publish.optional_artifacts = optional;
  publish.optional_artifact_count = 2U;
  publish.resolved_values_printable = 1;
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, &publish, 0, &publication,
                         &diagnostic) == CONFIT_OK);
  verify_artifact(project_root, output_root, "values.mk", 1);
  verify_artifact(project_root, output_root, "values.h", 1);
  verify_artifact(project_root, output_root, "resolved-values.json", 1);

  memset(&emit_request, 0, sizeof(emit_request));
  emit_request.emit_c_header = 1;
  CONFIT_TEST_ASSERT(confit_emit(resolution, &emit_request, 0, &c_only,
                                 &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_emission_artifact_count(c_only) == 1U);
  CONFIT_TEST_ASSERT(confit_emission_find_artifact(
      c_only, CONFIT_EMITTER_C_HEADER, &header));
  c_optional.role = header.role;
  c_optional.name = header.name;
  c_optional.bytes = header.bytes;
  c_optional.size = header.size;
  c_optional.printable = header.printable;
  memset(&publish, 0, sizeof(publish));
  publish.project = project;
  publish.user_config = config;
  publish.resolution = resolution;
  publish.optional_artifacts = &c_optional;
  publish.optional_artifact_count = 1U;
  publish.resolved_values_printable = 0;
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_c_only_root, &publish, 0, &publication,
                         &diagnostic) == CONFIT_OK);
  verify_artifact(project_root, output_c_only_root, "values.h", 1);
  verify_artifact(project_root, output_c_only_root, "values.mk", 0);
  verify_artifact(project_root, output_c_only_root,
                  "resolved-values.json", 0);

  confit_emission_destroy(c_only);
  confit_emission_destroy(emission);
  confit_resolution_destroy(resolution);
  confit_user_config_destroy(config);
  confit_schema_project_destroy(project);
  confit_host_root_destroy(output_c_only_root);
  confit_host_root_destroy(output_root);
  confit_host_root_destroy(project_root);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
}

int main(void) {
  test_hostile_string_header_compile();
  test_snapshot_roles_and_native_consumers();
  return 0;
}
