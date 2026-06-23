#ifndef CONFIT_GENERATOR_H
#define CONFIT_GENERATOR_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief C config header generator option이다.
 *
 * `profile_name`과 `target_name`은 header 상단의 provenance comment에만
 * 사용된다. 값이 없으면 `unknown`으로 출력한다.
 */
typedef struct ConfitConfigHeaderOptions {
  /** resolved profile name. 없으면 `NULL`. */
  const char *profile_name;
  /** resolved target name. 없으면 `NULL`. */
  const char *target_name;
} ConfitConfigHeaderOptions;

/**
 * @brief report와 manifest에 기록할 입력 file record다.
 *
 * `path`와 `sha256`은 caller가 소유한다. Generator는 값을 복사하지 않고
 * deterministic JSON으로 직렬화만 한다.
 */
typedef struct ConfitInputFile {
  /** repo/project-relative input path. */
  const char *path;
  /** lowercase 또는 uppercase SHA-256 hex digest. */
  const char *sha256;
} ConfitInputFile;

/**
 * @brief report generator option이다.
 *
 * `input_files`는 `config.inputs.json` 생성에만 사용한다. JSON 출력은 path
 * lexical order로 정렬되어 caller 입력 순서에 의존하지 않는다.
 */
typedef struct ConfitReportOptions {
  /** resolved profile name. 없으면 `unknown`. */
  const char *profile_name;
  /** resolved target name. 없으면 `unknown`. */
  const char *target_name;
  /** input file record 목록. 없으면 `NULL`. */
  const ConfitInputFile *input_files;
  /** input file record 개수. */
  size_t input_file_count;
} ConfitReportOptions;

/**
 * @brief resolved config에서 deterministic C `config.h` 문자열을 생성한다.
 *
 * Header는 project include guard, `#define`, resolved source hash를 포함한다.
 * Timestamp와 absolute path는 넣지 않는다. 반환된 문자열은 caller가 소유하며
 * `confit_generator_string_free`로 해제한다.
 *
 * @param project source project model.
 * @param config resolver가 만든 resolved config.
 * @param options optional header generator options.
 * @param out_text 성공 시 NUL 종료 header text를 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_generate_config_header(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitConfigHeaderOptions *options, char **out_text,
    ConfitDiagnostic *diagnostic);

/**
 * @brief `config.report.json` 내용을 deterministic JSON으로 생성한다.
 *
 * 반환된 문자열은 caller가 소유하며 `confit_generator_string_free`로 해제한다.
 *
 * @param project source project model.
 * @param config resolver가 만든 resolved config.
 * @param options optional report generator options.
 * @param out_json 성공 시 NUL 종료 JSON 문자열을 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_generate_report_json(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitReportOptions *options, char **out_json,
    ConfitDiagnostic *diagnostic);

/**
 * @brief `config.explain.txt` 내용을 deterministic text로 생성한다.
 *
 * @param project source project model.
 * @param config resolver가 만든 resolved config.
 * @param options optional report generator options.
 * @param out_text 성공 시 NUL 종료 text를 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_generate_explain_report(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitReportOptions *options, char **out_text,
    ConfitDiagnostic *diagnostic);

/**
 * @brief `config.inputs.json` 내용을 deterministic JSON으로 생성한다.
 *
 * @param project source project model.
 * @param options input file record를 포함한 report generator options.
 * @param out_json 성공 시 NUL 종료 JSON 문자열을 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_generate_inputs_json(const ConfitProject *project,
                                         const ConfitReportOptions *options,
                                         char **out_json,
                                         ConfitDiagnostic *diagnostic);

/**
 * @brief generator module이 caller에게 넘긴 문자열 allocation을 해제한다.
 *
 * @param text 해제할 문자열. `NULL`은 허용한다.
 */
void confit_generator_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_GENERATOR_H */
