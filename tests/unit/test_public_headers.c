#include "confit/diagnostic.h"
#include "confit/digest.h"
#include "confit/limits.h"
#include "confit/model.h"
#include "confit/status.h"
#include "confit/toml.h"
#include "confit/version.h"

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitValue value;
  char digest[65];

  confit_diagnostic_init(&diagnostic);
  confit_value_init(&value);
  confit_sha256_text(confit_version_string(), digest);
  return diagnostic.status == CONFIT_OK && value.kind == CONFIT_VALUE_INVALID &&
                 digest[0] != '\0' && CONFIT_SCHEMA_CONTRACT_VERSION == 6 &&
                 CONFIT_LIMIT_CONFIG_SYMBOLS == 16384U
             ? 0
             : 1;
}
