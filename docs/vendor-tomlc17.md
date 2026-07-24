---
doc_type: dependency-record
status: accepted
authority: normative
last_verified: 2026-07-24
---

# tomlc17 Vendor Record

Confit의 `schema_version = 2` TOML syntax adapter는 `tomlc17` amalgamation을
사용한다. 이 dependency는 TOML syntax와 immutable value tree만 제공하며 Confit
schema, expression, import, merge, resolver 의미론을 소유하지 않는다.

## 고정 원본

- upstream: `https://github.com/cktan/tomlc17`
- release tag: `R260618`
- commit: `7813bdd218b2b5f54940a9759ec0ffb3b60c1d1f`
- upstream release marker: `TOMLC17_RELEASE_AFTER "260618"`
- license: MIT, `vendor/tomlc17/LICENSE`

Vendored 원본은 다음 세 파일뿐이다.

| File | SHA-256 |
|---|---|
| `vendor/tomlc17/tomlc17.c` | `f28f3742808505b5c55189b72cf62705e824276eca6533b36e8751a8433482aa` |
| `vendor/tomlc17/tomlc17.h` | `eef9a891b93fc6235a9552db16286cefc684be8fae49018bedf1c6ab9ca55b87` |
| `vendor/tomlc17/LICENSE` | `e6eae7966fa001217173768e984f758fd49cef45afb2e8f471e947a9712f06c9` |

Confit은 이 파일을 patch하지 않는다. 정책, memory ownership, diagnostic 형식,
schema binding은 `src/parser/v2/tomlc17_adapter.c`에만 둔다.

## 사용 경계

- v1은 현재 `src/parser/toml_scan.c`와 v1 loader를 계속 사용한다.
- v2는 host adapter가 읽은 memory buffer를 `toml_parse_named()`로 parse한다.
- `toml_parse_file*()`는 file open을 우회하므로 사용하지 않는다.
- `toml_merge()`는 Confit import/assignment precedence를 대체할 수 없으므로
  사용하지 않는다.
- `toml_seek()`는 multipart key 길이와 escaping 제약이 있으므로 사용하지 않는다.
  Confit adapter와 v2 loader가 table level을 직접 순회한다.
- `toml_set_option()`은 global mutable option이므로 호출하지 않는다. adapter가
  자체적으로 UTF-8을 검사해 parser option global state를 만들지 않는다.

V2 source는 TOML 1.1 syntax parser로 읽는다. 그러나 schema loader는
`schema-v2.md`에 문서화된 field와 value shape만 허용한다. TOML parser가 date/time,
array, inline table을 읽을 수 있다는 사실이 모든 v2 field에서 해당 값을 허용한다는
뜻은 아니다.

## 갱신 절차

1. `/tmp`에 upstream tag를 clone하고 exact commit을 확인한다.
2. amalgamation과 license를 원본 그대로 교체한다.
3. SHA-256과 이 문서의 release marker를 함께 갱신한다.
4. adapter valid/invalid/UTF-8/CRLF/source-span test를 실행한다.
5. v1 baseline, sanitizer, macOS/Linux/Windows clang gate를 모두 통과시킨다.
6. upstream file을 local patch해야 한다면 vendor patch가 아니라 adapter 변경 또는
   upstream contribution을 먼저 검토한다.
