#include "confit/config.h"
#include "confit/diagnostic.h"
#include "confit/digest.h"
#include "confit/emitter.h"
#include "confit/expression.h"
#include "confit/host.h"
#include "confit/input.h"
#include "confit/limits.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit/snapshot.h"
#include "confit/migration.h"
#include "confit/source.h"
#include "confit/status.h"
#include "confit/toml.h"
#include "confit/version.h"

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitHostBuffer buffer;
  ConfitHostLock lock;
  ConfitInputImage *image = 0;
  ConfitSourceGraph *graph = 0;
  ConfitSchemaProject *project = 0;
  ConfitDependencyPlan *dependency_plan = 0;
  ConfitDependencyEvaluation *dependency_evaluation = 0;
  ConfitEmission *emission = 0;
  ConfitResolution *resolution = 0;
  ConfitUserConfig *user_config = 0;
  ConfitUserDocument *user_document = 0;
  ConfitSnapshotPublication publication;
  ConfitValue value;
  char digest[65];

  confit_diagnostic_init(&diagnostic);
  confit_host_buffer_init(&buffer);
  confit_host_lock_init(&lock);
  confit_value_init(&value);
  publication.digest[0] = '\0';
  publication.reused_existing = 0;
  confit_sha256_text(confit_version_string(), digest);
  return diagnostic.status == CONFIT_OK && buffer.bytes == 0 &&
                 lock.descriptor == -1 && value.kind == CONFIT_VALUE_INVALID &&
                 image == 0 && graph == 0 && project == 0 &&
                 dependency_plan == 0 && dependency_evaluation == 0 &&
                 emission == 0 &&
                 resolution == 0 &&
                 user_config == 0 &&
                 user_document == 0 &&
                 publication.digest[0] == '\0' &&
                 digest[0] != '\0' && CONFIT_SCHEMA_CONTRACT_VERSION == 6 &&
                 CONFIT_LIMIT_CONFIG_SYMBOLS == 16384U
             ? 0
             : 1;
}
