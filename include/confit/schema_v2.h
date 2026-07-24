#ifndef CONFIT_SCHEMA_V2_H
#define CONFIT_SCHEMA_V2_H

#include <stddef.h>
#include <stdint.h>

#include "confit/project.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief v2 source 위치다. `path`는 이 structure가 소유한다. */
typedef struct ConfitV2SourceSpan {
  /** source file path 또는 test source name. */
  char *path;
  /** 1-based source line. 위치를 알 수 없으면 0. */
  size_t line;
  /** 1-based source column. 위치를 알 수 없으면 0. */
  size_t column;
  /** expression text 안의 0-based byte offset. field 전체면 0. */
  size_t local_offset;
} ConfitV2SourceSpan;

/** @brief v2 option의 declared value type이다. */
typedef enum ConfitV2OptionType {
  CONFIT_V2_OPTION_TYPE_INVALID = 0,
  CONFIT_V2_OPTION_TYPE_BOOL,
  CONFIT_V2_OPTION_TYPE_TRISTATE,
  CONFIT_V2_OPTION_TYPE_INT,
  CONFIT_V2_OPTION_TYPE_UINT,
  CONFIT_V2_OPTION_TYPE_HEX,
  CONFIT_V2_OPTION_TYPE_FLOAT,
  CONFIT_V2_OPTION_TYPE_STRING,
  CONFIT_V2_OPTION_TYPE_ENUM,
  CONFIT_V2_OPTION_TYPE_PATH,
  CONFIT_V2_OPTION_TYPE_STRING_LIST,
  CONFIT_V2_OPTION_TYPE_PATH_LIST,
  CONFIT_V2_OPTION_TYPE_ENUM_SET,
} ConfitV2OptionType;

/** @brief typed value payload kind이다. */
typedef enum ConfitV2ValueKind {
  CONFIT_V2_VALUE_UNSET = 0,
  CONFIT_V2_VALUE_BOOL,
  CONFIT_V2_VALUE_TRISTATE,
  CONFIT_V2_VALUE_INT,
  CONFIT_V2_VALUE_UINT,
  CONFIT_V2_VALUE_FLOAT,
  CONFIT_V2_VALUE_STRING,
  CONFIT_V2_VALUE_STRING_LIST,
} ConfitV2ValueKind;

/** @brief source order를 보존하는 owned string list다. */
typedef struct ConfitV2StringList {
  /** caller/project가 소유하는 NUL-terminated UTF-8 strings. */
  char **items;
  /** `items` 원소 개수. */
  size_t count;
} ConfitV2StringList;

/** @brief typed v2 value다. string/list payload는 value가 소유한다. */
typedef struct ConfitV2Value {
  /** payload kind. UNSET은 값이 없음을 뜻한다. */
  ConfitV2ValueKind kind;
  union {
    int bool_value;
    char tristate_value;
    int64_t int_value;
    uint64_t uint_value;
    double float_value;
    char *string_value;
    ConfitV2StringList string_list;
  } as;
} ConfitV2Value;

/** @brief option type별 TOML value shape를 고정하는 descriptor다. */
typedef struct ConfitV2TypeDescriptor {
  /** declared option type. */
  ConfitV2OptionType option_type;
  /** default/assignment에 사용할 typed payload kind. */
  ConfitV2ValueKind value_kind;
  /** list 또는 set collection이면 1. */
  int is_collection;
  /** enum candidate declaration이 필요하면 1. */
  int requires_values;
} ConfitV2TypeDescriptor;

/** @brief schema/profile/target/computed writer domain이다. */
typedef enum ConfitV2WriteDomain {
  CONFIT_V2_WRITE_DOMAIN_INVALID = 0,
  CONFIT_V2_WRITE_DOMAIN_SCHEMA,
  CONFIT_V2_WRITE_DOMAIN_PROFILE,
  CONFIT_V2_WRITE_DOMAIN_TARGET,
  CONFIT_V2_WRITE_DOMAIN_COMPUTED,
} ConfitV2WriteDomain;

/** @brief stability metadata다. */
typedef enum ConfitV2Stability {
  CONFIT_V2_STABILITY_INVALID = 0,
  CONFIT_V2_STABILITY_EXPERIMENTAL,
  CONFIT_V2_STABILITY_STABLE,
  CONFIT_V2_STABILITY_DEPRECATED,
  CONFIT_V2_STABILITY_INTERNAL,
} ConfitV2Stability;

/** @brief build output emit surface bit mask다. */
typedef enum ConfitV2EmitSurface {
  CONFIT_V2_EMIT_NONE = 0,
  CONFIT_V2_EMIT_HEADER = 1U << 0,
  CONFIT_V2_EMIT_CMAKE = 1U << 1,
  CONFIT_V2_EMIT_QSTAR = 1U << 2,
  CONFIT_V2_EMIT_SELECTION = 1U << 3,
} ConfitV2EmitSurface;

/** @brief source span을 가진 expression source text다. 아직 AST가 아니다. */
typedef struct ConfitV2ExpressionText {
  /** NUL-terminated expression source. 없으면 NULL. */
  char *text;
  /** `text`가 선언된 source position. */
  ConfitV2SourceSpan span;
} ConfitV2ExpressionText;

/** @brief typed assignment와 그 source다. */
typedef struct ConfitV2Assignment {
  /** assignment가 존재하면 1. */
  int is_set;
  /** option type과 이미 일치 검증된 value. */
  ConfitV2Value value;
  /** assignment TOML value의 source position. */
  ConfitV2SourceSpan span;
} ConfitV2Assignment;

/** @brief numeric range의 typed min/max pair다. */
typedef struct ConfitV2NumericRange {
  /** range가 선언되면 1. */
  int is_set;
  /** option type과 이미 일치 검증된 lower bound. */
  ConfitV2Value min_value;
  /** option type과 이미 일치 검증된 upper bound. */
  ConfitV2Value max_value;
  /** range table declaration source position. */
  ConfitV2SourceSpan span;
} ConfitV2NumericRange;

/** @brief 아직 parse하지 않은 conditional default source다. */
typedef struct ConfitV2ConditionalDefault {
  ConfitV2ExpressionText when;
  ConfitV2Assignment assignment;
  int32_t priority;
  ConfitV2SourceSpan span;
} ConfitV2ConditionalDefault;

/** @brief suggestion source다. suggestion은 configuration validity를 바꾸지 않는다. */
typedef struct ConfitV2Suggestion {
  ConfitV2ExpressionText when;
  ConfitV2Assignment assignment;
  char *message;
  ConfitV2SourceSpan span;
} ConfitV2Suggestion;

/** @brief exactly/zero/one-or-more choice cardinality다. */
typedef enum ConfitV2ChoiceCardinality {
  CONFIT_V2_CHOICE_CARDINALITY_INVALID = 0,
  CONFIT_V2_CHOICE_CARDINALITY_EXACTLY_ONE,
  CONFIT_V2_CHOICE_CARDINALITY_ZERO_OR_ONE,
  CONFIT_V2_CHOICE_CARDINALITY_ONE_OR_MORE,
} ConfitV2ChoiceCardinality;

/** @brief choice conditional default source다. */
typedef struct ConfitV2ChoiceDefault {
  ConfitV2ExpressionText when;
  char *member;
  int32_t priority;
  ConfitV2SourceSpan span;
} ConfitV2ChoiceDefault;

/** @brief one semantic option definition이다. 모든 하위 allocation은 symbol이 소유한다. */
typedef struct ConfitV2Symbol {
  char *id;
  ConfitV2OptionType type;
  ConfitV2WriteDomain write_domain;
  ConfitV2Stability stability;
  int required;
  int user_override;
  ConfitV2Assignment default_value;
  ConfitV2NumericRange range;
  ConfitV2StringList values;
  char *prompt;
  char *help;
  char *menu;
  ConfitV2StringList tags;
  char *owner;
  char *since;
  unsigned int emit_mask;
  ConfitV2ExpressionText computed;
  ConfitV2ExpressionText available_if;
  ConfitV2ExpressionText visible_if;
  ConfitV2ConditionalDefault *defaults;
  size_t default_count;
  ConfitV2Suggestion *suggestions;
  size_t suggestion_count;
  ConfitV2SourceSpan span;
} ConfitV2Symbol;

/** @brief menu 안의 read-only option 표시 source다. */
typedef struct ConfitV2MenuReference {
  /** canonical option id다. */
  char *option_id;
  /** v2에서는 duplicate menu display가 항상 read-only여야 한다. */
  int read_only;
  /** reference inline table의 source position이다. */
  ConfitV2SourceSpan span;
} ConfitV2MenuReference;

/** @brief explicit menu declaration이다. menu는 UI hierarchy만 나타낸다. */
typedef struct ConfitV2MenuNode {
  char *id;
  char *prompt;
  char *parent;
  int32_t order;
  ConfitV2ExpressionText visible_if;
  /** primary `option.menu` placement 외의 read-only display references다. */
  ConfitV2MenuReference *references;
  size_t reference_count;
  ConfitV2SourceSpan span;
} ConfitV2MenuNode;

/** @brief explicit choice declaration이다. member reference link는 이후 단계가 소유한다. */
typedef struct ConfitV2Choice {
  char *id;
  ConfitV2OptionType member_type;
  ConfitV2StringList members;
  ConfitV2ChoiceCardinality cardinality;
  ConfitV2ExpressionText available_if;
  ConfitV2ExpressionText visible_if;
  ConfitV2ChoiceDefault *defaults;
  size_t default_count;
  ConfitV2SourceSpan span;
} ConfitV2Choice;

/** @brief named constraint source다. expression parse/typecheck는 이후 단계가 소유한다. */
typedef struct ConfitV2Constraint {
  char *id;
  ConfitV2ExpressionText when;
  ConfitV2ExpressionText require;
  char *message;
  ConfitV2SourceSpan span;
} ConfitV2Constraint;

/** @brief import traversal state다. completed import만 linked project 입력이 된다. */
typedef enum ConfitV2ImportState {
  CONFIT_V2_IMPORT_STATE_DECLARED = 0,
  CONFIT_V2_IMPORT_STATE_VISITING,
  CONFIT_V2_IMPORT_STATE_COMPLETE,
} ConfitV2ImportState;

/** @brief explicit project import의 logical/canonical path와 source span이다. */
typedef struct ConfitV2Import {
  /** config root 기준 normalized forward-slash logical path. */
  char *path;
  /** symlink까지 해석한 canonical host path. */
  char *canonical_path;
  ConfitV2SourceSpan span;
  ConfitV2ImportState state;
} ConfitV2Import;

/** @brief allocation ownership을 test/embedding caller가 제어하는 allocator다. */
typedef struct ConfitV2Allocator {
  void *context;
  void *(*allocate)(void *context, size_t size);
  void *(*reallocate)(void *context, void *allocation, size_t size);
  void (*deallocate)(void *context, void *allocation);
} ConfitV2Allocator;

/**
 * @brief parsed but unresolved v2 project source model이다.
 *
 * 모든 public field와 그 하위 allocation은 project가 소유한다. caller는 model을
 * 변경하지 않으며 `confit_v2_project_free()`만 사용해 해제한다.
 */
typedef struct ConfitV2Project {
  ConfitV2Allocator allocator;
  char *config_root;
  char *name;
  char *namespace_name;
  char *version;
  ConfitV2Import *imports;
  size_t import_count;
  ConfitV2StringList profile_dirs;
  ConfitV2StringList target_dirs;
  ConfitV2StringList selection_dirs;
  ConfitV2Symbol *symbols;
  size_t symbol_count;
  ConfitV2MenuNode *menus;
  size_t menu_count;
  ConfitV2Choice *choices;
  size_t choice_count;
  ConfitV2Constraint *constraints;
  size_t constraint_count;
  ConfitV2SourceSpan span;
} ConfitV2Project;

/** @brief declared type의 immutable descriptor를 반환한다. */
const ConfitV2TypeDescriptor *
confit_v2_type_descriptor(ConfitV2OptionType option_type);

/** @brief v2 project를 기본 allocator로 load한다. */
ConfitStatus confit_v2_schema_load_project(const char *project_root,
                                            ConfitV2Project **out_project,
                                            ConfitDiagnostic *diagnostic);

/**
 * @brief v2 project를 caller allocator로 load한다.
 *
 * Allocation failure를 포함한 모든 partial failure에서 `*out_project`는 NULL이고
 * allocator로 확보한 typed-model allocation은 모두 해제된다. allocator callback
 * 세 개는 모두 non-NULL이어야 한다.
 */
ConfitStatus confit_v2_schema_load_project_with_allocator(
    const char *project_root, const ConfitV2Allocator *allocator,
    ConfitV2Project **out_project, ConfitDiagnostic *diagnostic);

/** @brief v2 project owner tree를 해제한다. NULL은 허용한다. */
void confit_v2_project_free(ConfitV2Project *project);

/**
 * @brief v2 schema loader를 opaque project handle adapter로 호출한다.
 *
 * Project source가 schema_version 2가 아니면 hard error다. 성공 handle은 raw
 * `ConfitV2Project`를 generic public path에 노출하지 않는다.
 */
ConfitStatus confit_schema_v2_load_project_handle(
    const char *project_root, ConfitProjectHandle **out_project,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SCHEMA_V2_H */
