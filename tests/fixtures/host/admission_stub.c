/*
 * Confit host-boundary test가 exact source topology와 version measurement를 검증할 때만
 * 사용하는 compile fixture다. 실제 Parus admission semantics의 positive proof가 아니며,
 * build-enter가 이 source를 caller 입력으로 선택하는 경로도 존재하지 않는다.
 */
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    (void)puts("parus-admit 1.0.0");
    return 0;
  }
  return 2;
}
