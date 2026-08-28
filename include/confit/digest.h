#ifndef CONFIT_DIGEST_H
#define CONFIT_DIGEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief NUL-terminated text의 SHA-256을 lower-case hexadecimal로 쓴다. */
void confit_sha256_text(const char *text, char output[65]);

/** @brief bounded byte sequence의 SHA-256을 lower-case hexadecimal로 쓴다. */
void confit_sha256_bytes(const void *bytes, size_t size, char output[65]);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_DIGEST_H */
