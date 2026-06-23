#ifndef CONFIT_PARSER_H
#define CONFIT_PARSER_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Confit TOML parser adapter가 소유하는 parsed document handle이다.
 *
 * Round 4 parser는 schema를 해석하지 않는다. 이 handle은 TOML syntax load
 * 성공 여부와 source metadata만 보존하며, option/profile model 변환은 이후
 * schema loader 라운드에서 별도 layer가 담당한다.
 */
typedef struct ConfitParserDocument ConfitParserDocument;

/**
 * @brief host path에서 TOML file을 읽고 syntax validation을 수행한다.
 *
 * 성공 시 `*out_document`는 caller 소유가 되며 `confit_parser_document_free`로
 * 해제해야 한다. 실패 시 diagnostic에는 parse error line/column이 기록된다.
 *
 * @param path 읽을 TOML file path.
 * @param out_document 성공 시 parsed document handle을 받는다.
 * @param diagnostic 실패 시 오류 정보가 기록되는 optional diagnostic.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_parser_load_file(const char *path,
                                     ConfitParserDocument **out_document,
                                     ConfitDiagnostic *diagnostic);

/**
 * @brief memory에 있는 TOML text를 syntax validation한다.
 *
 * `source_name`은 diagnostic path로만 사용된다. 성공 시 text는 document handle
 * 안에 복사되어 caller text lifetime과 분리된다.
 *
 * @param source_name diagnostic에 표시할 source 이름. 없으면 `NULL`.
 * @param text TOML source text.
 * @param text_size `text` byte 길이.
 * @param out_document 성공 시 parsed document handle을 받는다.
 * @param diagnostic 실패 시 오류 정보가 기록되는 optional diagnostic.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_parser_load_text(const char *source_name, const char *text,
                                     size_t text_size,
                                     ConfitParserDocument **out_document,
                                     ConfitDiagnostic *diagnostic);

/**
 * @brief parser document handle을 해제한다.
 *
 * @param document 해제할 document. `NULL`은 허용한다.
 */
void confit_parser_document_free(ConfitParserDocument *document);

/**
 * @brief parser document의 source 이름을 반환한다.
 *
 * @param document 조회할 document.
 * @return source 이름. 없으면 `NULL`.
 */
const char *
confit_parser_document_source_name(const ConfitParserDocument *document);

/**
 * @brief parser document의 source byte 길이를 반환한다.
 *
 * @param document 조회할 document.
 * @return NUL byte를 제외한 source byte 길이.
 */
size_t confit_parser_document_source_size(
    const ConfitParserDocument *document);

/**
 * @brief parser가 관찰한 source line 수를 반환한다.
 *
 * @param document 조회할 document.
 * @return source line 수.
 */
size_t confit_parser_document_line_count(
    const ConfitParserDocument *document);

/**
 * @brief parser가 관찰한 TOML table header 수를 반환한다.
 *
 * @param document 조회할 document.
 * @return table header 수.
 */
size_t confit_parser_document_table_count(
    const ConfitParserDocument *document);

/**
 * @brief parser가 관찰한 TOML key/value entry 수를 반환한다.
 *
 * @param document 조회할 document.
 * @return key/value entry 수.
 */
size_t confit_parser_document_key_count(const ConfitParserDocument *document);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_PARSER_H */
