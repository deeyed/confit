#include "confit/emitter.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/expression.h"
#include "confit/limits.h"
#include "test_assert.h"

typedef struct TestDefinition {
  const char *symbol;
  ConfitValueKind kind;
  int boolean;
  int64_t integer;
  uint64_t hexadecimal;
  const char *text;
  const char *const *enum_values;
  size_t enum_value_count;
} TestDefinition;

typedef struct TestResolved {
  ConfitCatalog *catalog;
  ConfitDependencyPlan *plan;
  ConfitResolution *resolution;
} TestResolved;

typedef struct FailingAllocatorState {
  size_t calls;
  size_t fail_at;
  size_t live;
} FailingAllocatorState;

static const char *const kLevels[] = {".include", "normal", "verbose"};

static const TestDefinition kFullDefinitions[] = {
    {"DEVICE_ID", CONFIT_VALUE_HEX, 0, 0, UINT64_C(0x10e8), 0, 0, 0U},
    {"ENABLE_METRICS", CONFIT_VALUE_BOOL, 1, 0, 0U, 0, 0, 0U},
    {"INSTANCE_LABEL", CONFIT_VALUE_STRING, 0, 0, 0U,
     "line\n.include ${VALUE} # ?" "?/ \"quote\"\\caf\303\251", 0, 0U},
    {"LOG_LEVEL", CONFIT_VALUE_ENUM, 0, 0, 0U, ".include", kLevels, 3U},
    {"WORKER_COUNT", CONFIT_VALUE_INT, 0, -4, 0U, 0, 0, 0U},
};

static const TestDefinition kReorderedFullDefinitions[] = {
    {"WORKER_COUNT", CONFIT_VALUE_INT, 0, -4, 0U, 0, 0, 0U},
    {"LOG_LEVEL", CONFIT_VALUE_ENUM, 0, 0, 0U, ".include", kLevels, 3U},
    {"INSTANCE_LABEL", CONFIT_VALUE_STRING, 0, 0, 0U,
     "line\n.include ${VALUE} # ?" "?/ \"quote\"\\caf\303\251", 0, 0U},
    {"ENABLE_METRICS", CONFIT_VALUE_BOOL, 1, 0, 0U, 0, 0, 0U},
    {"DEVICE_ID", CONFIT_VALUE_HEX, 0, 0, UINT64_C(0x10e8), 0, 0, 0U},
};

static const TestDefinition kMakeDefinitions[] = {
    {"DEVICE_ID", CONFIT_VALUE_HEX, 0, 0, UINT64_C(0x10e8), 0, 0, 0U},
    {"ENABLE_METRICS", CONFIT_VALUE_BOOL, 1, 0, 0U, 0, 0, 0U},
    {"LOG_LEVEL", CONFIT_VALUE_ENUM, 0, 0, 0U, ".include", kLevels, 3U},
    {"WORKER_COUNT", CONFIT_VALUE_INT, 0, -4, 0U, 0, 0, 0U},
};

static ConfitStatus set_value(const TestDefinition *definition,
                              ConfitValue *value,
                              ConfitDiagnostic *diagnostic) {
  switch (definition->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_value_set_bool(value, definition->boolean, 0, diagnostic);
  case CONFIT_VALUE_INT:
    return confit_value_set_int(value, definition->integer, 0, diagnostic);
  case CONFIT_VALUE_HEX:
    return confit_value_set_hex(value, definition->hexadecimal, 0,
                                diagnostic);
  case CONFIT_VALUE_STRING:
    return confit_value_set_string(value, definition->text,
                                   strlen(definition->text), 0, diagnostic);
  case CONFIT_VALUE_ENUM:
    return confit_value_set_enum(value, definition->text,
                                 strlen(definition->text), 0, diagnostic);
  case CONFIT_VALUE_INVALID:
  default:
    return CONFIT_ERR_INTERNAL;
  }
}

static TestResolved make_resolution(const TestDefinition *definitions,
                                    size_t count) {
  TestResolved result;
  ConfitSourceFragmentSpec fragment;
  ConfitDiagnostic diagnostic;
  size_t index;
  memset(&result, 0, sizeof(result));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_catalog_create(0, &result.catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(
                         result.catalog, "Emitter test", &diagnostic) ==
                     CONFIT_OK);
  fragment.path = "config/emitter.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(
                         result.catalog, &fragment, 0, &diagnostic) ==
                     CONFIT_OK);
  for (index = 0U; index < count; ++index) {
    ConfitConfigSpec spec;
    ConfitValue default_value;
    confit_value_init(&default_value);
    CONFIT_TEST_ASSERT(set_value(&definitions[index], &default_value,
                                 &diagnostic) == CONFIT_OK);
    memset(&spec, 0, sizeof(spec));
    spec.fragment = 0U;
    spec.menu = CONFIT_INDEX_NONE;
    spec.symbol = definitions[index].symbol;
    spec.kind = definitions[index].kind;
    spec.prompt = definitions[index].symbol;
    spec.help = "Emit one generic typed configuration value.";
    spec.default_value = &default_value;
    spec.enum_values = definitions[index].enum_values;
    spec.enum_value_count = definitions[index].enum_value_count;
    spec.declaration.path = "config/emitter.toml";
    spec.declaration.line = index + 1U;
    spec.declaration.column = 1U;
    CONFIT_TEST_ASSERT(confit_catalog_add_config(
                           result.catalog, &spec, 0, &diagnostic) ==
                       CONFIT_OK);
    confit_value_destroy(&default_value);
  }
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(
                         result.catalog, 0, &result.plan, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolve(result.catalog, result.plan, 0, 0U, 0,
                                    &result.resolution, &diagnostic) ==
                     CONFIT_OK);
  return result;
}

static void destroy_resolution(TestResolved *resolved) {
  confit_resolution_destroy(resolved->resolution);
  confit_dependency_plan_destroy(resolved->plan);
  confit_catalog_destroy(resolved->catalog);
  memset(resolved, 0, sizeof(*resolved));
}

static void expect_artifact(const ConfitEmission *emission,
                            ConfitEmitterKind kind, const char *role,
                            const char *name, const char *expected) {
  ConfitEmittedArtifactView artifact;
  CONFIT_TEST_ASSERT(confit_emission_find_artifact(emission, kind,
                                                   &artifact));
  CONFIT_TEST_ASSERT(strcmp(artifact.role, role) == 0);
  CONFIT_TEST_ASSERT(strcmp(artifact.name, name) == 0);
  CONFIT_TEST_ASSERT(artifact.printable == 1);
  CONFIT_TEST_ASSERT(artifact.size == strlen(expected));
  CONFIT_TEST_ASSERT(memcmp(artifact.bytes, expected, artifact.size) == 0);
  CONFIT_TEST_ASSERT(artifact.bytes[artifact.size] == '\0');
}

static void test_requested_only_and_target_escaping(void) {
  static const char expected_c[] =
      "#ifndef CONFIT_GENERATED_VALUES_H\n"
      "#define CONFIT_GENERATED_VALUES_H\n\n"
      "#define CONFIG_DEVICE_ID 0x10e8\n"
      "#define CONFIG_ENABLE_METRICS 1\n"
      "#define CONFIG_INSTANCE_LABEL "
      "\"line\\012.include ${VALUE} # \\077\\077/ "
      "\\\"quote\\\"\\\\caf\\303\\251\"\n"
      "#define CONFIG_LOG_LEVEL \".include\"\n"
      "#define CONFIG_WORKER_COUNT -4\n\n"
      "#endif\n";
  static const char expected_json[] =
      "{\"schema_version\":6,\"values\":["
      "{\"symbol\":\"DEVICE_ID\",\"type\":\"hex\",\"value\":\"0x10e8\","
      "\"default\":\"0x10e8\",\"origin\":\"default\",\"available\":true},"
      "{\"symbol\":\"ENABLE_METRICS\",\"type\":\"bool\",\"value\":true,"
      "\"default\":true,\"origin\":\"default\",\"available\":true},"
      "{\"symbol\":\"INSTANCE_LABEL\",\"type\":\"string\","
      "\"value\":\"line\\n.include ${VALUE} # ?" "?/ "
      "\\\"quote\\\"\\\\caf\303\251\","
      "\"default\":\"line\\n.include ${VALUE} # ?" "?/ "
      "\\\"quote\\\"\\\\caf\303\251\","
      "\"origin\":\"default\",\"available\":true},"
      "{\"symbol\":\"LOG_LEVEL\",\"type\":\"enum\",\"value\":\".include\","
      "\"default\":\".include\",\"origin\":\"default\",\"available\":true},"
      "{\"symbol\":\"WORKER_COUNT\",\"type\":\"int\",\"value\":-4,"
      "\"default\":-4,\"origin\":\"default\",\"available\":true}]}\n";
  TestResolved full = make_resolution(
      kFullDefinitions, sizeof(kFullDefinitions) / sizeof(kFullDefinitions[0]));
  ConfitEmitRequest request;
  ConfitEmission *emission = 0;
  ConfitEmittedArtifactView first;
  ConfitEmittedArtifactView second;
  ConfitDiagnostic diagnostic;
  memset(&request, 0, sizeof(request));
  request.emit_c_header = 1;
  request.emit_json = 1;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_emit(full.resolution, &request, 0, &emission,
                                 &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_emission_artifact_count(emission) == 2U);
  CONFIT_TEST_ASSERT(confit_emission_artifact_at(emission, 0U, &first));
  CONFIT_TEST_ASSERT(confit_emission_artifact_at(emission, 1U, &second));
  CONFIT_TEST_ASSERT(first.kind == CONFIT_EMITTER_C_HEADER &&
                     second.kind == CONFIT_EMITTER_JSON);
  expect_artifact(emission, CONFIT_EMITTER_C_HEADER, "c-header-values",
                  "values.h", expected_c);
  expect_artifact(emission, CONFIT_EMITTER_JSON, "resolved-values",
                  "resolved-values.json", expected_json);
  confit_emission_destroy(emission);

  memset(&request, 0, sizeof(request));
  request.emit_json = 1;
  emission = 0;
  CONFIT_TEST_ASSERT(confit_emit(full.resolution, &request, 0, &emission,
                                 &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_emission_artifact_count(emission) == 1U);
  CONFIT_TEST_ASSERT(!confit_emission_find_artifact(
      emission, CONFIT_EMITTER_MAKE, &first));
  CONFIT_TEST_ASSERT(!confit_emission_find_artifact(
      emission, CONFIT_EMITTER_C_HEADER, &first));
  expect_artifact(emission, CONFIT_EMITTER_JSON, "resolved-values",
                  "resolved-values.json", expected_json);
  confit_emission_destroy(emission);
  destroy_resolution(&full);
}

static void test_make_closed_literals_and_whole_failure(void) {
  static const char expected_make[] =
      "CONFIG_DEVICE_ID=0x10e8\n"
      "CONFIG_ENABLE_METRICS=true\n"
      "CONFIG_LOG_LEVEL=.include\n"
      "CONFIG_WORKER_COUNT=-4\n";
  TestResolved make = make_resolution(
      kMakeDefinitions, sizeof(kMakeDefinitions) / sizeof(kMakeDefinitions[0]));
  TestResolved full = make_resolution(
      kFullDefinitions, sizeof(kFullDefinitions) / sizeof(kFullDefinitions[0]));
  ConfitEmitRequest request;
  ConfitEmission *emission = 0;
  ConfitDiagnostic diagnostic;
  memset(&request, 0, sizeof(request));
  request.emit_make = 1;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_emit(make.resolution, &request, 0, &emission,
                                 &diagnostic) == CONFIT_OK);
  expect_artifact(emission, CONFIT_EMITTER_MAKE, "make-values", "values.mk",
                  expected_make);
  confit_emission_destroy(emission);

  memset(&request, 0, sizeof(request));
  request.emit_make = 1;
  request.emit_c_header = 1;
  emission = (ConfitEmission *)(uintptr_t)1U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_emit(full.resolution, &request, 0, &emission,
                                 &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(emission == 0 && diagnostic.path != 0 &&
                     strcmp(diagnostic.path, "config/emitter.toml") == 0 &&
                     strcmp(diagnostic.message,
                            "Make output does not support string "
                            "configuration values") == 0);
  destroy_resolution(&full);
  destroy_resolution(&make);
}

static void test_order_independence_and_defensive_enum_validation(void) {
  TestResolved first = make_resolution(
      kFullDefinitions, sizeof(kFullDefinitions) / sizeof(kFullDefinitions[0]));
  TestResolved reordered = make_resolution(
      kReorderedFullDefinitions,
      sizeof(kReorderedFullDefinitions) / sizeof(kReorderedFullDefinitions[0]));
  TestResolved make = make_resolution(
      kMakeDefinitions, sizeof(kMakeDefinitions) / sizeof(kMakeDefinitions[0]));
  ConfitEmitRequest request;
  ConfitEmission *first_emission = 0;
  ConfitEmission *second_emission = 0;
  ConfitEmittedArtifactView first_json;
  ConfitEmittedArtifactView second_json;
  const ConfitResolvedValue *level = 0;
  ConfitResolvedValue *mutable_level;
  ConfitDiagnostic diagnostic;
  char saved;
  memset(&request, 0, sizeof(request));
  request.emit_json = 1;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_emit(first.resolution, &request, 0,
                                 &first_emission, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_emit(reordered.resolution, &request, 0,
                                 &second_emission, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_emission_find_artifact(
      first_emission, CONFIT_EMITTER_JSON, &first_json));
  CONFIT_TEST_ASSERT(confit_emission_find_artifact(
      second_emission, CONFIT_EMITTER_JSON, &second_json));
  CONFIT_TEST_ASSERT(first_json.size == second_json.size &&
                     memcmp(first_json.bytes, second_json.bytes,
                            first_json.size) == 0);
  confit_emission_destroy(second_emission);
  confit_emission_destroy(first_emission);

  CONFIT_TEST_ASSERT(confit_resolution_find_value(make.resolution,
                                                  "LOG_LEVEL", &level));
  mutable_level = (ConfitResolvedValue *)(uintptr_t)level;
  saved = mutable_level->effective_value.data.text.data[0];
  mutable_level->effective_value.data.text.data[0] = '#';
  memset(&request, 0, sizeof(request));
  request.emit_make = 1;
  first_emission = (ConfitEmission *)(uintptr_t)1U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_emit(make.resolution, &request, 0,
                                 &first_emission, &diagnostic) ==
                     CONFIT_ERR_INTERNAL);
  CONFIT_TEST_ASSERT(first_emission == 0);
  mutable_level->effective_value.data.text.data[0] = saved;
  destroy_resolution(&make);
  destroy_resolution(&reordered);
  destroy_resolution(&first);
}

static void *failing_allocate(void *context, size_t size) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  void *allocation;
  if (state->calls++ == state->fail_at) return 0;
  allocation = malloc(size);
  if (allocation != 0) state->live += 1U;
  return allocation;
}

static void failing_deallocate(void *context, void *pointer) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  CONFIT_TEST_ASSERT(pointer != 0 && state->live > 0U);
  state->live -= 1U;
  free(pointer);
}

static void test_transactional_failure_and_usage(void) {
  TestResolved make = make_resolution(
      kMakeDefinitions, sizeof(kMakeDefinitions) / sizeof(kMakeDefinitions[0]));
  ConfitEmitRequest request;
  ConfitDiagnostic diagnostic;
  size_t fail_at;
  int observed_success = 0;
  memset(&request, 0, sizeof(request));
  confit_diagnostic_init(&diagnostic);
  {
    ConfitEmission *emission = (ConfitEmission *)(uintptr_t)1U;
    CONFIT_TEST_ASSERT(confit_emit(make.resolution, &request, 0, &emission,
                                   &diagnostic) == CONFIT_ERR_USAGE);
    CONFIT_TEST_ASSERT(emission == 0);
  }
  request.emit_make = 1;
  request.emit_c_header = 1;
  request.emit_json = 1;
  for (fail_at = 0U; fail_at < 16U; ++fail_at) {
    FailingAllocatorState state;
    ConfitAllocator allocator;
    ConfitEmission *emission = (ConfitEmission *)(uintptr_t)1U;
    ConfitStatus status;
    memset(&state, 0, sizeof(state));
    state.fail_at = fail_at;
    allocator.context = &state;
    allocator.allocate = failing_allocate;
    allocator.deallocate = failing_deallocate;
    confit_diagnostic_init(&diagnostic);
    status = confit_emit(make.resolution, &request, &allocator, &emission,
                         &diagnostic);
    if (status == CONFIT_OK) {
      observed_success = 1;
      confit_emission_destroy(emission);
      CONFIT_TEST_ASSERT(state.live == 0U);
      break;
    }
    CONFIT_TEST_ASSERT(emission == 0 && state.live == 0U);
  }
  CONFIT_TEST_ASSERT(observed_success);
  destroy_resolution(&make);
}

static void test_public_maximum_output_rejection(void) {
  ConfitCatalog *catalog = 0;
  ConfitDependencyPlan *plan = 0;
  ConfitResolution *resolution = 0;
  ConfitValue default_value;
  ConfitSourceFragmentSpec fragment;
  ConfitConfigSpec spec;
  ConfitEmitRequest request;
  ConfitEmission *emission = (ConfitEmission *)(uintptr_t)1U;
  ConfitDiagnostic diagnostic;
  char text[CONFIT_LIMIT_STRING_BYTES + 1U];
  char symbol[16];
  size_t index;
  memset(text, '\n', CONFIT_LIMIT_STRING_BYTES);
  text[CONFIT_LIMIT_STRING_BYTES] = '\0';
  confit_value_init(&default_value);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_value_set_string(
                         &default_value, text, CONFIT_LIMIT_STRING_BYTES, 0,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_create(0, &catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(
                         catalog, "Maximum emitter output", &diagnostic) ==
                     CONFIT_OK);
  fragment.path = "config/maximum-emitter.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                 &diagnostic) == CONFIT_OK);
  memset(&spec, 0, sizeof(spec));
  spec.fragment = 0U;
  spec.menu = CONFIT_INDEX_NONE;
  spec.kind = CONFIT_VALUE_STRING;
  spec.help = "Exercise the exact public emitted-artifact byte ceiling.";
  spec.default_value = &default_value;
  spec.declaration.path = "config/maximum-emitter.toml";
  spec.declaration.column = 1U;
  for (index = 0U; index < 4096U; ++index) {
    CONFIT_TEST_ASSERT(snprintf(symbol, sizeof(symbol), "S%04zu", index) > 0);
    spec.symbol = symbol;
    spec.prompt = symbol;
    spec.declaration.line = index + 1U;
    CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &spec, 0,
                                                 &diagnostic) == CONFIT_OK);
  }
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, 0, 0U, 0, &resolution,
                                    &diagnostic) == CONFIT_OK);
  memset(&request, 0, sizeof(request));
  request.emit_c_header = 1;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_emit(resolution, &request, 0, &emission,
                                 &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(emission == 0 && diagnostic.message != 0 &&
                     strcmp(diagnostic.message,
                            "emitted artifact exceeds the public byte limit") ==
                         0);
  confit_resolution_destroy(resolution);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
  confit_value_destroy(&default_value);
}

int main(void) {
  test_requested_only_and_target_escaping();
  test_make_closed_literals_and_whole_failure();
  test_order_independence_and_defensive_enum_validation();
  test_transactional_failure_and_usage();
  test_public_maximum_output_rejection();
  return 0;
}
