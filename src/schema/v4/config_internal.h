#ifndef CONFIT_SCHEMA_V4_CONFIG_INTERNAL_H
#define CONFIT_SCHEMA_V4_CONFIG_INTERNAL_H

#include "confit/config_v4.h"

#define CONFIT_V4_MAX_PATH_BYTES 4095U
#define CONFIT_V4_MAX_TEXT_BYTES 512U
#define CONFIT_V4_MAX_SYMBOL_BYTES 127U
#define CONFIT_V4_MAX_OPTIONS 512U
#define CONFIT_V4_MAX_MENUS 256U
#define CONFIT_V4_MAX_CHOICES 256U
#define CONFIT_V4_MAX_RULES 512U
#define CONFIT_V4_MAX_PROVIDERS 512U
#define CONFIT_V4_MAX_DOCUMENTS 1024U
#define CONFIT_V4_MAX_DISCOVERY_ENTRIES 32768U
#define CONFIT_V4_MAX_DISCOVERY_DEPTH 32U
#define CONFIT_V4_MAX_FILE_BYTES (128U * 1024U)
#define CONFIT_V4_MAX_TOTAL_BYTES (16U * 1024U * 1024U)
#define CONFIT_V4_MAX_TAGS 32U
#define CONFIT_V4_MAX_VALUES 128U
#define CONFIT_V4_MAX_TOTAL_EDGES 4096U
#define CONFIT_V4_MAX_GRAPH_DEPTH 32U

typedef struct ConfitV4OwnedSpan {
  char *path;
  size_t line;
  size_t column;
} ConfitV4OwnedSpan;

typedef struct ConfitV4StringList {
  char **items;
  ConfitV4OwnedSpan *spans;
  size_t count;
} ConfitV4StringList;

typedef struct ConfitV4Provider {
  char *namespace_name;
  uint32_t major;
  ConfitV4ProviderCardinality cardinality;
  ConfitV4ProviderAbsence absence;
  ConfitV4OwnedSpan source;
} ConfitV4Provider;

typedef struct ConfitV4Option {
  char *symbol;
  char *projection;
  ConfitV4OptionType type;
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
  ConfitV4StringList tags;
  ConfitV4StringList allowed;
  ConfitV4StringList values;
  ConfitV4StringList enabled_values;
  ConfitV4StringList prerequisites;
  ConfitV4StringList visible_all;
  ConfitV4Provider *providers;
  size_t provider_count;
  ConfitV4OwnedSpan declaration;
  ConfitV4OwnedSpan owner_source;
  ConfitV4OwnedSpan since_source;
  ConfitV4OwnedSpan stability_source;
  ConfitV4OwnedSpan tags_source;
  ConfitV4OwnedSpan menu_order_source;
  ConfitV4OwnedSpan default_source;
} ConfitV4Option;

typedef struct ConfitV4Menu {
  char *id;
  char *prompt;
  char *help;
  char *parent;
  int64_t order;
  ConfitV4OwnedSpan source;
} ConfitV4Menu;

typedef struct ConfitV4Choice {
  char *symbol;
  char *prompt;
  char *help;
  ConfitV4StringList members;
  ConfitV4ChoiceCardinality cardinality;
  ConfitV4OwnedSpan source;
} ConfitV4Choice;

typedef struct ConfitV4Rule {
  ConfitV4StringList if_all;
  ConfitV4StringList require_all;
  char *message;
  ConfitV4OwnedSpan source;
} ConfitV4Rule;

typedef struct ConfitV4Defaults {
  char *owner;
  char *since;
  char *stability;
  ConfitV4StringList tags;
  int has_menu_order;
  int64_t menu_order;
  ConfitV4OwnedSpan owner_source;
  ConfitV4OwnedSpan since_source;
  ConfitV4OwnedSpan stability_source;
  ConfitV4OwnedSpan tags_source;
  ConfitV4OwnedSpan menu_order_source;
} ConfitV4Defaults;

typedef enum ConfitV4Role {
  CONFIT_V4_ROLE_OPTIONS = 0,
  CONFIT_V4_ROLE_MENUS,
  CONFIT_V4_ROLE_CHOICES,
  CONFIT_V4_ROLE_CONSTRAINTS,
  CONFIT_V4_ROLE_PROFILES,
  CONFIT_V4_ROLE_TARGETS,
  CONFIT_V4_ROLE_SELECTIONS,
  CONFIT_V4_ROLE_PRODUCTS,
  CONFIT_V4_ROLE_COUNT,
} ConfitV4Role;

typedef struct ConfitV4RoleRoots {
  char **items;
  size_t count;
} ConfitV4RoleRoots;

struct ConfitV4Catalog {
  char *repository_root;
  char *project_path;
  char *project_name;
  char *project_namespace;
  ConfitV4RoleRoots roots[CONFIT_V4_ROLE_COUNT];
  ConfitV4Defaults defaults;
  ConfitV4Option *options;
  size_t option_count;
  ConfitV4Menu *menus;
  size_t menu_count;
  ConfitV4Choice *choices;
  size_t choice_count;
  ConfitV4Rule *rules;
  size_t rule_count;
  char **documents;
  size_t document_count;
  size_t discovery_entry_count;
  size_t total_edges;
  size_t total_bytes;
};

typedef struct ConfitV4EffectiveValue {
  char *symbol;
  char *value;
  int enabled;
  ConfitV4OwnedSpan source;
} ConfitV4EffectiveValue;

typedef struct ConfitV4Reason {
  ConfitV4ReasonKind kind;
  int satisfied;
  char *subject;
  char *cause;
  ConfitV4OwnedSpan source;
} ConfitV4Reason;

typedef struct ConfitV4ResolvedProvider {
  char *namespace_name;
  uint32_t major;
  char *option_symbol;
} ConfitV4ResolvedProvider;

struct ConfitV4Evaluation {
  ConfitV4EffectiveValue *values;
  size_t value_count;
  ConfitV4Reason *reasons;
  size_t reason_count;
  ConfitV4ResolvedProvider *providers;
  size_t provider_count;
};

char *confit_v4_copy(const char *text);
void confit_v4_owned_span_clear(ConfitV4OwnedSpan *span);
int confit_v4_owned_span_set(ConfitV4OwnedSpan *span, const char *path,
                            size_t line, size_t column);
void confit_v4_string_list_clear(ConfitV4StringList *list);
const ConfitV4Option *confit_v4_find_option(const ConfitV4Catalog *catalog,
                                            const char *symbol);
int confit_v4_symbol_valid(const char *text);
int confit_v4_namespace_valid(const char *text);
int confit_v4_option_value_valid(const ConfitV4Option *option,
                                 const char *value, int *out_enabled);
ConfitStatus confit_v4_evaluation_add_reason(
    ConfitV4Evaluation *evaluation, ConfitV4ReasonKind kind, int satisfied,
    const char *subject, const char *cause, const ConfitV4OwnedSpan *source);
void confit_v4_set_diagnostic(ConfitDiagnostic *diagnostic,
                              ConfitStatus status,
                              const ConfitV4OwnedSpan *source,
                              const char *message);

#endif /* CONFIT_SCHEMA_V4_CONFIG_INTERNAL_H */
