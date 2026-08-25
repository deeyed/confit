#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "confit/digest.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct ConfitV5Sha256 {
  uint32_t state[8];
  uint64_t bit_count;
  unsigned char block[64];
  size_t block_size;
} ConfitV5Sha256;

static uint32_t confit_v5_rotr(uint32_t value, unsigned int shift) {
  return (value >> shift) | (value << (32U - shift));
}

static void confit_v5_sha256_init(ConfitV5Sha256 *hash) {
  static const uint32_t initial[] = {
      UINT32_C(0x6A09E667), UINT32_C(0xBB67AE85), UINT32_C(0x3C6EF372),
      UINT32_C(0xA54FF53A), UINT32_C(0x510E527F), UINT32_C(0x9B05688C),
      UINT32_C(0x1F83D9AB), UINT32_C(0x5BE0CD19)};
  memcpy(hash->state, initial, sizeof(initial));
  hash->bit_count = 0U;
  hash->block_size = 0U;
}

static void confit_v5_sha256_compress(ConfitV5Sha256 *hash,
                                      const unsigned char block[64]) {
  static const uint32_t constants[] = {
      UINT32_C(0x428A2F98), UINT32_C(0x71374491), UINT32_C(0xB5C0FBCF),
      UINT32_C(0xE9B5DBA5), UINT32_C(0x3956C25B), UINT32_C(0x59F111F1),
      UINT32_C(0x923F82A4), UINT32_C(0xAB1C5ED5), UINT32_C(0xD807AA98),
      UINT32_C(0x12835B01), UINT32_C(0x243185BE), UINT32_C(0x550C7DC3),
      UINT32_C(0x72BE5D74), UINT32_C(0x80DEB1FE), UINT32_C(0x9BDC06A7),
      UINT32_C(0xC19BF174), UINT32_C(0xE49B69C1), UINT32_C(0xEFBE4786),
      UINT32_C(0x0FC19DC6), UINT32_C(0x240CA1CC), UINT32_C(0x2DE92C6F),
      UINT32_C(0x4A7484AA), UINT32_C(0x5CB0A9DC), UINT32_C(0x76F988DA),
      UINT32_C(0x983E5152), UINT32_C(0xA831C66D), UINT32_C(0xB00327C8),
      UINT32_C(0xBF597FC7), UINT32_C(0xC6E00BF3), UINT32_C(0xD5A79147),
      UINT32_C(0x06CA6351), UINT32_C(0x14292967), UINT32_C(0x27B70A85),
      UINT32_C(0x2E1B2138), UINT32_C(0x4D2C6DFC), UINT32_C(0x53380D13),
      UINT32_C(0x650A7354), UINT32_C(0x766A0ABB), UINT32_C(0x81C2C92E),
      UINT32_C(0x92722C85), UINT32_C(0xA2BFE8A1), UINT32_C(0xA81A664B),
      UINT32_C(0xC24B8B70), UINT32_C(0xC76C51A3), UINT32_C(0xD192E819),
      UINT32_C(0xD6990624), UINT32_C(0xF40E3585), UINT32_C(0x106AA070),
      UINT32_C(0x19A4C116), UINT32_C(0x1E376C08), UINT32_C(0x2748774C),
      UINT32_C(0x34B0BCB5), UINT32_C(0x391C0CB3), UINT32_C(0x4ED8AA4A),
      UINT32_C(0x5B9CCA4F), UINT32_C(0x682E6FF3), UINT32_C(0x748F82EE),
      UINT32_C(0x78A5636F), UINT32_C(0x84C87814), UINT32_C(0x8CC70208),
      UINT32_C(0x90BEFFFA), UINT32_C(0xA4506CEB), UINT32_C(0xBEF9A3F7),
      UINT32_C(0xC67178F2)};
  uint32_t schedule[64];
  uint32_t a, b, c, d, e, f, g, h;
  size_t index;
  for (index = 0U; index < 16U; ++index) {
    const size_t offset = index * 4U;
    schedule[index] = ((uint32_t)block[offset] << 24U) |
                      ((uint32_t)block[offset + 1U] << 16U) |
                      ((uint32_t)block[offset + 2U] << 8U) |
                      (uint32_t)block[offset + 3U];
  }
  for (index = 16U; index < 64U; ++index) {
    const uint32_t small0 = confit_v5_rotr(schedule[index - 15U], 7U) ^
                            confit_v5_rotr(schedule[index - 15U], 18U) ^
                            (schedule[index - 15U] >> 3U);
    const uint32_t small1 = confit_v5_rotr(schedule[index - 2U], 17U) ^
                            confit_v5_rotr(schedule[index - 2U], 19U) ^
                            (schedule[index - 2U] >> 10U);
    schedule[index] = schedule[index - 16U] + small0 +
                      schedule[index - 7U] + small1;
  }
  a = hash->state[0]; b = hash->state[1]; c = hash->state[2]; d = hash->state[3];
  e = hash->state[4]; f = hash->state[5]; g = hash->state[6]; h = hash->state[7];
  for (index = 0U; index < 64U; ++index) {
    const uint32_t big1 = confit_v5_rotr(e, 6U) ^ confit_v5_rotr(e, 11U) ^
                          confit_v5_rotr(e, 25U);
    const uint32_t choose = (e & f) ^ ((~e) & g);
    const uint32_t temporary1 =
        h + big1 + choose + constants[index] + schedule[index];
    const uint32_t big0 = confit_v5_rotr(a, 2U) ^ confit_v5_rotr(a, 13U) ^
                          confit_v5_rotr(a, 22U);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temporary2 = big0 + majority;
    h = g; g = f; f = e; e = d + temporary1;
    d = c; c = b; b = a; a = temporary1 + temporary2;
  }
  hash->state[0] += a; hash->state[1] += b; hash->state[2] += c;
  hash->state[3] += d; hash->state[4] += e; hash->state[5] += f;
  hash->state[6] += g; hash->state[7] += h;
}

static void confit_v5_sha256_update(ConfitV5Sha256 *hash,
                                    const unsigned char *text, size_t size) {
  size_t index = 0U;
  hash->bit_count += (uint64_t)size * UINT64_C(8);
  while (index < size) {
    const size_t available = sizeof(hash->block) - hash->block_size;
    const size_t remaining = size - index;
    const size_t copy_size = remaining < available ? remaining : available;
    memcpy(hash->block + hash->block_size, text + index, copy_size);
    hash->block_size += copy_size;
    index += copy_size;
    if (hash->block_size == sizeof(hash->block)) {
      confit_v5_sha256_compress(hash, hash->block);
      hash->block_size = 0U;
    }
  }
}

static void confit_v5_sha256_final(ConfitV5Sha256 *hash,
                                   unsigned char output[32]) {
  size_t index;
  const uint64_t bit_count = hash->bit_count;
  hash->block[hash->block_size++] = 0x80U;
  if (hash->block_size > 56U) {
    while (hash->block_size < 64U) hash->block[hash->block_size++] = 0U;
    confit_v5_sha256_compress(hash, hash->block);
    hash->block_size = 0U;
  }
  while (hash->block_size < 56U) hash->block[hash->block_size++] = 0U;
  for (index = 0U; index < 8U; ++index) {
    hash->block[63U - index] = (unsigned char)(bit_count >> (index * 8U));
  }
  confit_v5_sha256_compress(hash, hash->block);
  for (index = 0U; index < 8U; ++index) {
    output[index * 4U] = (unsigned char)(hash->state[index] >> 24U);
    output[index * 4U + 1U] = (unsigned char)(hash->state[index] >> 16U);
    output[index * 4U + 2U] = (unsigned char)(hash->state[index] >> 8U);
    output[index * 4U + 3U] = (unsigned char)hash->state[index];
  }
}

void confit_v5_sha256_bytes(const void *data, size_t size, char output[65]) {
  static const char digits[] = "0123456789abcdef";
  unsigned char bytes[32];
  ConfitV5Sha256 hash;
  size_t index;
  if (output == 0) return;
  if (data == 0 && size != 0U) {
    output[0] = '\0';
    return;
  }
  confit_v5_sha256_init(&hash);
  if (size != 0U) confit_v5_sha256_update(&hash, data, size);
  confit_v5_sha256_final(&hash, bytes);
  for (index = 0U; index < sizeof(bytes); ++index) {
    output[index * 2U] = digits[bytes[index] >> 4U];
    output[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
  }
  output[64] = '\0';
}

void confit_v5_sha256_hex(const char *text, char output[65]) {
  if (output == 0) return;
  if (text == 0) {
    output[0] = '\0';
    return;
  }
  confit_v5_sha256_bytes(text, strlen(text), output);
}

ConfitStatus confit_v5_sha256_file(const char *path, char output[65],
                                   ConfitDiagnostic *diagnostic) {
  static const char digits[] = "0123456789abcdef";
  unsigned char buffer[16384];
  unsigned char bytes[32];
  ConfitV5Sha256 hash;
  struct stat before;
  struct stat opened;
  size_t total = 0U;
  size_t index;
  int descriptor;
  char canonical[4096];
  if (path == 0 || path[0] != '/' || output == 0 ||
      realpath(path, canonical) == 0 || strcmp(path, canonical) != 0 ||
      lstat(path, &before) != 0 ||
      !S_ISREG(before.st_mode)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0U,
                          0U, "digest input must be an absolute regular file");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0 || fstat(descriptor, &opened) != 0 ||
      opened.st_dev != before.st_dev || opened.st_ino != before.st_ino ||
      !S_ISREG(opened.st_mode)) {
    if (descriptor >= 0) (void)close(descriptor);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, path, 0U, 0U,
                          "digest input identity changed before open");
    return CONFIT_ERR_UNSUPPORTED;
  }
  confit_v5_sha256_init(&hash);
  for (;;) {
    const ssize_t count = read(descriptor, buffer, sizeof(buffer));
    if (count < 0) {
      (void)close(descriptor);
      output[0] = '\0';
      confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, path, 0U, 0U,
                            "digest input read failed");
      return CONFIT_ERR_UNSUPPORTED;
    }
    if (count == 0) break;
    if ((size_t)count > (256U * 1024U * 1024U) - total) {
      (void)close(descriptor);
      output[0] = '\0';
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0U, 0U,
                            "digest input exceeds 256 MiB bound");
      return CONFIT_ERR_GENERATION;
    }
    total += (size_t)count;
    confit_v5_sha256_update(&hash, buffer, (size_t)count);
  }
  if (fstat(descriptor, &opened) != 0 || opened.st_dev != before.st_dev ||
      opened.st_ino != before.st_ino || opened.st_size != before.st_size ||
      close(descriptor) != 0 || total == 0U) {
    output[0] = '\0';
    confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, path, 0U, 0U,
                          "digest input is empty or changed during read");
    return CONFIT_ERR_UNSUPPORTED;
  }
  confit_v5_sha256_final(&hash, bytes);
  for (index = 0U; index < sizeof(bytes); ++index) {
    output[index * 2U] = digits[bytes[index] >> 4U];
    output[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
  }
  output[64] = '\0';
  return CONFIT_OK;
}
