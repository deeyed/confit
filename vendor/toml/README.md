# Confit TOML Vendor

이 디렉터리는 Confit parser adapter 뒤에 숨겨진 TOML syntax loader를 보관한다.

Round 4의 loader는 schema를 해석하지 않는다. 역할은 다음으로 제한된다.

- TOML text를 문법상 load 가능한지 검사한다.
- table header 수와 key/value entry 수 같은 최소 metadata를 기록한다.
- parse error line/column/message를 adapter에 전달한다.

Public Confit code는 `include/confit/parser.h`와 `src/parser/` adapter만 사용한다. Vendor header를
core, schema loader, CLI, TUI가 직접 include하지 않는다.

License: MIT.
