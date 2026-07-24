#ifndef CONFIT_PARSER_V2_H
#define CONFIT_PARSER_V2_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief V2 TOML adapter가 지원하는 read-only TOML value 종류다. */
typedef enum ConfitV2TomlValueType {
  CONFIT_V2_TOML_VALUE_UNKNOWN = 0,
  CONFIT_V2_TOML_VALUE_STRING,
  CONFIT_V2_TOML_VALUE_INT64,
  CONFIT_V2_TOML_VALUE_FLOAT64,
  CONFIT_V2_TOML_VALUE_BOOL,
  CONFIT_V2_TOML_VALUE_DATE,
  CONFIT_V2_TOML_VALUE_TIME,
  CONFIT_V2_TOML_VALUE_DATETIME,
  CONFIT_V2_TOML_VALUE_DATETIME_TZ,
  CONFIT_V2_TOML_VALUE_ARRAY,
  CONFIT_V2_TOML_VALUE_TABLE,
} ConfitV2TomlValueType;

/** @brief tomlc17 result와 source text를 소유하는 v2 TOML document다. */
typedef struct ConfitV2TomlDocument ConfitV2TomlDocument;

/** @brief v2 TOML document가 소유하는 immutable value handle이다. */
typedef struct ConfitV2TomlValue ConfitV2TomlValue;

/**
 * @brief host path에서 v2 TOML source를 읽고 strict syntax parse한다.
 *
 * 파일 I/O는 Confit host adapter가 담당한다. 성공한 document와 모든 value handle은
 * `confit_v2_toml_document_free()` 전까지 유효하다.
 *
 * @param path 읽을 TOML file path.
 * @param out_document 성공 시 caller가 소유할 document output.
 * @param diagnostic 실패 위치와 원인을 기록할 optional record.
 * @return 성공하면 CONFIT_OK, syntax 오류면 CONFIT_ERR_PARSE.
 */
ConfitStatus confit_v2_toml_parse_file(const char *path,
                                       ConfitV2TomlDocument **out_document,
                                       ConfitDiagnostic *diagnostic);

/**
 * @brief memory source를 v2 TOML document로 parse한다.
 *
 * adapter는 UTF-8을 검사하고 source text를 복사한다. `source_name`은 parsed value의
 * source span과 diagnostic path에만 사용하며, `NULL`을 허용한다.
 *
 * @param source_name source span에 기록할 이름. 없으면 NULL.
 * @param text NUL 종료가 아니어도 되는 TOML source byte.
 * @param text_size text의 byte 길이.
 * @param out_document 성공 시 caller가 소유할 document output.
 * @param diagnostic 실패 위치와 원인을 기록할 optional record.
 * @return 성공하면 CONFIT_OK, 실패하면 적절한 status.
 */
ConfitStatus confit_v2_toml_parse_text(const char *source_name,
                                       const char *text, size_t text_size,
                                       ConfitV2TomlDocument **out_document,
                                       ConfitDiagnostic *diagnostic);

/**
 * @brief v2 TOML document와 그 value handle을 해제한다.
 *
 * @param document 해제할 document. NULL은 허용한다.
 */
void confit_v2_toml_document_free(ConfitV2TomlDocument *document);

/** @brief document가 보존하는 원본 source text를 반환한다. */
const char *
confit_v2_toml_document_source_text(const ConfitV2TomlDocument *document);

/** @brief document 원본 source byte 길이를 반환한다. */
size_t confit_v2_toml_document_source_size(const ConfitV2TomlDocument *document);

/** @brief document root table value를 반환한다. */
const ConfitV2TomlValue *
confit_v2_toml_document_root(const ConfitV2TomlDocument *document);

/** @brief value의 TOML 종류를 반환한다. */
ConfitV2TomlValueType
confit_v2_toml_value_type(const ConfitV2TomlValue *value);

/** @brief value가 선언된 1-based source line을 반환한다. */
size_t confit_v2_toml_value_line(const ConfitV2TomlValue *value);

/** @brief value가 선언된 1-based source column을 반환한다. */
size_t confit_v2_toml_value_column(const ConfitV2TomlValue *value);

/** @brief value source 이름을 반환한다. 반환 문자열은 document가 소유한다. */
const char *confit_v2_toml_value_source(const ConfitV2TomlValue *value);

/**
 * @brief string value의 byte pointer와 byte 길이를 반환한다.
 *
 * @return value가 string이면 1, 아니면 0.
 */
int confit_v2_toml_value_string(const ConfitV2TomlValue *value,
                                const char **out_text, size_t *out_size);

/** @brief int64 value를 반환한다. value type이 다르면 0을 반환한다. */
int confit_v2_toml_value_int64(const ConfitV2TomlValue *value,
                                int64_t *out_value);

/** @brief float64 value를 반환한다. value type이 다르면 0을 반환한다. */
int confit_v2_toml_value_float64(const ConfitV2TomlValue *value,
                                  double *out_value);

/** @brief boolean value를 0 또는 1로 반환한다. type이 다르면 0을 반환한다. */
int confit_v2_toml_value_bool(const ConfitV2TomlValue *value, int *out_value);

/** @brief table entry 수를 반환한다. table이 아니면 0이다. */
size_t confit_v2_toml_table_size(const ConfitV2TomlValue *table);

/** @brief table entry key를 index 순서대로 반환한다. */
const char *confit_v2_toml_table_key_at(const ConfitV2TomlValue *table,
                                         size_t index);

/** @brief table entry value를 index 순서대로 반환한다. */
const ConfitV2TomlValue *
confit_v2_toml_table_value_at(const ConfitV2TomlValue *table, size_t index);

/**
 * @brief table에서 exact key를 직접 순회해 찾는다.
 *
 * multipart-key helper의 길이와 escape 제약을 피하기 위해 key path를 해석하지
 * 않는다. V2 loader는 각 table level을 명시적으로 순회해야 한다.
 */
const ConfitV2TomlValue *
confit_v2_toml_table_find(const ConfitV2TomlValue *table, const char *key);

/** @brief array element 수를 반환한다. array가 아니면 0이다. */
size_t confit_v2_toml_array_size(const ConfitV2TomlValue *array);

/** @brief array element를 index 순서대로 반환한다. */
const ConfitV2TomlValue *
confit_v2_toml_array_at(const ConfitV2TomlValue *array, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_PARSER_V2_H */
