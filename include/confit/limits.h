#ifndef CONFIT_LIMITS_H
#define CONFIT_LIMITS_H

#include <stddef.h>

/*
 * Public schema 6 resource ceilings.  Every implementation layer includes
 * this header instead of restating a numeric bound.  A ceiling is inclusive;
 * one unit beyond it is a deterministic error, never a truncation request.
 */
#define CONFIT_LIMIT_TOML_FILE_BYTES ((size_t)1024U * (size_t)1024U)
#define CONFIT_LIMIT_TOTAL_INPUT_BYTES                                        \
  ((size_t)64U * (size_t)1024U * (size_t)1024U)
#define CONFIT_LIMIT_SOURCE_FRAGMENTS ((size_t)4096U)
#define CONFIT_LIMIT_SOURCE_EDGES ((size_t)16384U)
#define CONFIT_LIMIT_INCLUDE_DEPTH ((size_t)64U)
#define CONFIT_LIMIT_MENUS ((size_t)4096U)
#define CONFIT_LIMIT_VISIBLE_MENU_DEPTH ((size_t)3U)
#define CONFIT_LIMIT_CONFIG_SYMBOLS ((size_t)16384U)
#define CONFIT_LIMIT_SOURCE_PATH_BYTES ((size_t)1024U)
#define CONFIT_LIMIT_PROMPT_BYTES ((size_t)256U)
#define CONFIT_LIMIT_HELP_BYTES ((size_t)8192U)
#define CONFIT_LIMIT_STRING_BYTES ((size_t)4096U)
#define CONFIT_LIMIT_ENUM_VALUES ((size_t)256U)
#define CONFIT_LIMIT_ENUM_ATOM_BYTES ((size_t)128U)
#define CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES ((size_t)4096U)
#define CONFIT_LIMIT_DEPENDENCY_AST_NODES ((size_t)512U)
#define CONFIT_LIMIT_DEPENDENCY_NESTING ((size_t)32U)
#define CONFIT_LIMIT_DIAGNOSTICS ((size_t)1024U)
#define CONFIT_LIMIT_RENDER_COLUMNS ((size_t)512U)
#define CONFIT_LIMIT_RENDER_ROWS ((size_t)256U)

#endif /* CONFIT_LIMITS_H */
