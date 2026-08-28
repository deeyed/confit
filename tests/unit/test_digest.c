#include <string.h>

#include "confit/digest.h"

int main(void) {
  char digest[65];

  confit_sha256_text("", digest);
  if (strcmp(digest,
             "e3b0c44298fc1c149afbf4c8996fb924"
             "27ae41e4649b934ca495991b7852b855") != 0) {
    return 1;
  }
  confit_sha256_bytes("abc", 3U, digest);
  if (strcmp(digest,
             "ba7816bf8f01cfea414140de5dae2223"
             "b00361a396177a9cb410ff61f20015ad") != 0) {
    return 2;
  }
  confit_sha256_bytes(0, 1U, digest);
  if (digest[0] != '\0') {
    return 3;
  }
  confit_sha256_text(0, digest);
  return digest[0] == '\0' ? 0 : 4;
}
