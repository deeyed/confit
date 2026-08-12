#ifndef CONFIT_SCHEMA_V5_CONFIG_INTERNAL_H
#define CONFIT_SCHEMA_V5_CONFIG_INTERNAL_H

#include "confit/config_v5.h"

#define CONFIT_V5_MAX_PATH_BYTES 4095U
#define CONFIT_V5_MAX_TEXT_BYTES 512U
#define CONFIT_V5_MAX_SYMBOL_BYTES 127U
#define CONFIT_V5_MAX_OPTIONS 512U
#define CONFIT_V5_MAX_MENUS 256U
#define CONFIT_V5_MAX_CHOICES 256U
#define CONFIT_V5_MAX_RULES 512U
#define CONFIT_V5_MAX_DOCUMENTS 1024U
#define CONFIT_V5_MAX_DISCOVERY_ENTRIES 32768U
#define CONFIT_V5_MAX_DISCOVERY_DEPTH 32U
#define CONFIT_V5_MAX_FILE_BYTES (128U * 1024U)
#define CONFIT_V5_MAX_TOTAL_BYTES (16U * 1024U * 1024U)
#define CONFIT_V5_MAX_TAGS 32U
#define CONFIT_V5_MAX_VALUES 128U
#define CONFIT_V5_MAX_TOTAL_EDGES 4096U
#define CONFIT_V5_MAX_GRAPH_DEPTH 32U
#define CONFIT_V5_MAX_ASSIGNMENTS 512U
#define CONFIT_V5_MAX_COMMON_ROOTS 64U

typedef struct ConfitV5OwnedSpan {
  char *path;
  size_t line;
  size_t column;
} ConfitV5OwnedSpan;

typedef struct ConfitV5StringList {
  char **items;
  ConfitV5OwnedSpan *spans;
  size_t count;
} ConfitV5StringList;

typedef struct ConfitV5Option {
  char *symbol;
  char *projection;
  ConfitV5OptionType type;
  char *prompt;
  char *help;
  char *menu;
  int64_t menu_order;
  char *owner;
  char *since;
  char *stability;
  char *default_value;
  int64_t minimum;
  int64_t maximum;
  ConfitV5StringList tags;
  ConfitV5StringList allowed;
  ConfitV5StringList values;
  ConfitV5StringList enabled_values;
  ConfitV5StringList prerequisites;
  ConfitV5StringList visible_all;
  ConfitV5OwnedSpan declaration;
  ConfitV5OwnedSpan owner_source;
  ConfitV5OwnedSpan since_source;
  ConfitV5OwnedSpan stability_source;
  ConfitV5OwnedSpan tags_source;
  ConfitV5OwnedSpan menu_order_source;
  ConfitV5OwnedSpan default_source;
} ConfitV5Option;

typedef struct ConfitV5Menu {
  char *id;
  char *prompt;
  char *help;
  char *parent;
  int64_t order;
  ConfitV5OwnedSpan source;
} ConfitV5Menu;

typedef struct ConfitV5Choice {
  char *symbol;
  char *prompt;
  char *help;
  ConfitV5StringList members;
  ConfitV5ChoiceCardinality cardinality;
  ConfitV5OwnedSpan source;
} ConfitV5Choice;

typedef struct ConfitV5Rule {
  ConfitV5StringList if_all;
  ConfitV5StringList require_all;
  char *message;
  ConfitV5OwnedSpan source;
} ConfitV5Rule;

typedef struct ConfitV5Defaults {
  char *owner;
  char *since;
  char *stability;
  ConfitV5StringList tags;
  int has_menu_order;
  int64_t menu_order;
  ConfitV5OwnedSpan owner_source;
  ConfitV5OwnedSpan since_source;
  ConfitV5OwnedSpan stability_source;
  ConfitV5OwnedSpan tags_source;
  ConfitV5OwnedSpan menu_order_source;
} ConfitV5Defaults;

typedef enum ConfitV5Role {
  CONFIT_V5_ROLE_OPTIONS = 0,
  CONFIT_V5_ROLE_MENUS,
  CONFIT_V5_ROLE_CHOICES,
  CONFIT_V5_ROLE_CONSTRAINTS,
  CONFIT_V5_ROLE_COUNT,
} ConfitV5Role;

typedef struct ConfitV5RoleRoots {
  char **items;
  size_t count;
} ConfitV5RoleRoots;

typedef struct ConfitV5OwnedAssignment {
  char *symbol;
  char *value;
  ConfitV5OwnedSpan source;
} ConfitV5OwnedAssignment;

struct ConfitV5Catalog {
  char *repository_root;
  char *project_path;
  char *project_name;
  char *project_namespace;
  char *architecture;
  char *kernconf;
  char *architecture_root;
  char *board_root;
  char *kernconf_root;
  char *kernconf_path;
  ConfitV5RoleRoots roots[CONFIT_V5_ROLE_COUNT];
  ConfitV5Defaults defaults;
  ConfitV5Option *options;
  size_t option_count;
  ConfitV5Menu *menus;
  size_t menu_count;
  ConfitV5Choice *choices;
  size_t choice_count;
  ConfitV5Rule *rules;
  size_t rule_count;
  ConfitV5OwnedAssignment *assignments;
  size_t assignment_count;
  char **documents;
  size_t document_count;
  size_t discovery_entry_count;
  size_t total_edges;
  size_t total_bytes;
};

typedef struct ConfitV5EffectiveValue {
  char *symbol;
  char *value;
  int enabled;
  ConfitV5OwnedSpan source;
} ConfitV5EffectiveValue;

typedef struct ConfitV5Reason {
  ConfitV5ReasonKind kind;
  int satisfied;
  char *subject;
  char *cause;
  ConfitV5OwnedSpan source;
} ConfitV5Reason;

struct ConfitV5Evaluation {
  ConfitV5EffectiveValue *values;
  size_t value_count;
  ConfitV5Reason *reasons;
  size_t reason_count;
};

char *confit_v5_copy(const char *text);
void confit_v5_owned_span_clear(ConfitV5OwnedSpan *span);
int confit_v5_owned_span_set(ConfitV5OwnedSpan *span, const char *path,
                            size_t line, size_t column);
void confit_v5_string_list_clear(ConfitV5StringList *list);
const ConfitV5Option *confit_v5_find_option(const ConfitV5Catalog *catalog,
                                            const char *symbol);
int confit_v5_symbol_valid(const char *text);
int confit_v5_namespace_valid(const char *text);
int confit_v5_option_value_valid(const ConfitV5Option *option,
                                 const char *value, int *out_enabled);
ConfitStatus confit_v5_evaluation_add_reason(
    ConfitV5Evaluation *evaluation, ConfitV5ReasonKind kind, int satisfied,
    const char *subject, const char *cause, const ConfitV5OwnedSpan *source);
void confit_v5_set_diagnostic(ConfitDiagnostic *diagnostic,
                              ConfitStatus status,
                              const ConfitV5OwnedSpan *source,
                              const char *message);

#endif /* CONFIT_SCHEMA_V5_CONFIG_INTERNAL_H */
