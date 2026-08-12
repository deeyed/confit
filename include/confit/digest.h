#ifndef CONFIT_DIGEST_H
#define CONFIT_DIGEST_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief NUL-terminated text의 SHA-256을 lower-case hexadecimal로 쓴다. */
void confit_v5_sha256_hex(const char *text, char output[65]);

/** @brief bounded byte sequence의 SHA-256을 lower-case hexadecimal로 쓴다. */
void confit_v5_sha256_bytes(const void *bytes, size_t size, char output[65]);

/**
 * @brief absolute regular file의 SHA-256 identity를 계산한다.
 *
 * Symbolic link와 256 MiB를 넘는 입력은 따라가거나 부분적으로 승인하지 않는다.
 */
ConfitStatus confit_v5_sha256_file(const char *path, char output[65],
                                   ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_DIGEST_H */
