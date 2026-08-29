#include <stdint.h>
#include <string.h>

#include "confit/expression.h"
#include "confit/limits.h"

#if !defined(CONFIT_LIBFUZZER)
static uint32_t next_state(uint32_t *state) {
  *state ^= *state << 13U;
  *state ^= *state >> 17U;
  *state ^= *state << 5U;
  return *state;
}
#endif

static int add_boolean(ConfitCatalog *catalog, const char *symbol,
                       const char *dependency, int value,
                       ConfitDiagnostic *diagnostic) {
  ConfitConfigSpec spec;
  ConfitValue default_value;
  ConfitStatus status;
  confit_value_init(&default_value);
  status = confit_value_set_bool(&default_value, value, 0, diagnostic);
  if (status != CONFIT_OK)
    return 0;
  memset(&spec, 0, sizeof(spec));
  spec.fragment = 0U;
  spec.menu = CONFIT_INDEX_NONE;
  spec.symbol = symbol;
  spec.kind = CONFIT_VALUE_BOOL;
  spec.prompt = symbol;
  spec.help = "Exercise the bounded expression parser.";
  spec.default_value = &default_value;
  spec.dependency_text = dependency;
  spec.declaration.path = "fuzz/expression.toml";
  spec.declaration.line = 1U;
  spec.declaration.column = 1U;
  status = confit_catalog_add_config(catalog, &spec, 0, diagnostic);
  confit_value_destroy(&default_value);
  return status == CONFIT_OK;
}

static int compile_one(const char *expression) {
  ConfitCatalog *catalog = 0;
  ConfitDependencyPlan *plan = 0;
  ConfitDiagnostic diagnostic;
  ConfitSourceFragmentSpec fragment;
  ConfitStatus status;
  confit_diagnostic_init(&diagnostic);
  if (confit_catalog_create(0, &catalog, &diagnostic) != CONFIT_OK ||
      confit_catalog_set_mainmenu(catalog, "Fuzz", &diagnostic) != CONFIT_OK) {
    confit_catalog_destroy(catalog);
    return 0;
  }
  fragment.path = "fuzz/expression.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  if (confit_catalog_add_fragment(catalog, &fragment, 0, &diagnostic) !=
          CONFIT_OK ||
      !add_boolean(catalog, "BASE", 0, 1, &diagnostic) ||
      !add_boolean(catalog, "FEATURE", expression, 0, &diagnostic)) {
    confit_catalog_destroy(catalog);
    return 0;
  }
  status = confit_dependency_plan_create(catalog, 0, &plan, &diagnostic);
  if (status == CONFIT_OK) {
    ConfitValue values[2];
    ConfitDependencyEvaluation *evaluation = 0;
    ConfitConfigView view;
    size_t index;
    for (index = 0U; index < 2U; ++index) {
      confit_value_init(&values[index]);
      if (!confit_catalog_config_at(catalog, index, &view) ||
          confit_value_copy(&values[index], view.default_value, 0,
                            &diagnostic) != CONFIT_OK) {
        while (index > 0U)
          confit_value_destroy(&values[--index]);
        confit_dependency_plan_destroy(plan);
        confit_catalog_destroy(catalog);
        return 0;
      }
    }
    status = confit_dependency_plan_evaluate(plan, 1U, values, 2U, 0,
                                             &evaluation, &diagnostic);
    confit_dependency_evaluation_destroy(evaluation);
    confit_value_destroy(&values[1]);
    confit_value_destroy(&values[0]);
  }
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
  return status == CONFIT_OK || status == CONFIT_ERR_VALIDATION ||
         status == CONFIT_ERR_INTERNAL;
}

#if defined(CONFIT_LIBFUZZER)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char expression[CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES + 1U];

  if (size > CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES)
    return 0;
  if (size > 0U)
    memcpy(expression, data, size);
  expression[size] = '\0';
  (void)compile_one(expression);
  return 0;
}
#else
int main(void) {
  static const char *const corpus[] = {
      "",
      "BASE",
      "!BASE",
      "BASE && BASE",
      "BASE || BASE",
      "(BASE)",
      "BASE == true",
      "BASE != false",
      "UNKNOWN",
      "BASE + BASE",
      "((((BASE))))",
      "BASE == \"x\"",
  };
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_!&|=() \"+-xabcdeftruefalse";
  uint32_t state = UINT32_C(0x6c11a5e5);
  size_t index;
  for (index = 0U; index < sizeof(corpus) / sizeof(corpus[0]); ++index)
    if (!compile_one(corpus[index]))
      return 2;
  for (index = 0U; index < 2048U; ++index) {
    char generated[257];
    size_t length = (size_t)(next_state(&state) % 256U);
    size_t cursor;
    for (cursor = 0U; cursor < length; ++cursor)
      generated[cursor] =
          alphabet[next_state(&state) % (sizeof(alphabet) - 1U)];
    generated[length] = '\0';
    if (!compile_one(generated))
      return 3;
  }
  return 0;
}
#endif
