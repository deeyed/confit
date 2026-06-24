#ifndef CONFIT_MODEL_H
#define CONFIT_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIT_CATEGORY_PATH_MAX_LENGTH 63U
#define CONFIT_CATEGORY_PATH_RECOMMENDED_DEPTH 2U
#define CONFIT_CATEGORY_PATH_MAX_DEPTH 3U

/**
 * @brief 표시용 TUI category path 분석 결과다.
 */
typedef struct ConfitCategoryPathInfo {
  /** path byte length. */
  size_t length;
  /** slash-separated segment 개수. */
  size_t depth;
} ConfitCategoryPathInfo;

/**
 * @brief Confit option schema type이다.
 *
 * bool, int, uint, hex, string, enum, float, path를 schema surface와
 * default value payload로 표현한다. Hex는 unsigned integer payload를
 * 사용하되 display convention이 hex인 option type으로 구분한다.
 */
typedef enum ConfitOptionType {
  /** 아직 type이 정해지지 않았다. */
  CONFIT_OPTION_TYPE_INVALID = 0,
  /** true 또는 false 값. */
  CONFIT_OPTION_TYPE_BOOL = 1,
  /** signed integer 값. */
  CONFIT_OPTION_TYPE_INT = 2,
  /** unsigned integer 값. */
  CONFIT_OPTION_TYPE_UINT = 3,
  /** unsigned integer를 hex display convention으로 표시하는 값. */
  CONFIT_OPTION_TYPE_HEX = 4,
  /** 사람이 읽는 짧은 문자열 값. */
  CONFIT_OPTION_TYPE_STRING = 5,
  /** choice 후보 중 하나를 선택하는 문자열 값. */
  CONFIT_OPTION_TYPE_ENUM = 6,
  /** finite floating-point 값. */
  CONFIT_OPTION_TYPE_FLOAT = 7,
  /** source-relative 또는 build-relative path 값. */
  CONFIT_OPTION_TYPE_PATH = 8,
} ConfitOptionType;

/**
 * @brief Confit value payload kind이다.
 */
typedef enum ConfitValueKind {
  /** 값이 비어 있다. */
  CONFIT_VALUE_EMPTY = 0,
  /** bool payload를 가진다. */
  CONFIT_VALUE_BOOL = 1,
  /** signed integer payload를 가진다. */
  CONFIT_VALUE_INT = 2,
  /** unsigned integer payload를 가진다. */
  CONFIT_VALUE_UINT = 3,
  /** string payload를 가진다. */
  CONFIT_VALUE_STRING = 4,
  /** enum string payload를 가진다. */
  CONFIT_VALUE_ENUM = 5,
  /** finite floating-point payload를 가진다. */
  CONFIT_VALUE_FLOAT = 6,
  /** path string payload를 가진다. */
  CONFIT_VALUE_PATH = 7,
} ConfitValueKind;

/**
 * @brief option 간 dependency edge 종류다.
 */
typedef enum ConfitDependencyKind {
  /** hard dependency. */
  CONFIT_DEPENDENCY_REQUIRES = 1,
  /** 동시에 만족할 수 없는 조합. */
  CONFIT_DEPENDENCY_CONFLICTS = 2,
  /** soft recommendation. */
  CONFIT_DEPENDENCY_RECOMMENDS = 3,
  /** 제한된 reverse dependency. */
  CONFIT_DEPENDENCY_FORCES = 4,
  /** TUI/display visibility 조건. */
  CONFIT_DEPENDENCY_VISIBLE_IF = 5,
} ConfitDependencyKind;

/**
 * @brief option schema에 기록된 dependency reference다.
 *
 * `option_id` 문자열은 reference가 소유한다. Graph builder가 project 전체를
 * 본 뒤 unknown reference와 self-edge를 검증한다.
 */
typedef struct ConfitDependencyRef {
  /** dependency edge kind. */
  ConfitDependencyKind kind;
  /** 대상 option id. */
  char *option_id;
} ConfitDependencyRef;

/**
 * @brief Confit resolved 또는 default value를 담는 tagged union이다.
 *
 * 문자열 payload는 이 구조체가 소유한다. `confit_value_clear`는 소유 문자열을
 * 해제하고 kind를 `CONFIT_VALUE_EMPTY`로 되돌린다.
 */
typedef struct ConfitValue {
  /** 현재 payload kind. */
  ConfitValueKind kind;
  /** payload storage. */
  union {
    /** bool payload. 0은 false, 0이 아니면 true. */
    int bool_value;
    /** signed integer payload. */
    int64_t int_value;
    /** unsigned integer payload. */
    uint64_t uint_value;
    /** finite floating-point payload. */
    double float_value;
    /** string 또는 enum payload. */
    char *string_value;
  } as;
} ConfitValue;

/**
 * @brief 특정 option id에 연결된 value entry다.
 *
 * `option_id`와 `source` 문자열은 entry가 소유한다. `source`는 profile path나
 * target path 같은 provenance를 담기 위한 optional field다.
 */
typedef struct ConfitNamedValue {
  /** option id. */
  char *option_id;
  /** option value. */
  ConfitValue value;
  /** value provenance. 없으면 `NULL`. */
  char *source;
} ConfitNamedValue;

/**
 * @brief 선택지 후보를 가진 choice model이다.
 *
 * `id`, `default_option`, `options`의 문자열은 choice가 소유한다.
 */
typedef struct ConfitChoice {
  /** choice id. */
  char *id;
  /** candidate string 목록. */
  char **options;
  /** candidate 개수. */
  size_t option_count;
  /** default candidate. 없으면 `NULL`. */
  char *default_option;
} ConfitChoice;

/**
 * @brief 하나의 Confit option schema record다.
 */
typedef struct ConfitOption {
  /** global option id. */
  char *id;
  /** option type. */
  ConfitOptionType type;
  /** default value. */
  ConfitValue default_value;
  /** range metadata가 있으면 1. */
  int has_range;
  /** range minimum value. */
  ConfitValue range_min;
  /** range maximum value. */
  ConfitValue range_max;
  /** enum candidate 목록. */
  char **enum_values;
  /** enum candidate 개수. */
  size_t enum_value_count;
  /** TUI/display prompt. 없으면 `NULL`. */
  char *prompt;
  /** TUI/display category path. 없으면 `NULL`. */
  char *category;
  /** help text. 없으면 `NULL`. */
  char *help;
  /** tag 문자열 목록. */
  char **tags;
  /** tag 개수. */
  size_t tag_count;
  /** deprecated alias 목록. */
  char **deprecated_aliases;
  /** deprecated alias 개수. */
  size_t deprecated_alias_count;
  /** owner metadata. 없으면 `NULL`. */
  char *owner;
  /** first-supported version metadata. 없으면 `NULL`. */
  char *since;
  /** stability metadata. 없으면 `NULL`. */
  char *stability;
  /** deprecated option이면 1. */
  int deprecated;
  /** replacement option id. 없으면 `NULL`. */
  char *replaced_by;
  /** dependency reference 목록. */
  ConfitDependencyRef *dependencies;
  /** dependency reference 개수. */
  size_t dependency_count;
} ConfitOption;

/**
 * @brief profile override 묶음이다.
 *
 * `name`, `base`, `target`, `values`의 문자열과 value payload는 profile이
 * 소유한다.
 */
typedef struct ConfitProfile {
  /** profile name. */
  char *name;
  /** base profile name. 없으면 `NULL`. */
  char *base;
  /** 선택 target name. 없으면 `NULL`. */
  char *target;
  /** override value 목록. */
  ConfitNamedValue *values;
  /** override value 개수. */
  size_t value_count;
} ConfitProfile;

/**
 * @brief target override와 target metadata를 담는다.
 *
 * Target file은 support claim이 아니며, `claim_level`로 probe 성격을 기록할 수
 * 있다.
 */
typedef struct ConfitTarget {
  /** target name. */
  char *name;
  /** architecture label. 없으면 `NULL`. */
  char *arch;
  /** board label. 없으면 `NULL`. */
  char *board;
  /** support/probe claim level. 없으면 `NULL`. */
  char *claim_level;
  /** target override value 목록. */
  ConfitNamedValue *values;
  /** target override value 개수. */
  size_t value_count;
} ConfitTarget;

/**
 * @brief 하나의 Confit project model이다.
 *
 * Project는 options, choices, profiles, targets 배열과 그 내부 문자열/value를
 * 모두 소유한다. `confit_project_free`는 전체 tree를 재귀적으로 해제한다.
 */
typedef struct ConfitProject {
  /** project name. */
  char *name;
  /** project version string. 없으면 `NULL`. */
  char *version;
  /** schema_version 값. */
  unsigned schema_version;
  /** option 목록. */
  ConfitOption *options;
  /** option 개수. */
  size_t option_count;
  /** choice 목록. */
  ConfitChoice *choices;
  /** choice 개수. */
  size_t choice_count;
  /** profile 목록. */
  ConfitProfile *profiles;
  /** profile 개수. */
  size_t profile_count;
  /** target 목록. */
  ConfitTarget *targets;
  /** target 개수. */
  size_t target_count;
} ConfitProject;

/**
 * @brief option type 이름을 반환한다.
 *
 * @param type 조회할 option type.
 * @return 안정적인 ASCII 문자열.
 */
const char *confit_option_type_name(ConfitOptionType type);

/**
 * @brief value kind 이름을 반환한다.
 *
 * @param kind 조회할 value kind.
 * @return 안정적인 ASCII 문자열.
 */
const char *confit_value_kind_name(ConfitValueKind kind);

/**
 * @brief dependency kind 이름을 반환한다.
 *
 * @param kind 조회할 dependency kind.
 * @return 안정적인 ASCII 문자열.
 */
const char *confit_dependency_kind_name(ConfitDependencyKind kind);

/**
 * @brief TUI category path를 검증하고 depth 정보를 계산한다.
 *
 * Category path는 slash-separated 표시용 path다. 빈 path, leading slash,
 * trailing slash, empty segment, 너무 긴 path는 schema 오류다.
 *
 * @param path 검증할 category path.
 * @param out_info optional 분석 결과.
 * @return 유효하면 CONFIT_OK, 아니면 오류 status.
 */
ConfitStatus confit_category_path_analyze(
    const char *path, ConfitCategoryPathInfo *out_info);

/**
 * @brief category path의 segment 하나를 allocation 없이 가리킨다.
 *
 * @param path 유효한 category path.
 * @param index 0-based segment index.
 * @param out_begin segment 시작 포인터.
 * @param out_size segment byte length.
 * @return segment를 찾으면 CONFIT_OK.
 */
ConfitStatus confit_category_path_segment_at(const char *path, size_t index,
                                             const char **out_begin,
                                             size_t *out_size);

/**
 * @brief value를 empty 상태로 초기화한다.
 *
 * @param value 초기화할 value.
 */
void confit_value_init(ConfitValue *value);

/**
 * @brief value가 소유한 payload를 해제하고 empty 상태로 되돌린다.
 *
 * @param value 정리할 value.
 */
void confit_value_clear(ConfitValue *value);

/**
 * @brief value에 bool payload를 설정한다.
 *
 * @param value 갱신할 value.
 * @param bool_value 0이면 false, 0이 아니면 true.
 */
void confit_value_set_bool(ConfitValue *value, int bool_value);

/**
 * @brief value에 signed integer payload를 설정한다.
 *
 * @param value 갱신할 value.
 * @param int_value 저장할 signed integer.
 */
void confit_value_set_int(ConfitValue *value, int64_t int_value);

/**
 * @brief value에 unsigned integer payload를 설정한다.
 *
 * @param value 갱신할 value.
 * @param uint_value 저장할 unsigned integer.
 */
void confit_value_set_uint(ConfitValue *value, uint64_t uint_value);

/**
 * @brief value에 string payload를 복사해 설정한다.
 *
 * @param value 갱신할 value.
 * @param string_value 복사할 문자열.
 * @return 성공하면 CONFIT_OK, allocation 실패면 CONFIT_ERR_INTERNAL.
 */
ConfitStatus confit_value_set_string(ConfitValue *value,
                                     const char *string_value);

/**
 * @brief value에 enum string payload를 복사해 설정한다.
 *
 * @param value 갱신할 value.
 * @param enum_value 복사할 enum candidate 문자열.
 * @return 성공하면 CONFIT_OK, allocation 실패면 CONFIT_ERR_INTERNAL.
 */
ConfitStatus confit_value_set_enum(ConfitValue *value,
                                   const char *enum_value);

/**
 * @brief value에 finite floating-point payload를 설정한다.
 *
 * @param value 갱신할 value.
 * @param float_value 저장할 floating-point 값.
 */
void confit_value_set_float(ConfitValue *value, double float_value);

/**
 * @brief value에 path string payload를 복사해 설정한다.
 *
 * @param value 갱신할 value.
 * @param path_value 복사할 path 문자열.
 * @return 성공하면 CONFIT_OK, allocation 실패면 CONFIT_ERR_INTERNAL.
 */
ConfitStatus confit_value_set_path(ConfitValue *value,
                                   const char *path_value);

/**
 * @brief value payload를 deep-copy한다.
 *
 * @param out 복사 결과를 받을 value.
 * @param input 복사할 value.
 * @return 성공하면 CONFIT_OK, allocation 실패면 CONFIT_ERR_INTERNAL.
 */
ConfitStatus confit_value_copy(ConfitValue *out, const ConfitValue *input);

/**
 * @brief project model을 생성한다.
 *
 * 반환된 project는 caller가 소유하며 `confit_project_free`로 해제한다.
 *
 * @return 새 project pointer. allocation 실패 시 `NULL`.
 */
ConfitProject *confit_project_create(void);

/**
 * @brief project model과 그 하위 ownership tree를 해제한다.
 *
 * @param project 해제할 project. `NULL`은 허용한다.
 */
void confit_project_free(ConfitProject *project);

/**
 * @brief project identity를 설정한다.
 *
 * 기존 name/version은 해제되고 새 문자열이 복사된다.
 *
 * @param project 갱신할 project.
 * @param name project name.
 * @param version project version. 없으면 `NULL`.
 * @param schema_version schema version.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_project_set_identity(ConfitProject *project,
                                         const char *name,
                                         const char *version,
                                         unsigned schema_version);

/**
 * @brief project에 option record를 추가한다.
 *
 * 반환된 pointer는 project 내부 배열을 가리키며, 이후 add 호출로 invalidation될
 * 수 있다.
 *
 * @param project 갱신할 project.
 * @param out_option 성공 시 추가된 option pointer를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_project_add_option(ConfitProject *project,
                                       ConfitOption **out_option);

/**
 * @brief project에 choice record를 추가한다.
 *
 * @param project 갱신할 project.
 * @param out_choice 성공 시 추가된 choice pointer를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_project_add_choice(ConfitProject *project,
                                       ConfitChoice **out_choice);

/**
 * @brief project에 profile record를 추가한다.
 *
 * @param project 갱신할 project.
 * @param out_profile 성공 시 추가된 profile pointer를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_project_add_profile(ConfitProject *project,
                                        ConfitProfile **out_profile);

/**
 * @brief project에 target record를 추가한다.
 *
 * @param project 갱신할 project.
 * @param out_target 성공 시 추가된 target pointer를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_project_add_target(ConfitProject *project,
                                       ConfitTarget **out_target);

/**
 * @brief option id로 option을 찾는다.
 *
 * @param project 조회할 project.
 * @param id 찾을 option id.
 * @return 찾으면 option pointer, 없으면 `NULL`.
 */
ConfitOption *confit_project_find_option(ConfitProject *project,
                                         const char *id);

/**
 * @brief option의 id와 type을 설정한다.
 *
 * @param option 갱신할 option.
 * @param id 복사할 option id.
 * @param type option type.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_set_identity(ConfitOption *option, const char *id,
                                        ConfitOptionType type);

/**
 * @brief option의 prompt/category/help metadata를 설정한다.
 *
 * 각 문자열은 optional이며 deep-copy된다.
 *
 * @param option 갱신할 option.
 * @param prompt prompt 문자열. 없으면 `NULL`.
 * @param category category 문자열. 없으면 `NULL`.
 * @param help help 문자열. 없으면 `NULL`.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_set_metadata(ConfitOption *option,
                                        const char *prompt,
                                        const char *category,
                                        const char *help);

/**
 * @brief option에 tag를 추가한다.
 *
 * @param option 갱신할 option.
 * @param tag 복사할 tag 문자열.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_add_tag(ConfitOption *option, const char *tag);

/**
 * @brief option에 deprecated alias를 추가한다.
 *
 * Alias는 profile/target value가 옛 id를 사용할 때 canonical option id로
 * migration하기 위한 compatibility surface다.
 *
 * @param option 갱신할 option.
 * @param alias 복사할 deprecated alias id.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_add_deprecated_alias(ConfitOption *option,
                                                const char *alias);

/**
 * @brief option stability metadata를 설정한다.
 *
 * 각 문자열은 optional이며 deep-copy된다.
 *
 * @param option 갱신할 option.
 * @param owner owner/team metadata. 없으면 `NULL`.
 * @param since first-supported version metadata. 없으면 `NULL`.
 * @param stability stability class. 없으면 `NULL`.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_set_stability_metadata(ConfitOption *option,
                                                  const char *owner,
                                                  const char *since,
                                                  const char *stability);

/**
 * @brief option deprecation metadata를 설정한다.
 *
 * @param option 갱신할 option.
 * @param deprecated deprecated option이면 1.
 * @param replaced_by replacement option id. 없으면 `NULL`.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_set_deprecation(ConfitOption *option,
                                           int deprecated,
                                           const char *replaced_by);

/**
 * @brief option에 dependency reference를 추가한다.
 *
 * @param option 갱신할 option.
 * @param kind dependency edge kind.
 * @param option_id 대상 option id.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_add_dependency(ConfitOption *option,
                                          ConfitDependencyKind kind,
                                          const char *option_id);

/**
 * @brief option default value를 deep-copy한다.
 *
 * @param option 갱신할 option.
 * @param value 복사할 default value.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_set_default(ConfitOption *option,
                                       const ConfitValue *value);

/**
 * @brief option range metadata를 deep-copy한다.
 *
 * Range validation은 option type과 value kind가 맞아야 한다. int는 int range,
 * uint/hex는 uint range, float는 float range를 사용한다.
 *
 * @param option 갱신할 option.
 * @param min_value 복사할 minimum value.
 * @param max_value 복사할 maximum value.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_set_range(ConfitOption *option,
                                     const ConfitValue *min_value,
                                     const ConfitValue *max_value);

/**
 * @brief option range metadata를 제거한다.
 *
 * @param option 갱신할 option.
 */
void confit_option_clear_range(ConfitOption *option);

/**
 * @brief enum option candidate를 추가한다.
 *
 * @param option 갱신할 option.
 * @param enum_value 복사할 enum candidate.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_option_add_enum_value(ConfitOption *option,
                                          const char *enum_value);

/**
 * @brief option default value가 type/range/enum candidate 제약을 만족하는지 확인한다.
 *
 * @param option 검사할 option.
 * @return 유효하면 CONFIT_OK, 아니면 CONFIT_ERR_SCHEMA.
 */
ConfitStatus confit_option_validate_default(const ConfitOption *option);

/**
 * @brief choice identity를 설정한다.
 *
 * @param choice 갱신할 choice.
 * @param id 복사할 choice id.
 * @param default_option 복사할 default candidate. 없으면 `NULL`.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_choice_set_identity(ConfitChoice *choice, const char *id,
                                        const char *default_option);

/**
 * @brief choice candidate를 추가한다.
 *
 * @param choice 갱신할 choice.
 * @param option 복사할 candidate 문자열.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_choice_add_option(ConfitChoice *choice,
                                      const char *option);

/**
 * @brief profile identity를 설정한다.
 *
 * @param profile 갱신할 profile.
 * @param name 복사할 profile name.
 * @param base 복사할 base profile name. 없으면 `NULL`.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_profile_set_identity(ConfitProfile *profile,
                                         const char *name, const char *base);

/**
 * @brief profile이 선택하는 target name을 설정한다.
 *
 * @param profile 갱신할 profile.
 * @param target 복사할 target name. 없으면 `NULL`.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_profile_set_target(ConfitProfile *profile,
                                       const char *target);

/**
 * @brief profile override value를 추가한다.
 *
 * @param profile 갱신할 profile.
 * @param option_id 복사할 option id.
 * @param value 복사할 value.
 * @param source optional provenance.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_profile_add_value(ConfitProfile *profile,
                                      const char *option_id,
                                      const ConfitValue *value,
                                      const char *source);

/**
 * @brief target identity와 metadata를 설정한다.
 *
 * @param target 갱신할 target.
 * @param name 복사할 target name.
 * @param arch optional architecture label.
 * @param board optional board label.
 * @param claim_level optional support/probe claim level.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_target_set_identity(ConfitTarget *target, const char *name,
                                        const char *arch, const char *board,
                                        const char *claim_level);

/**
 * @brief target override value를 추가한다.
 *
 * @param target 갱신할 target.
 * @param option_id 복사할 option id.
 * @param value 복사할 value.
 * @param source optional provenance.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_target_add_value(ConfitTarget *target,
                                     const char *option_id,
                                     const ConfitValue *value,
                                     const char *source);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_MODEL_H */
