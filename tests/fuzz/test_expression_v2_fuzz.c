#include <stdint.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/expression_v2.h"

static uint32_t next_state(uint32_t *state) {
  *state ^= *state << 13U;
  *state ^= *state >> 17U;
  *state ^= *state << 5U;
  return *state;
}

int main(void) {
  static const char *const seeds[] = {
      "true", "enabled(delos.debug.ddc)", "[1, 2, 3]", "a", "(true",
      "0x", "\"\\uD800\"", "true ? false : true", "!(!(!true))",
  };
  static const char alphabet[] =
      "abcdefghijklmnopqrstuvwxyz0123456789._[]()?,:!&|=<>+-*/%\\\" \t";
  ConfitV2ExpressionLimits limits = confit_v2_expression_default_limits();
  uint32_t state = 0xC0FFEEU;
  size_t index;

  limits.max_source_bytes = 64U;
  limits.max_tokens = 128U;
  limits.max_nodes = 128U;
  limits.max_nesting = 16U;
  for (index = 0U; index < sizeof(seeds) / sizeof(seeds[0]) + 1024U; ++index) {
    char generated[65];
    ConfitV2ExpressionText source;
    ConfitV2Expression *expression;
    ConfitDiagnostic diagnostic;
    size_t length;

    if (index < sizeof(seeds) / sizeof(seeds[0])) {
      source.text = (char *)seeds[index];
    } else {
      length = (size_t)(next_state(&state) % 64U);
      for (size_t cursor = 0U; cursor < length; ++cursor) {
        generated[cursor] =
            alphabet[next_state(&state) % (sizeof(alphabet) - 1U)];
      }
      generated[length] = '\0';
      source.text = generated;
    }
    memset(&source.span, 0, sizeof(source.span));
    source.span.path = "expression-fuzz";
    source.span.line = 1U;
    source.span.column = 1U;
    confit_diagnostic_init(&diagnostic);
    expression = 0;
    (void)confit_v2_expression_parse(&source, &limits, &expression, &diagnostic);
    confit_v2_expression_free(expression);
  }
  return 0;
}
