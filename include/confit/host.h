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
 * @brief existing host path를 symlink 해석 뒤의 canonical absolute path로 만든다.
 *
 * schema import loader처럼 host filesystem boundary 검증이 필요한 계층만 사용한다.
 * 성공 결과는 native separator를 쓸 수 있으며, caller buffer는 결과 전체와 NUL을
 * 담을 만큼 커야 한다.
 */
ConfitStatus confit_host_path_canonicalize(char *out, size_t out_size,
                                           const char *path,
                                           ConfitDiagnostic *diagnostic);

/**
 * @brief PATH에서 단일 executable 이름을 찾아 canonical absolute path로 봉인한다.
 *
 * 입력은 path separator나 shell metacharacter가 없는 executable basename이어야 한다.
 * 성공 결과가 이후 artifact에 포함되므로 ambient PATH 자체가 아니라 실제 resolved
 * identity가 configuration 의미에 들어간다.
 */
ConfitStatus confit_host_resolve_executable(char *out, size_t out_size,
                                            const char *name,
                                            ConfitDiagnostic *diagnostic);

/**
 * @brief 현재 실행 중인 Confit image의 canonical absolute path를 구한다.
 *
 * `argv[0]`이나 PATH를 신뢰하지 않고 host kernel이 게시하는 executable identity를
 * 사용한다. 지원 host가 이 identity를 제공하지 않거나 canonical path로 봉인할 수
 * 없으면 fail-closed한다.
 *
 * @param out canonical absolute executable path를 받을 buffer.
 * @param out_size `out`의 byte 크기.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받을 optional diagnostic.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_self_executable(char *out, size_t out_size,
                                         ConfitDiagnostic *diagnostic);

/**
 * @brief absolute executable을 argv 두 원소로 실행해 bounded stdout 한 줄을 읽는다.
 *
 * Shell, command string, stderr merge와 arbitrary argv는 제공하지 않는다. 출력이
 * buffer를 넘거나 child가 비정상 종료하면 partial text를 성공으로 반환하지 않는다.
 */
ConfitStatus confit_host_capture_one_argument(
    char *out, size_t out_size, const char *executable, const char *argument,
    ConfitDiagnostic *diagnostic);

/**
 * @brief absolute executable의 bounded stdout에서 첫 줄만 identity로 읽는다.
 *
 * 전체 stdout은 4096 bytes로 제한해 나머지 줄을 버리더라도 output bomb가 되지 않게
 * 한다. Version banner처럼 첫 줄이 semantic identity이고 뒤쪽 attribution 문구는
 * presentation인 도구에만 사용한다.
 */
ConfitStatus confit_host_capture_first_line_argument(
    char *out, size_t out_size, const char *executable, const char *argument,
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
 * @brief host filesystem에 regular file이 존재하는지 확인한다.
 *
 * 이 helper는 loader bootstrap이 project root와 config root를 판별할 때만 쓴다.
 * 권한 오류와 존재하지 않는 path는 모두 0으로 처리한다. 상세 I/O diagnostic이
 * 필요한 호출자는 `confit_host_read_text_file()`를 사용해야 한다.
 *
 * @param path 검사할 host path.
 * @return 읽기 가능한 regular file이면 1, 그 밖에는 0.
 */
int confit_host_file_exists(const char *path);

/**
 * @brief host filesystem에 directory가 존재하는지 확인한다.
 *
 * Typed descriptor가 file field와 directory field를 서로 바꾸어 넣지 못하게 하는
 * read-only probe다. 권한 오류, symlink 해석 실패와 비-directory는 0이다.
 *
 * @param path 검사할 host path.
 * @return 접근 가능한 directory이면 1, 그 밖에는 0.
 */
int confit_host_directory_exists(const char *path);

/**
 * @brief UTF-8 또는 ASCII text file을 host filesystem에 쓴다.
 *
 * Parent directory는 caller가 먼저 만들어야 한다. 이 API는 host layer가 파일
 * writing을 소유하게 해 core/CLI가 `fopen` 같은 hosted API를 직접 의존하지
 * 않도록 한다.
 *
 * @param path 쓸 host path.
 * @param text 쓸 NUL 종료 문자열.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받을 optional diagnostic.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_write_text_file(const char *path, const char *text,
                                         ConfitDiagnostic *diagnostic);

/**
 * @brief text가 바뀐 경우에만 같은 directory의 temporary file을 atomic replace한다.
 *
 * 성공 시 `out_changed`는 새 bytes를 publish했으면 1, 기존 bytes와 같아 write를
 * 생략했으면 0이다. Parent directory는 caller가 먼저 만들어야 한다.
 */
ConfitStatus confit_host_write_text_file_if_changed_atomic(
    const char *path, const char *text, int *out_changed,
    ConfitDiagnostic *diagnostic);

/**
 * @brief directory path를 재귀적으로 생성한다.
 *
 * 이미 존재하는 directory는 성공으로 처리한다.
 *
 * @param path 생성할 host directory path.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받을 optional diagnostic.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_host_make_directories(const char *path,
                                          ConfitDiagnostic *diagnostic);

/**
 * @brief 비어 있는 LUCA object root와 invocation별 admission leaf를 준비한다.
 *
 * 이 API는 LUCA의 stage-0 bootstrap 전용이다. `root`의 parent는 이미 존재하는
 * canonical absolute directory여야 하며, `invocation`은 decimal PID token이다.
 * 구현은 symlink를 따르지 않는 descriptor walk로 root와 invocation leaf를 만들고,
 * root lock을 유지한 채 fixed compiler argv와 bounded file limit로 repository의 exact
 * `tools/host/admit/main.c`를 `luca-admit`으로 컴파일한다. Source path나 임의 compiler
 * argv를 받는 범용 실행 API가 아니다. `stage0_confit`은 CLI가 host kernel에서 구한
 * 현재 executable의 canonical absolute path이며 public `build-enter` 문법으로 별도
 * 주입할 수 없다. 이 parameter는 direct host-boundary test가 동일 AuditRecord
 * 경계를 검증할 때만 명시적으로 전달한다.
 */
ConfitStatus confit_host_prepare_luca_build_root(
    const char *root, const char *repository, const char *invocation,
    const char *stage0_confit, const char *bmake, const char *host_compiler,
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
 * @brief fixed directory root 아래에서 exact file name만 bounded lexical order로 찾는다.
 *
 * Symlink directory/file은 따라가지 않고 즉시 거부한다. `max_depth`, `max_count`,
 * `max_file_bytes`는 모두 0보다 커야 하며, limit을 넘기면 partial list를 반환하지
 * 않는다. 이 API는 configuration manifest discovery 전용이며 source globbing이나
 * Makefile discovery를 제공하지 않는다.
 */
ConfitStatus confit_host_list_named_files_recursive(
    const char *directory, const char *file_name, size_t max_depth,
    size_t max_count, size_t max_file_bytes, char ***out_paths,
    size_t *out_count, ConfitDiagnostic *diagnostic);

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
