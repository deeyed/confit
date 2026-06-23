#include "confit/status.h"

int confit_status_is_ok(ConfitStatus status) { return status == CONFIT_OK; }

int confit_status_exit_code(ConfitStatus status) {
  switch (status) {
  case CONFIT_OK:
  case CONFIT_ERR_INVALID_ARGUMENT:
  case CONFIT_ERR_PARSE:
  case CONFIT_ERR_SCHEMA:
  case CONFIT_ERR_DEPENDENCY:
  case CONFIT_ERR_COMPATIBILITY:
  case CONFIT_ERR_GENERATION:
  case CONFIT_ERR_INTERNAL:
    return (int)status;
  default:
    return (int)CONFIT_ERR_INTERNAL;
  }
}

const char *confit_status_name(ConfitStatus status) {
  switch (status) {
  case CONFIT_OK:
    return "ok";
  case CONFIT_ERR_INVALID_ARGUMENT:
    return "invalid argument";
  case CONFIT_ERR_PARSE:
    return "parse error";
  case CONFIT_ERR_SCHEMA:
    return "schema error";
  case CONFIT_ERR_DEPENDENCY:
    return "dependency or conflict error";
  case CONFIT_ERR_COMPATIBILITY:
    return "compatibility error";
  case CONFIT_ERR_GENERATION:
    return "generation error";
  case CONFIT_ERR_INTERNAL:
    return "internal error";
  default:
    return "unknown status";
  }
}
