# Schema V2 Hardening

이 문서는 Schema V2를 큰 구성과 비정상 입력에 적용할 때 지켜지는 실행
경계를 설명한다. 이 경계는 V1 형식이나 V2의 configuration 의미를 바꾸지
않는다.

## 입력 경계

- V2 import traversal의 최대 깊이는 128이다. 이를 넘으면 partial project를
  반환하지 않고 `schema v2 import depth exceeds the supported limit` diagnostic으로
  실패한다.
- import cycle, config root 탈출, duplicate canonical import는 이 깊이 제한과
  별도로 계속 hard error다.
- TOML adapter는 UTF-8을 먼저 검사하고, parser 실패 시 document handle을 반환하지
  않는다. bounded parser fuzz와 expression fuzz는 CTest `fuzz` label로 실행한다.
- expression parser는 source byte, token, AST node, nesting의 고정 limit를 사용한다.

## 대형 Schema 적재

V2 loader는 semantic identifier uniqueness를 project-owned hash index로 검증한다.
symbol, menu, choice, constraint array는 기하 성장 capacity를 사용한다. 따라서
definition 개수가 커질 때 declaration order를 보존하면서 매 definition마다 전체
array를 복사하지 않는다.

Linker는 expression lookup index를 만들어 `(role, owner id, occurrence)`로 찾는다.
constraint compiler는 linker가 보장하는 source order를 이용해 required
`when`/`require` pair를 선형으로 연결한다. compiled graph edge buffer도 기하
성장한다. 이 세 경계는 large graph에서 생기던 반복 선형 탐색과 재할당을 없애며,
serialized artifact의 순서나 snapshot hash에는 관여하지 않는다.

기본 CTest regression은 10,000 option synthetic resolver, 100회 menu lookup,
1,000회 incremental reconcile을 사용한다. 이는 일반 개발 machine에서도 매
push마다 실행하는 빠른 gate다. release candidate의 20,000 option/100,000 edge
목표는 별도 높은-memory stress host에서 실행해야 하며, 그 결과는 release QA
기록으로 남긴다. 이 수치는 기본 CTest가 이미 보장한다고 해석하면 안 된다.

## Sanitizer Gate

macOS/Linux의 GNU-style Clang 또는 GCC에서 다음처럼 AddressSanitizer와
UndefinedBehaviorSanitizer를 켤 수 있다.

```sh
cmake -S . -B build-sanitizer -DCMAKE_BUILD_TYPE=Debug \
  -DCONFIT_ENABLE_SANITIZERS=ON
cmake --build build-sanitizer --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-sanitizer --output-on-failure \
  -LE 'scale|sanitizer-exclude'
```

macOS의 bundled AddressSanitizer는 leak detector를 제공하지 않을 수 있다. 그
host에서는 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`을 사용한다. GitHub
Actions Linux sanitizer lane은 `detect_leaks=1`을 유지한다. memory-heavy scale
label은 일반 Linux/macOS test lane에서 실행하며 sanitizer lane에서는 제외한다.
Windows portability를 위한 C child-process integration tests도 sanitizer에서는
제외하고, 일반 platform lane에서 실행한다.

Windows CLI-only preview는 이 sanitizer option의 대상이 아니다. Windows에서는
GNU-style clang CTest와 C integration workflow로 CLI behavior를 확인한다.

## 결정성

generated artifact는 LF, canonical logical path, stable sort를 사용한다. host
directory enumeration, CRLF input, path separator, locale은 artifact meaning을
바꾸면 안 된다. allocation failure test는 V2 model loader가 partial project를
남기지 않고 allocator-owned memory를 해제하는지를 검사한다. snapshot과 artifact
writer는 validation success 뒤의 immutable input만 publish한다.
