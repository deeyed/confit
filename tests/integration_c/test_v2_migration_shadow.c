#include "test_assert.h"
#include "test_fs.h"
#include "test_process.h"

#include <stdio.h>
#include <string.h>

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

typedef struct ConfitMigrationShadowCase {
  const char *id;
  const char *project;
  const char *profile;
  const char *target;
} ConfitMigrationShadowCase;

typedef struct ConfitMigrationShadowContext {
  const char *confit_bin;
  const char *source_dir;
  const char *work_dir;
  unsigned int run_index;
} ConfitMigrationShadowContext;

static const ConfitMigrationShadowCase kShadowCases[] = {
    {"delos/debug/default", "delos", "debug", 0},
    {"delos/release/default", "delos", "release", 0},
    {"delos/sim-dsh/sim-dsh", "delos", "sim-dsh", "sim-dsh"},
    {"delos/parus-delos-debug/sim-dsh", "delos", "parus-delos-debug",
     "sim-dsh"},
    {"delos/parus-delos-mismatch/qemu-mps2-an500", "delos",
     "parus-delos-mismatch", "qemu-mps2-an500"},
    {"parus/bringup/default", "parus", "bringup", 0},
    {"parus/qemu-aarch64/qemu-virt-aarch64", "parus", "qemu-aarch64",
     "qemu-virt-aarch64"},
    {"parus/rpi5-direct-dtb/rpi5-direct-dtb", "parus", "rpi5-direct-dtb",
     "rpi5-direct-dtb"},
    {"parus/parus-delos-debug/qemu-virt-aarch64", "parus",
     "parus-delos-debug", "qemu-virt-aarch64"},
    {"parus/parus-delos-mismatch/qemu-virt-aarch64", "parus",
     "parus-delos-mismatch", "qemu-virt-aarch64"},
};

static const char kExpectedReport[] =
    "{\n"
    "  \"schema\": \"confit-migration-shadow-v1\",\n"
    "  \"scope\": \"realish fixture only; no Parus or Delos source tree was modified\",\n"
    "  \"entries\": [\n"
    "    {\"id\": \"delos/debug/default\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"delos/release/default\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"delos/sim-dsh/sim-dsh\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"delos/parus-delos-debug/sim-dsh\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"delos/parus-delos-mismatch/qemu-mps2-an500\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"parus/bringup/default\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"parus/qemu-aarch64/qemu-virt-aarch64\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"parus/rpi5-direct-dtb/rpi5-direct-dtb\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"parus/parus-delos-debug/qemu-virt-aarch64\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"},\n"
    "    {\"id\": \"parus/parus-delos-mismatch/qemu-virt-aarch64\", \"requested\": \"unavailable-in-v1-abi\", \"effective\": \"same\", \"provenance\": \"mechanical\", \"artifact\": \"artifact-abi\"}\n"
    "  ],\n"
    "  \"summary\": {\"effective_same\": 10, \"mechanical\": 10, \"intentional_semantic\": 0, \"unresolved\": 0}\n"
    "}\n";

static void test_join(char *out, size_t out_size, const char *left,
                      const char *right) {
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(out, out_size, left, right));
}

static void test_join4(char *out, size_t out_size, const char *first,
                       const char *second, const char *third,
                       const char *fourth) {
  char scratch[4096];
  char scratch2[4096];

  test_join(scratch, sizeof(scratch), first, second);
  test_join(scratch2, sizeof(scratch2), scratch, third);
  test_join(out, out_size, scratch2, fourth);
}

static void test_run(ConfitMigrationShadowContext *context,
                     const char *const *argv, ConfitTestProcessResult *result) {
  char stdout_path[4096];
  char stderr_path[4096];
  char stdout_name[64];
  char stderr_name[64];

  confit_test_process_result_clear(result);
  (void)snprintf(stdout_name, sizeof(stdout_name), "run-%03u.out",
                 context->run_index);
  (void)snprintf(stderr_name, sizeof(stderr_name), "run-%03u.err",
                 context->run_index);
  context->run_index += 1U;
  test_join(stdout_path, sizeof(stdout_path), context->work_dir, stdout_name);
  test_join(stderr_path, sizeof(stderr_path), context->work_dir, stderr_name);
  CONFIT_TEST_ASSERT(confit_test_process_run(argv, 0, stdout_path, stderr_path,
                                             result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result->exit_code);
}

static const char *test_values_begin(const char *text) {
  const char *marker;

  if (strncmp(text, "[values]\n", 9U) == 0) {
    return text + 9U;
  }
  marker = strstr(text, "\n[values]\n");
  return marker != 0 ? marker + 10U : 0;
}

static int test_next_value_line(const char **cursor, const char **out_start,
                                size_t *out_size) {
  const char *start = *cursor;
  const char *end;

  while (*start == '\n' || *start == '\r') {
    ++start;
  }
  if (*start == '\0' || *start == '[') {
    *cursor = start;
    return 0;
  }
  end = start;
  while (*end != '\0' && *end != '\n' && *end != '\r') {
    ++end;
  }
  *out_start = start;
  *out_size = (size_t)(end - start);
  *cursor = end;
  return 1;
}

static int test_effective_values_equal(const char *v1_text,
                                       const char *v2_text) {
  const char *v1_cursor = test_values_begin(v1_text);
  const char *v2_cursor = test_values_begin(v2_text);
  const char *v1_line;
  const char *v2_line;
  size_t v1_size;
  size_t v2_size;
  int has_v1;
  int has_v2;

  if (v1_cursor == 0 || v2_cursor == 0) {
    return 0;
  }
  for (;;) {
    has_v1 = test_next_value_line(&v1_cursor, &v1_line, &v1_size);
    has_v2 = test_next_value_line(&v2_cursor, &v2_line, &v2_size);
    if (has_v1 != has_v2) {
      return 0;
    }
    if (!has_v1) {
      return 1;
    }
    if (v1_size != v2_size || memcmp(v1_line, v2_line, v1_size) != 0) {
      return 0;
    }
  }
}

static void test_expect_artifact_bundle(const char *root) {
  static const char *const kArtifacts[] = {"config.h", "config.report.json",
                                            "config.cmake", 0};
  char path[4096];
  char config_dir[4096];
  size_t index;

  for (index = 0U; kArtifacts[index] != 0; ++index) {
    test_join(path, sizeof(path), root, kArtifacts[index]);
    CONFIT_TEST_ASSERT(confit_test_fs_file_exists(path));
  }
  test_join(config_dir, sizeof(config_dir), root, "config");
  test_join(path, sizeof(path), config_dir, "config.qsm");
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(path));
}

static void test_rehearse_automatic_candidate(
    ConfitMigrationShadowContext *context, const char *project) {
  const char *migrate[] = {context->confit_bin, "migrate", "--project", 0,
                           "--out", 0, 0};
  ConfitTestProcessResult result = {-1, 0, 0};
  char source_root[4096];
  char source_project[4096];
  char output_root[4096];
  char report_path[4096];
  char *before;
  char *after;
  char *report;

  test_join4(source_root, sizeof(source_root), context->source_dir, "tests",
             "fixtures/realish", project);
  test_join(source_project, sizeof(source_project), source_root,
            "config/project.toml");
  test_join4(output_root, sizeof(output_root), context->work_dir, "migration",
             "automatic", project);
  before = confit_test_fs_read_file(source_project);
  CONFIT_TEST_ASSERT(before != 0);
  migrate[3] = source_root;
  migrate[5] = output_root;
  test_run(context, migrate, &result);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "migration candidate ok");
  confit_test_process_result_clear(&result);
  after = confit_test_fs_read_file(source_project);
  CONFIT_TEST_ASSERT(after != 0);
  CONFIT_TEST_ASSERT(strcmp(before, after) == 0);
  confit_test_fs_free(before);
  confit_test_fs_free(after);
  test_join(report_path, sizeof(report_path), output_root,
            "migration-report.json");
  report = confit_test_fs_read_file(report_path);
  CONFIT_TEST_ASSERT(report != 0);
  CONFIT_TEST_ASSERT_CONTAINS(report, "candidate only");
  confit_test_fs_free(report);
}

static void test_run_shadow_case(ConfitMigrationShadowContext *context,
                                 const ConfitMigrationShadowCase *shadow_case,
                                 size_t index) {
  const char *resolve_v1[14];
  const char *resolve_v2[14];
  const char *gen_v1[14];
  const char *gen_v2[14];
  ConfitTestProcessResult v1_result = {-1, 0, 0};
  ConfitTestProcessResult v2_result = {-1, 0, 0};
  char v1_project[4096];
  char v2_project[4096];
  char output_root[4096];
  char v1_output[4096];
  char v2_output[4096];
  size_t argument_index;

  test_join4(v1_project, sizeof(v1_project), context->source_dir, "tests",
             "fixtures/realish", shadow_case->project);
  test_join4(v2_project, sizeof(v2_project), context->source_dir, "tests",
             "fixtures/realish-v2", shadow_case->project);
  test_join(output_root, sizeof(output_root), context->work_dir, "artifacts");
  (void)snprintf(v1_output, sizeof(v1_output), "%s%c%02zu-v1", output_root,
                 confit_test_fs_separator(), index);
  (void)snprintf(v2_output, sizeof(v2_output), "%s%c%02zu-v2", output_root,
                 confit_test_fs_separator(), index);

  argument_index = 0U;
  resolve_v1[argument_index++] = context->confit_bin;
  resolve_v1[argument_index++] = "resolve";
  resolve_v1[argument_index++] = "--project";
  resolve_v1[argument_index++] = v1_project;
  resolve_v1[argument_index++] = "--profile";
  resolve_v1[argument_index++] = shadow_case->profile;
  if (shadow_case->target != 0) {
    resolve_v1[argument_index++] = "--target";
    resolve_v1[argument_index++] = shadow_case->target;
  }
  resolve_v1[argument_index++] = "--format";
  resolve_v1[argument_index++] = "toml";
  resolve_v1[argument_index] = 0;

  argument_index = 0U;
  resolve_v2[argument_index++] = context->confit_bin;
  resolve_v2[argument_index++] = "resolve";
  resolve_v2[argument_index++] = "--project";
  resolve_v2[argument_index++] = v2_project;
  resolve_v2[argument_index++] = "--profile";
  resolve_v2[argument_index++] = shadow_case->profile;
  if (shadow_case->target != 0) {
    resolve_v2[argument_index++] = "--target";
    resolve_v2[argument_index++] = shadow_case->target;
  }
  resolve_v2[argument_index++] = "--format";
  resolve_v2[argument_index++] = "toml";
  resolve_v2[argument_index] = 0;

  test_run(context, resolve_v1, &v1_result);
  test_run(context, resolve_v2, &v2_result);
  CONFIT_TEST_ASSERT(test_effective_values_equal(v1_result.stdout_text,
                                                  v2_result.stdout_text));
  confit_test_process_result_clear(&v1_result);
  confit_test_process_result_clear(&v2_result);

  argument_index = 0U;
  gen_v1[argument_index++] = context->confit_bin;
  gen_v1[argument_index++] = "gen";
  gen_v1[argument_index++] = "--project";
  gen_v1[argument_index++] = v1_project;
  gen_v1[argument_index++] = "--profile";
  gen_v1[argument_index++] = shadow_case->profile;
  if (shadow_case->target != 0) {
    gen_v1[argument_index++] = "--target";
    gen_v1[argument_index++] = shadow_case->target;
  }
  gen_v1[argument_index++] = "--out";
  gen_v1[argument_index++] = v1_output;
  gen_v1[argument_index++] = "--artifact";
  gen_v1[argument_index++] = "all";
  gen_v1[argument_index] = 0;

  argument_index = 0U;
  gen_v2[argument_index++] = context->confit_bin;
  gen_v2[argument_index++] = "gen";
  gen_v2[argument_index++] = "--project";
  gen_v2[argument_index++] = v2_project;
  gen_v2[argument_index++] = "--profile";
  gen_v2[argument_index++] = shadow_case->profile;
  if (shadow_case->target != 0) {
    gen_v2[argument_index++] = "--target";
    gen_v2[argument_index++] = shadow_case->target;
  }
  gen_v2[argument_index++] = "--out";
  gen_v2[argument_index++] = v2_output;
  gen_v2[argument_index++] = "--artifact";
  gen_v2[argument_index++] = "all";
  gen_v2[argument_index] = 0;

  test_run(context, gen_v1, &v1_result);
  test_run(context, gen_v2, &v2_result);
  test_expect_artifact_bundle(v1_output);
  test_expect_artifact_bundle(v2_output);
  confit_test_process_result_clear(&v1_result);
  confit_test_process_result_clear(&v2_result);
}

int main(int argc, char **argv) {
  ConfitMigrationShadowContext context;
  char bin_buffer[4096];
  char work_buffer[4096];
  char report_path[4096];
  char golden_path[4096];
  char *golden;
  size_t index;

  memset(&context, 0, sizeof(context));
  if (argc == 4) {
    context.confit_bin = argv[1];
    context.source_dir = argv[2];
    context.work_dir = argv[3];
  } else if (argc == 1) {
    test_join(bin_buffer, sizeof(bin_buffer), ".", "confit");
    test_join(work_buffer, sizeof(work_buffer), ".", "round19-migration-shadow");
    context.confit_bin = bin_buffer;
    context.source_dir = CONFIT_TEST_SOURCE_DIR;
    context.work_dir = work_buffer;
  } else {
    CONFIT_TEST_FAIL("usage: confit_test_v2_migration_shadow <bin> <source> <work>");
  }

  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(context.work_dir));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(context.work_dir));
  test_rehearse_automatic_candidate(&context, "delos");
  test_rehearse_automatic_candidate(&context, "parus");
  for (index = 0U; index < sizeof(kShadowCases) / sizeof(kShadowCases[0]);
       ++index) {
    test_run_shadow_case(&context, &kShadowCases[index], index);
  }

  test_join(report_path, sizeof(report_path), context.work_dir,
            "semantic-shadow.json");
  test_join4(golden_path, sizeof(golden_path), context.source_dir, "tests",
             "golden/migration-v2", "realish-shadow.json");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(report_path, kExpectedReport));
  golden = confit_test_fs_read_file(golden_path);
  CONFIT_TEST_ASSERT(golden != 0);
  CONFIT_TEST_ASSERT(strcmp(kExpectedReport, golden) == 0);
  confit_test_fs_free(golden);
  return 0;
}
