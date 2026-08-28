#include "confit/status.h"

int confit_status_is_ok(ConfitStatus status) { return status == CONFIT_OK; }

int confit_status_exit_code(ConfitStatus status) {
  switch (status) {
  case CONFIT_OK:
  case CONFIT_ERR_USAGE:
  case CONFIT_ERR_VALIDATION:
  case CONFIT_ERR_STALE:
  case CONFIT_ERR_IO:
  case CONFIT_ERR_TERMINAL:
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
  case CONFIT_ERR_USAGE:
    return "usage error";
  case CONFIT_ERR_VALIDATION:
    return "validation error";
  case CONFIT_ERR_STALE:
    return "missing or stale configuration";
  case CONFIT_ERR_IO:
    return "input or output error";
  case CONFIT_ERR_TERMINAL:
    return "terminal error";
  case CONFIT_ERR_INTERNAL:
    return "internal error";
  default:
    return "unknown status";
  }
}
