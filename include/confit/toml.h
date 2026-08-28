#ifndef CONFIT_TOML_H
#define CONFIT_TOML_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief TOML adapter가 지원하는 read-only TOML value 종류다. */
typedef enum ConfitTomlValueType {
  CONFIT_TOML_VALUE_UNKNOWN = 0,
  CONFIT_TOML_VALUE_STRING,
  CONFIT_TOML_VALUE_INT64,
  CONFIT_TOML_VALUE_FLOAT64,
  CONFIT_TOML_VALUE_BOOL,
  CONFIT_TOML_VALUE_DATE,
  CONFIT_TOML_VALUE_TIME,
  CONFIT_TOML_VALUE_DATETIME,
  CONFIT_TOML_VALUE_DATETIME_TZ,
  CONFIT_TOML_VALUE_ARRAY,
  CONFIT_TOML_VALUE_TABLE,
} ConfitTomlValueType;

/** @brief Lexical base of one native TOML integer token. */
typedef enum ConfitTomlIntegerBase {
  CONFIT_TOML_INTEGER_BASE_UNKNOWN = 0,
  CONFIT_TOML_INTEGER_BASE_BINARY = 2,
  CONFIT_TOML_INTEGER_BASE_OCTAL = 8,
  CONFIT_TOML_INTEGER_BASE_DECIMAL = 10,
  CONFIT_TOML_INTEGER_BASE_HEXADECIMAL = 16,
} ConfitTomlIntegerBase;

/** @brief tomlc17 result와 source text를 소유하는 TOML document다. */
typedef struct ConfitTomlDocument ConfitTomlDocument;

/** @brief TOML document가 소유하는 immutable value handle이다. */
typedef struct ConfitTomlValue ConfitTomlValue;

/**
 * @brief memory source를 TOML document로 parse한다.
 *
 * adapter는 public one-file limit, embedded NUL, UTF-8과 TOML syntax를 검사하고
 * source text를 복사한다. `source_name`은 parsed value의 source span과 diagnostic
 * path에만 사용하며, `NULL`을 허용한다. Product file loading은
 * `confit_input_load_toml`을 사용해 one-image ownership을 유지한다.
 *
 * @param source_name source span에 기록할 이름. 없으면 NULL.
 * @param text NUL 종료가 아니어도 되는 TOML source byte.
 * @param text_size text의 byte 길이.
 * @param out_document 성공 시 caller가 소유할 document output.
 * @param diagnostic 실패 위치와 원인을 기록할 optional record.
 * @return 성공하면 CONFIT_OK, 문법 또는 UTF-8 오류면 CONFIT_ERR_VALIDATION.
 */
ConfitStatus confit_toml_parse_text(const char *source_name,
                                       const char *text, size_t text_size,
                                       ConfitTomlDocument **out_document,
                                       ConfitDiagnostic *diagnostic);

/**
 * @brief TOML document와 그 value handle을 해제한다.
 *
 * @param document 해제할 document. NULL은 허용한다.
 */
void confit_toml_document_free(ConfitTomlDocument *document);

/** @brief document가 보존하는 원본 source text를 반환한다. */
const char *
confit_toml_document_source_text(const ConfitTomlDocument *document);

/** @brief document 원본 source byte 길이를 반환한다. */
size_t confit_toml_document_source_size(const ConfitTomlDocument *document);

/** @brief document root table value를 반환한다. */
const ConfitTomlValue *
confit_toml_document_root(const ConfitTomlDocument *document);

/** @brief value의 TOML 종류를 반환한다. */
ConfitTomlValueType
confit_toml_value_type(const ConfitTomlValue *value);

/** @brief value가 선언된 1-based source line을 반환한다. */
size_t confit_toml_value_line(const ConfitTomlValue *value);

/** @brief value가 선언된 1-based source column을 반환한다. */
size_t confit_toml_value_column(const ConfitTomlValue *value);

/** @brief value source 이름을 반환한다. 반환 문자열은 document가 소유한다. */
const char *confit_toml_value_source(const ConfitTomlValue *value);

/**
 * @brief string value의 byte pointer와 byte 길이를 반환한다.
 *
 * @return value가 string이면 1, 아니면 0.
 */
int confit_toml_value_string(const ConfitTomlValue *value,
                                const char **out_text, size_t *out_size);

/** @brief int64 value를 반환한다. value type이 다르면 0을 반환한다. */
int confit_toml_value_int64(const ConfitTomlValue *value,
                                int64_t *out_value);

/**
 * @brief Recover an integer token's base from the document's owned byte image.
 *
 * tomlc17 intentionally normalizes all native TOML integer spellings to an
 * int64 value.  Schema types such as `hex` also need the lexical distinction.
 * This accessor uses only the already parsed document bytes and the value's
 * recorded source line/column; it does not reopen a path or reparse a file.
 *
 * @return nonzero for an integer value whose exact token start is available.
 */
int confit_toml_value_integer_base(const ConfitTomlDocument *document,
                                   const ConfitTomlValue *value,
                                   ConfitTomlIntegerBase *out_base);

/** @brief float64 value를 반환한다. value type이 다르면 0을 반환한다. */
int confit_toml_value_float64(const ConfitTomlValue *value,
                                  double *out_value);

/** @brief boolean value를 0 또는 1로 반환한다. type이 다르면 0을 반환한다. */
int confit_toml_value_bool(const ConfitTomlValue *value, int *out_value);

/** @brief table entry 수를 반환한다. table이 아니면 0이다. */
size_t confit_toml_table_size(const ConfitTomlValue *table);

/** @brief table entry key를 index 순서대로 반환한다. */
const char *confit_toml_table_key_at(const ConfitTomlValue *table,
                                         size_t index);

/** @brief table entry key의 decoded byte 길이를 반환한다. */
size_t confit_toml_table_key_size_at(const ConfitTomlValue *table,
                                     size_t index);

/** @brief table entry value를 index 순서대로 반환한다. */
const ConfitTomlValue *
confit_toml_table_value_at(const ConfitTomlValue *table, size_t index);

/**
 * @brief table에서 exact key를 직접 순회해 찾는다.
 *
 * multipart-key helper의 길이와 escape 제약을 피하기 위해 key path를 해석하지
 * 않는다. Schema loader는 각 table level을 명시적으로 순회해야 한다.
 */
const ConfitTomlValue *
confit_toml_table_find(const ConfitTomlValue *table, const char *key);

/** @brief array element 수를 반환한다. array가 아니면 0이다. */
size_t confit_toml_array_size(const ConfitTomlValue *array);

/** @brief array element를 index 순서대로 반환한다. */
const ConfitTomlValue *
confit_toml_array_at(const ConfitTomlValue *array, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_TOML_H */
