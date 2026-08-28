#include "confit/diagnostic.h"
#include "confit/digest.h"
#include "confit/status.h"
#include "confit/toml.h"
#include "confit/version.h"

int main(void) {
  ConfitDiagnostic diagnostic;
  char digest[65];

  confit_diagnostic_init(&diagnostic);
  confit_sha256_text(confit_version_string(), digest);
  return diagnostic.status == CONFIT_OK && digest[0] != '\0' &&
                 CONFIT_SCHEMA_CONTRACT_VERSION == 6
             ? 0
             : 1;
}
