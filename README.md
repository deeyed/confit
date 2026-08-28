# Confit

Confit은 TOML로 작성한 명시적 configuration graph와 작은 사용자 값 파일을
해석하여, typed configuration data를 결정론적으로 확정하고 immutable snapshot으로
출판하는 generic configuration tool이다. Confit은 build를 실행하거나 project source,
Makefile, compiler invocation, link graph를 분석하지 않는다.

이 checkout의 `codex/confit-v6` 브랜치는 schema 6 개발선이다. 현재 브랜치가 따라야 할
정본 계약은 다음 두 문서다.

- [Schema 6 configuration contract](docs/config-v6.md)
- [Schema 6 architecture and security contract](docs/architecture-v6.md)
- [Schema 6 bootstrap contract](docs/bootstrap-v6.md)
- [Schema 6 generic core model](docs/model-v6.md)
- [Schema 6 descriptor-rooted host I/O](docs/host-v6.md)
- [Schema 6 exact input-image ownership](docs/input-v6.md)
- [Schema 6 explicit source graph](docs/source-v6.md)
- [Schema 6 minimal structural loader](docs/schema-v6.md)
- [Schema 6 typed declaration semantics](docs/types-v6.md)
- [Schema 6 bounded dependency expressions](docs/expression-v6.md)
- [Schema 6 deterministic resolver](docs/resolver-v6.md)
- [Schema 6 user configuration and minimal serialization](docs/user-config-v6.md)
- [Schema 6 immutable selected snapshots](docs/snapshot-v6.md)
- [Schema 6 R10 mid-program audit](docs/audits/confit-v6-r10.md)

## 현재 구현 상태

Schema 6는 22개 검증 라운드로 구현한다. R01은 위 계약을 고정했고 R02는 분기
기준점의 schema 5 parser, workflow, generator와 consumer-specific host capability를
제거했다. R03은 explicit clang+bmake bootstrap을 닫았고 R04는 public limits와 pure
in-memory generic model을 구현했다. R05는 descriptor-rooted bounded POSIX host I/O primitive를
추가했고 R06은 한 번 읽은 byte image의 TOML parse, SHA-256, file identity, line index ownership을
결속했다. R07은 entry와 reachable `[menu].source` literal만 따라가는 bounded source graph를
추가했다. R08은 reachable input image에 대해서만 닫힌 entry, menu, config, user-value
문서 구조를 검증하는 schema loader를 추가했다. R09는 다섯 public type의 native TOML
default, numeric range, enum domain과 field applicability를 검증하여 generic typed catalog에
소유시키고, hex token의 lexical identity를 같은 input byte image에서 보존한다.
R10 강감사에서 vendored TOML file-parser capability를 compile out하고 schema 전용 hex
lexeme helper를 internal ownership-checked seam으로 축소했다. 현재 binary는 여전히 `help`와
`--version`만 성공하는 development skeleton이다. R11은 동결된 `depends_on` grammar의
bounded lexer/parser, 전 reference link와 type/domain 검사, cycle rejection, stable
prerequisite order, read-only short-circuit evaluator와 reason tree를 추가했다.
R12는 declaration default와 unordered typed assignment를 single-writer 규칙으로 resolve하고,
availability, default/user origin, effective value와 owned causal reason을 lexical-order의
immutable result로 만든다. Unknown/duplicate/wrong-domain assignment와 unavailable non-default
값은 partial result 없이 거부한다. R13은 explicit user TOML의 native scalar를 catalog에
link한 typed assignment로 만들고, 성공한 resolution을 default filler 없는 stable `[values]`
TOML로 직렬화한다. Memory serializer와 explicit atomic destination writer는 하나의 구현을
공유하고 load/resolve/format 단계는 source user file을 수정하지 않는다.
R14는 이 exact project/user input과 resolved result를 sealed content-addressed directory로
출판하고 regular `selected` 하나로만 활성화한다. Verify는 sealed manifest에 열거된 path만
다시 hash하며 schema parse, resolver, directory scan 또는 project source 검사를 실행하지 않는다.
Configuration command는 아직 project graph를 열지 않고 usage error로 종료한다.

따라서 이 문서는 다음을 주장하지 않는다.

- schema 6 emitter, configuration CLI 또는 TUI가 이미 구현됨
- 기존 schema 5 configuration의 compatibility 또는 migration
- generic project의 build 성공이 Confit에 의해 검증됨
- schema 6 release candidate가 완성됨

## 목표 사용자 흐름

Schema 6가 구현되면 configuration과 ordinary build는 명시적으로 분리된다.

```text
bmake menuconfig
bmake all
```

또는 비대화형으로 다음처럼 사용한다.

```text
bmake configure
bmake all
```

`all`은 configuration이 없거나 stale할 때 configure를 암묵 실행하지 않는다.
Project의 Makefile은 `confit verify`로 선택된 value artifact를 검증한 뒤 소비한다.
Confit은 그 Makefile이나 project source를 읽지 않는다.

## Bootstrap 경계

제품과 필수 C test binary의 목표 bootstrap dependency는 provisioned clang toolchain,
bmake, shell, 이미 빌드된 Confit, 그리고 clang으로 빌드한 Confit-owned C binary뿐이다.
Python, CMake, Ninja, ncurses, parser generator와 외부 schema processor는 mandatory
build 또는 runtime dependency가 아니다. 정확한 claim과 non-claim은 architecture
contract와 bootstrap contract에 기록한다. Build target은 이미 존재하는 object root를
요구하며 외부 `mkdir`, `rm`, source/test discovery를 실행하지 않는다. Destructive clean
target 대신 fresh pre-created object root를 사용한다.
