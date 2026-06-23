#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/model.h"
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

static char *copy_string(const char *text) {
  char *copy;
  size_t size;

  if (text == 0) {
    return 0;
  }

  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    return 0;
  }
  memcpy(copy, text, size + 1U);
  return copy;
}

static void override_init(ConfitNamedValue *override_value) {
  override_value->option_id = 0;
  confit_value_init(&override_value->value);
  override_value->source = 0;
}

static void override_clear(ConfitNamedValue *override_value) {
  free(override_value->option_id);
  confit_value_clear(&override_value->value);
  free(override_value->source);
  override_init(override_value);
}

static ConfitStatus override_set(ConfitNamedValue *override_value,
                                 const char *option_id,
                                 const ConfitValue *value) {
  override_clear(override_value);
  override_value->option_id = copy_string(option_id);
  override_value->source = copy_string("user");
  if (override_value->option_id == 0 || override_value->source == 0) {
    override_clear(override_value);
    return CONFIT_ERR_INTERNAL;
  }
  return confit_value_copy(&override_value->value, value);
}

static int expect_bool_value(const ConfitResolvedConfig *config,
                             const char *option_id, int expected,
                             const char *source) {
  const ConfitResolvedValue *value =
      confit_resolved_config_find(config, option_id);

  return value != 0 && value->value.kind == CONFIT_VALUE_BOOL &&
         value->value.as.bool_value == expected && value->source != 0 &&
         strcmp(value->source, source) == 0;
}

static int expect_uint_value(const ConfitResolvedConfig *config,
                             const char *option_id, uint64_t expected,
                             const char *source) {
  const ConfitResolvedValue *value =
      confit_resolved_config_find(config, option_id);

  return value != 0 && value->value.kind == CONFIT_VALUE_UINT &&
         value->value.as.uint_value == expected && value->source != 0 &&
         strcmp(value->source, source) == 0;
}

static int expect_float_value(const ConfitResolvedConfig *config,
                              const char *option_id, double min_value,
                              double max_value, const char *source) {
  const ConfitResolvedValue *value =
      confit_resolved_config_find(config, option_id);

  return value != 0 && value->value.kind == CONFIT_VALUE_FLOAT &&
         value->value.as.float_value >= min_value &&
         value->value.as.float_value <= max_value && value->source != 0 &&
         strcmp(value->source, source) == 0;
}

static int expect_string_value(const ConfitResolvedConfig *config,
                               const char *option_id, ConfitValueKind kind,
                               const char *expected, const char *source) {
  const ConfitResolvedValue *value =
      confit_resolved_config_find(config, option_id);

  return value != 0 && value->value.kind == kind &&
         value->value.as.string_value != 0 &&
         strcmp(value->value.as.string_value, expected) == 0 &&
         value->source != 0 && strcmp(value->source, source) == 0;
}

static int expect_order(const ConfitResolvedConfig *config) {
  static const char *const expected_ids[] = {
      "delos.debug.ddc",
      "delos.internal.debug_gate",
      "delos.memory.flash_base",
      "delos.output.name",
      "delos.paths.config_root",
      "delos.scheduler.task_slots",
      "delos.sim.default_gain",
      "delos.target.board",
  };
  size_t index;

  if (config->value_count != sizeof(expected_ids) / sizeof(expected_ids[0])) {
    return 0;
  }

  for (index = 0U; index < config->value_count; ++index) {
    if (config->values[index].option_id == 0 ||
        strcmp(config->values[index].option_id, expected_ids[index]) != 0) {
      return 0;
    }
  }
  return 1;
}

static int expect_resolve_error(const ConfitProject *project,
                                const ConfitNamedValue *overrides,
                                size_t override_count,
                                ConfitStatus expected_status,
                                const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitResolvedConfig *config;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  config = 0;
  status = confit_resolver_resolve(project, "sim-dsh", 0, overrides,
                                   override_count, &config, &diagnostic);
  confit_resolved_config_free(config);
  return status == expected_status &&
         confit_diagnostic_has_error(&diagnostic) &&
         diagnostic.message != 0 && strcmp(diagnostic.message, message) == 0;
}

static int set_override_bool(ConfitNamedValue *override_value,
                             const char *option_id, int bool_value) {
  ConfitValue value;
  ConfitStatus status;

  confit_value_init(&value);
  confit_value_set_bool(&value, bool_value);
  status = override_set(override_value, option_id, &value);
  confit_value_clear(&value);
  return status == CONFIT_OK;
}

static int set_override_string(ConfitNamedValue *override_value,
                               const char *option_id, const char *text) {
  ConfitValue value;
  ConfitStatus status;

  confit_value_init(&value);
  status = confit_value_set_string(&value, text);
  if (status == CONFIT_OK) {
    status = override_set(override_value, option_id, &value);
  }
  confit_value_clear(&value);
  return status == CONFIT_OK;
}

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitResolvedConfig *config;
  ConfitResolvedConfig *config_again;
  ConfitNamedValue override_value;
  char *json;
  char *golden_json;
  uint64_t hash;
  uint64_t hash_again;

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

  if (!expect_order(config)) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 3;
  }
  if (!expect_bool_value(config, "delos.debug.ddc", 1,
                         "profiles/debug.toml")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 4;
  }
  if (!expect_bool_value(config, "delos.internal.debug_gate", 0, "default")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 5;
  }
  if (!expect_uint_value(config, "delos.memory.flash_base", 0x08000000U,
                         "default")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 6;
  }
  if (!expect_string_value(config, "delos.output.name", CONFIT_VALUE_STRING,
                           "delos-host-sim", "targets/host-sim.toml")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 7;
  }
  if (!expect_string_value(config, "delos.paths.config_root",
                           CONFIT_VALUE_PATH,
                           "build/generated/config/delos/sim-dsh",
                           "profiles/sim-dsh.toml")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 8;
  }
  if (!expect_uint_value(config, "delos.scheduler.task_slots", 32U,
                         "profiles/sim-dsh.toml")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 9;
  }
  if (!expect_float_value(config, "delos.sim.default_gain", 0.249, 0.251,
                          "profiles/sim-dsh.toml")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 10;
  }
  if (!expect_string_value(config, "delos.target.board", CONFIT_VALUE_ENUM,
                           "host-sim", "profiles/sim-dsh.toml")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 11;
  }

  json = 0;
  if (confit_resolved_config_to_json(config, &json) != CONFIT_OK ||
      !read_fixture_text("tests/golden/resolver/sim-dsh.json",
                         &golden_json)) {
    confit_resolver_string_free(json);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 12;
  }
  if (strcmp(json, golden_json) != 0) {
    confit_resolver_string_free(json);
    free(golden_json);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 13;
  }
  confit_resolver_string_free(json);
  free(golden_json);

  hash = 0U;
  hash_again = 0U;
  config_again = 0;
  if (confit_resolved_config_hash(config, &hash) != CONFIT_OK ||
      confit_resolver_resolve(project, "sim-dsh", 0, 0, 0U, &config_again,
                              &diagnostic) != CONFIT_OK ||
      confit_resolved_config_hash(config_again, &hash_again) != CONFIT_OK ||
      hash == 0U || hash != hash_again) {
    confit_resolved_config_free(config_again);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 14;
  }
  confit_resolved_config_free(config_again);
  confit_resolved_config_free(config);

  config = 0;
  if (confit_resolver_resolve(project, "sim-dsh", "qemu-mps2-an500", 0, 0U,
                              &config, &diagnostic) != CONFIT_OK) {
    confit_project_free(project);
    return 15;
  }
  if (!expect_uint_value(config, "delos.scheduler.task_slots", 32U,
                         "profiles/sim-dsh.toml") ||
      !expect_string_value(config, "delos.target.board", CONFIT_VALUE_ENUM,
                           "host-sim", "profiles/sim-dsh.toml") ||
      !expect_string_value(config, "delos.output.name", CONFIT_VALUE_STRING,
                           "delos-debug", "profiles/debug.toml")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 16;
  }
  confit_resolved_config_free(config);

  override_init(&override_value);
  if (!set_override_string(&override_value, "delos.output.name", "manual")) {
    override_clear(&override_value);
    confit_project_free(project);
    return 17;
  }
  config = 0;
  if (confit_resolver_resolve(project, "sim-dsh", 0, &override_value, 1U,
                              &config, &diagnostic) != CONFIT_OK ||
      !expect_string_value(config, "delos.output.name", CONFIT_VALUE_STRING,
                           "manual", "user")) {
    confit_resolved_config_free(config);
    override_clear(&override_value);
    confit_project_free(project);
    return 18;
  }
  confit_resolved_config_free(config);
  override_clear(&override_value);

  override_init(&override_value);
  if (!set_override_bool(&override_value, "delos.debug.ddc", 0) ||
      !expect_resolve_error(project, &override_value, 1U,
                            CONFIT_ERR_DEPENDENCY,
                            "dependency not satisfied")) {
    override_clear(&override_value);
    confit_project_free(project);
    return 19;
  }
  override_clear(&override_value);

  override_init(&override_value);
  if (!set_override_bool(&override_value, "delos.internal.debug_gate", 1) ||
      !expect_resolve_error(project, &override_value, 1U, CONFIT_ERR_CONFLICT,
                            "conflict active")) {
    override_clear(&override_value);
    confit_project_free(project);
    return 20;
  }
  override_clear(&override_value);

  confit_project_free(project);
  return 0;
}
