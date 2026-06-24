#include <string.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/model.h"
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

static int expect_schema_error(const char *fixture, const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_load_project(path, &project, &diagnostic) !=
      CONFIT_ERR_SCHEMA) {
    confit_project_free(project);
    return 0;
  }
  if (!confit_diagnostic_has_error(&diagnostic) ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message, message) != 0) {
    return 0;
  }

  return 1;
}

static ConfitProfile *find_profile(ConfitProject *project, const char *name) {
  size_t index;

  for (index = 0U; index < project->profile_count; ++index) {
    if (project->profiles[index].name != 0 &&
        strcmp(project->profiles[index].name, name) == 0) {
      return &project->profiles[index];
    }
  }
  return 0;
}

static ConfitTarget *find_target(ConfitProject *project, const char *name) {
  size_t index;

  for (index = 0U; index < project->target_count; ++index) {
    if (project->targets[index].name != 0 &&
        strcmp(project->targets[index].name, name) == 0) {
      return &project->targets[index];
    }
  }
  return 0;
}

static int expect_alias_canonicalization(void) {
  ConfitDiagnostic diagnostic;
  ConfitSchemaAudit audit;
  ConfitProject *project;
  ConfitProfile *profile;
  char path[512];
  int ok;

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema/valid/alias")) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  confit_schema_audit_init(&audit);
  project = 0;
  ok = 0;
  if (confit_schema_load_project_with_audit(path, &project, &audit,
                                            &diagnostic) != CONFIT_OK) {
    goto done;
  }
  profile = find_profile(project, "default");
  if (profile == 0 || profile->value_count != 1U ||
      strcmp(profile->values[0].option_id, "delos.debug.ddc") != 0 ||
      profile->values[0].value.kind != CONFIT_VALUE_BOOL ||
      profile->values[0].value.as.bool_value != 1) {
    goto done;
  }
  if (audit.warning_count != 1U || audit.warnings[0].option_id == 0 ||
      strcmp(audit.warnings[0].option_id, "delos.debug.old_ddc") != 0 ||
      audit.warnings[0].message == 0 ||
      strcmp(audit.warnings[0].message,
             "deprecated alias canonicalized to current option id") != 0) {
    goto done;
  }
  ok = 1;

done:
  confit_project_free(project);
  confit_schema_audit_clear(&audit);
  return ok;
}

static int expect_category_path_audit(void) {
  ConfitDiagnostic diagnostic;
  ConfitSchemaAudit audit;
  ConfitCategoryPathInfo info;
  ConfitProject *project;
  ConfitOption *option;
  char path[512];
  int ok;

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema/valid/category-path-depth")) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  confit_schema_audit_init(&audit);
  project = 0;
  ok = 0;
  if (confit_schema_load_project_with_audit(path, &project, &audit,
                                            &diagnostic) != CONFIT_OK) {
    goto done;
  }
  option = confit_project_find_option(project, "delos.trace.enabled");
  if (option == 0 || option->category == 0 ||
      strcmp(option->category, "runtime/trace") != 0 ||
      confit_category_path_analyze(option->category, &info) != CONFIT_OK ||
      info.depth != CONFIT_CATEGORY_PATH_RECOMMENDED_DEPTH) {
    goto done;
  }
  option = confit_project_find_option(project, "delos.trace.capacity");
  if (option == 0 || option->category == 0 ||
      strcmp(option->category, "runtime/trace/capacity/advanced") != 0 ||
      confit_category_path_analyze(option->category, &info) != CONFIT_OK ||
      info.depth != 4U) {
    goto done;
  }
  if (audit.warning_count != 1U || audit.warnings[0].option_id == 0 ||
      strcmp(audit.warnings[0].option_id, "delos.trace.capacity") != 0 ||
      audit.warnings[0].message == 0 ||
      strcmp(audit.warnings[0].message,
             "category path depth exceeds 3 levels") != 0) {
    goto done;
  }
  ok = 1;

done:
  confit_project_free(project);
  confit_schema_audit_clear(&audit);
  return ok;
}

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitOption *option;
  ConfitProfile *profile;
  ConfitTarget *target;
  char path[512];

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema/valid/basic")) {
    return 1;
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_load_project(path, &project, &diagnostic) != CONFIT_OK) {
    return 2;
  }
  if (strcmp(project->name, "delos") != 0 ||
      strcmp(project->version, "0.1.0") != 0 ||
      project->schema_version != 1U) {
    confit_project_free(project);
    return 3;
  }
  if (project->option_count != 8U) {
    confit_project_free(project);
    return 4;
  }
  if (project->profile_count != 2U || project->target_count != 2U) {
    confit_project_free(project);
    return 5;
  }

  option = confit_project_find_option(project, "delos.debug.ddc");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_BOOL ||
      option->default_value.kind != CONFIT_VALUE_BOOL ||
      option->default_value.as.bool_value != 0 ||
      strcmp(option->prompt, "Enable DDC") != 0 ||
      option->tag_count != 2U || option->dependency_count != 2U ||
      option->dependencies[0].kind != CONFIT_DEPENDENCY_RECOMMENDS ||
      strcmp(option->dependencies[0].option_id,
             "delos.internal.debug_gate") != 0 ||
      option->dependencies[1].kind != CONFIT_DEPENDENCY_FORCES) {
    confit_project_free(project);
    return 6;
  }

  option = confit_project_find_option(project, "delos.scheduler.task_slots");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_UINT ||
      option->default_value.kind != CONFIT_VALUE_UINT ||
      option->default_value.as.uint_value != 16U ||
      !option->has_range || option->range_min.as.uint_value != 1U ||
      option->range_max.as.uint_value != 128U ||
      option->dependency_count != 1U ||
      option->dependencies[0].kind != CONFIT_DEPENDENCY_CONFLICTS) {
    confit_project_free(project);
    return 7;
  }

  option = confit_project_find_option(project, "delos.target.board");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_ENUM ||
      option->default_value.kind != CONFIT_VALUE_ENUM ||
      strcmp(option->default_value.as.string_value, "host-sim") != 0 ||
      option->enum_value_count != 2U || option->dependency_count != 2U ||
      option->dependencies[0].kind != CONFIT_DEPENDENCY_REQUIRES ||
      option->dependencies[1].kind != CONFIT_DEPENDENCY_VISIBLE_IF) {
    confit_project_free(project);
    return 8;
  }

  option = confit_project_find_option(project, "delos.internal.debug_gate");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_BOOL ||
      option->prompt != 0 || option->dependency_count != 0U) {
    confit_project_free(project);
    return 9;
  }

  option = confit_project_find_option(project, "delos.memory.flash_base");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_HEX ||
      option->default_value.kind != CONFIT_VALUE_UINT ||
      option->default_value.as.uint_value != 0x08000000U ||
      option->range_max.as.uint_value != 0x080FFFFFU) {
    confit_project_free(project);
    return 10;
  }

  option = confit_project_find_option(project, "delos.sim.default_gain");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_FLOAT ||
      option->default_value.kind != CONFIT_VALUE_FLOAT ||
      option->default_value.as.float_value < 0.124 ||
      option->default_value.as.float_value > 0.126 ||
      option->range_max.as.float_value < 0.99) {
    confit_project_free(project);
    return 11;
  }

  option = confit_project_find_option(project, "delos.paths.config_root");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_PATH ||
      option->default_value.kind != CONFIT_VALUE_PATH ||
      strcmp(option->default_value.as.string_value,
             "build/generated/config") != 0) {
    confit_project_free(project);
    return 12;
  }

  profile = find_profile(project, "sim-dsh");
  if (profile == 0 || profile->base == 0 ||
      strcmp(profile->base, "debug") != 0 || profile->target == 0 ||
      strcmp(profile->target, "host-sim") != 0 ||
      profile->value_count != 4U) {
    confit_project_free(project);
    return 13;
  }
  if (strcmp(profile->values[0].option_id,
             "delos.scheduler.task_slots") != 0 ||
      profile->values[0].value.kind != CONFIT_VALUE_UINT ||
      profile->values[0].value.as.uint_value != 32U) {
    confit_project_free(project);
    return 14;
  }
  if (strcmp(profile->values[2].option_id, "delos.sim.default_gain") != 0 ||
      profile->values[2].value.kind != CONFIT_VALUE_FLOAT ||
      profile->values[2].value.as.float_value < 0.249 ||
      profile->values[2].value.as.float_value > 0.251) {
    confit_project_free(project);
    return 15;
  }

  profile = find_profile(project, "debug");
  if (profile == 0 || profile->base != 0 || profile->target != 0 ||
      profile->value_count != 2U ||
      strcmp(profile->values[0].option_id, "delos.debug.ddc") != 0 ||
      profile->values[0].value.kind != CONFIT_VALUE_BOOL ||
      profile->values[0].value.as.bool_value != 1) {
    confit_project_free(project);
    return 16;
  }

  target = find_target(project, "host-sim");
  if (target == 0 || target->claim_level == 0 ||
      strcmp(target->claim_level, "portability-probe") != 0 ||
      target->value_count != 2U ||
      strcmp(target->values[0].option_id, "delos.target.board") != 0 ||
      target->values[0].value.kind != CONFIT_VALUE_ENUM ||
      strcmp(target->values[0].value.as.string_value, "host-sim") != 0) {
    confit_project_free(project);
    return 17;
  }
  target = find_target(project, "qemu-mps2-an500");
  if (target == 0 || target->arch == 0 ||
      strcmp(target->arch, "armv7m") != 0 || target->value_count != 2U) {
    confit_project_free(project);
    return 18;
  }
  confit_project_free(project);

  if (!expect_schema_error("tests/fixtures/schema/invalid/duplicate",
                           "duplicate option id")) {
    return 19;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/unknown-field",
                           "unknown option field")) {
    return 20;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/missing-type",
                           "missing option type")) {
    return 21;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/invalid-id",
                           "invalid option id")) {
    return 22;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/bad-project-version",
                           "unsupported schema_version")) {
    return 23;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/out-of-range",
                           "default outside range")) {
    return 24;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/enum-candidate",
                           "enum default is not a candidate")) {
    return 25;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/nonfinite-float",
                           "float default must be finite")) {
    return 26;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/bad-range-type",
                           "range is only valid for numeric options")) {
    return 27;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/profile-unknown-base",
                           "unknown base profile")) {
    return 28;
  }
  if (!expect_schema_error(
          "tests/fixtures/schema/invalid/profile-unknown-option",
          "unknown value option")) {
    return 29;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/target-bad-value",
                           "invalid target value")) {
    return 30;
  }
  if (!expect_alias_canonicalization()) {
    return 31;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/bad-stability",
                           "invalid stability metadata")) {
    return 32;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/duplicate-alias",
                           "duplicate deprecated alias")) {
    return 33;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/bad-namespace",
                           "option id must use project namespace")) {
    return 34;
  }
  if (!expect_category_path_audit()) {
    return 35;
  }
  if (!expect_schema_error(
          "tests/fixtures/schema/invalid/category-leading-slash",
          "category path must be slash-separated, without empty segments, "
          "and at most 63 bytes")) {
    return 36;
  }
  if (!expect_schema_error(
          "tests/fixtures/schema/invalid/category-empty-segment",
          "category path must be slash-separated, without empty segments, "
          "and at most 63 bytes")) {
    return 37;
  }
  if (!expect_schema_error(
          "tests/fixtures/schema/invalid/category-too-long",
          "category path must be slash-separated, without empty segments, "
          "and at most 63 bytes")) {
    return 38;
  }

  return 0;
}
