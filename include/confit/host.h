#ifndef CONFIT_HOST_H
#define CONFIT_HOST_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief host path separator 문자를 반환한다.
 *
 * Core layer는 path separator를 직접 분기하지 않는다. Path 조합이 필요하면
 * 이 header의 host path API를 통해 처리한다.
 *
 * @return 현재 host platform의 path separator 문자.
 */
char confit_host_path_separator(void);

/**
 * @brief 두 path fragment를 host path separator로 결합한다.
 *
 * `left`가 separator로 끝나면 separator를 중복해서 넣지 않는다. `right`가
 * 빈 문자열이면 `left`만 복사한다. 출력 buffer는 항상 NUL 종료되어야 하며,
 * buffer가 부족하면 오류를 반환한다.
 *
 * @param out 결과를 쓸 caller-owned buffer.
 * @param out_size `out` buffer 크기.
 * @param left 왼쪽 path fragment.
 * @param right 오른쪽 path fragment.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받을 optional diagnostic.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_path_join(char *out, size_t out_size,
                                   const char *left, const char *right,
                                   ConfitDiagnostic *diagnostic);

/**
 * @brief UTF-8 또는 ASCII text file 전체를 memory buffer로 읽는다.
 *
 * 반환된 buffer는 NUL 종료된다. Binary file 여부는 이 layer에서 판정하지 않으며,
 * parser adapter가 TOML 문법과 encoding policy를 검증한다. 성공 시 `*out_text`는
 * caller 소유가 되고 `confit_host_free`로 해제해야 한다.
 *
 * @param path 읽을 host path.
 * @param out_text 성공 시 할당된 text buffer를 받는다.
 * @param out_size 성공 시 NUL byte를 제외한 byte 길이를 받는다. 필요 없으면 `NULL`.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받을 optional diagnostic.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_read_text_file(const char *path, char **out_text,
                                        size_t *out_size,
                                        ConfitDiagnostic *diagnostic);

/**
 * @brief directory 바로 아래의 `.toml` file path 목록을 deterministic order로 읽는다.
 *
 * Directory가 없으면 빈 목록을 성공으로 반환한다. 반환된 문자열 배열과 각
 * 문자열은 caller 소유이며 `confit_host_string_list_free`로 해제해야 한다.
 * Subdirectory traversal은 하지 않는다.
 *
 * @param directory 조회할 host directory path.
 * @param out_paths 성공 시 할당된 path 문자열 배열을 받는다.
 * @param out_count 성공 시 path 개수를 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받을 optional diagnostic.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_list_toml_files(const char *directory,
                                         char ***out_paths, size_t *out_count,
                                         ConfitDiagnostic *diagnostic);

/**
 * @brief host adapter가 caller에게 넘긴 string list를 해제한다.
 *
 * @param items 해제할 문자열 배열. `NULL`은 허용한다.
 * @param count 배열 원소 개수.
 */
void confit_host_string_list_free(char **items, size_t count);

/**
 * @brief host adapter가 caller에게 넘긴 heap allocation을 해제한다.
 *
 * @param allocation 해제할 pointer. `NULL`은 허용한다.
 */
void confit_host_free(void *allocation);

/**
 * @brief stdout에 문자열을 쓴다.
 *
 * @param text 쓸 NUL 종료 문자열.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_stdout_write(const char *text);

/**
 * @brief stdout에 문자열과 trailing newline을 쓴다.
 *
 * @param text 쓸 NUL 종료 문자열.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_stdout_write_line(const char *text);

/**
 * @brief stderr에 문자열을 쓴다.
 *
 * @param text 쓸 NUL 종료 문자열.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_stderr_write(const char *text);

/**
 * @brief stderr에 문자열과 trailing newline을 쓴다.
 *
 * @param text 쓸 NUL 종료 문자열.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_stderr_write_line(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_HOST_H */
