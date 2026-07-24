---
doc_type: reference-analysis
status: accepted
authority: informative
last_verified: 2026-07-24
---

# Kconfiglib 분석과 Confit V2 적용

이 문서는 Confit v2 설계를 위해 Python Kconfiglib 원본의 parser, model,
evaluator, menuconfig 흐름을 분석한 결과다. 외부 source를 Confit repository에
복사하지 않고 동작과 설계 판단만 기록한다.

## 분석 기준

```text
upstream: https://github.com/ulfalizer/Kconfiglib
temporary checkout: /tmp/confit-kconfiglib-v2-reference
tag: v14.1.0
revision: 061e71f7d78cb057762d88de088055361863deff
```

주요 파일:

```text
kconfiglib.py
menuconfig.py
testsuite.py
tests/K*
setup.py
```

Upstream selftest는 통과했다. Linux kernel source root가 필요한 C Kconfig 도구
대조 suite는 이 분석 환경에서 실행하지 않았다.

## Entry Point와 TUI

`setup.py:46`은 `menuconfig = menuconfig:_main` console entry point를
등록한다. `menuconfig.py:662`의 `_main()`은 `standard_kconfig()`로 model을
만들고 `menuconfig(kconf)`를 호출한다.

`menuconfig.py:666`의 `menuconfig()`는 `.config`를 load하고 dirty 상태를 만든
뒤 curses wrapper로 들어간다. 실제 event loop는 `menuconfig.py:816`의
`_menuconfig()`다.

중요한 경계는 TUI가 독자적인 dependency resolver가 아니라는 점이다.

- `menuconfig.py:1488` `_shown_nodes()`가 MenuNode와 visibility를 row로 만든다.
- `menuconfig.py:1567` `_change_node()`가 model의 assignable value를 조회하고
  `set_value()`를 호출한다.
- `menuconfig.py:1673` `_update_menu()`가 변경 뒤 visible row를 갱신한다.
- `menuconfig.py:2013` search는 전체 node index를 재사용한다.
- `menuconfig.py:2541` help view는 model의 value/dependency/source 정보를 읽는다.

Confit v2도 TUI가 `Compiled MenuTree + Resolved Snapshot + Explanation Graph`만
읽게 한다.

## Parse와 Finalize

`kconfiglib.py:1058`의 `Kconfig.__init__()` 흐름은 대략 다음 단계다.

```text
tokenize/block parse
-> MenuNode tree 작성
-> property/dependency 전파
-> implicit menu 구성
-> Symbol/Choice sanity 검사
-> reverse dependent index 작성
-> dependency cycle 검사
-> choice invalidation edge 작성
```

Confit v2는 parse 직후 resolution을 시작하지 않고 parse, link, type-check,
ownership check, graph compile, cycle check를 분리한다는 점을 채택한다.

Kconfig의 dependency 기반 implicit menu 구성은 채택하지 않는다. Confit menu는
표현 계층이고 dependency는 semantic graph이므로 명시 declaration로 분리한다.

## Symbol, Choice, MenuNode

Kconfiglib은 세 model을 분리한다.

- `Symbol`: configuration value와 dependency
- `Choice`: 선택 group과 mode
- `MenuNode`: prompt/help/source/menu 위치

`kconfiglib.py:3984` 부근에서 Symbol과 property/menu node 관계를 볼 수 있고,
`kconfiglib.py:5469`의 MenuNode는 `parent`, `list`, `next`로 tree를 구성한다.

Confit v2는 역할 분리를 채택하지만 동일 Symbol의 여러 semantic definition은
허용하지 않는다. 여러 위치 표시는 read-only MenuNode reference로 표현한다.

## Expression Representation

Kconfiglib은 expression을 tuple AST로 저장한다.

```text
A       -> Symbol(A)
!A      -> (NOT, A)
A && B  -> (AND, A, B)
A || B  -> (OR, A, B)
A = B   -> (EQUAL, A, B)
A < B   -> (LESS, A, B)
```

Parser는 `kconfiglib.py:3376`, representation 설명은 `kconfiglib.py:289`
부근에 있다.

Confit v2는 AST 방식을 채택하되 각 node에 static type과 source span을 넣는다.
Kconfiglib의 관계 연산은 숫자 변환 실패 시 string lexical 비교로 fallback할 수
있다(`kconfiglib.py:6024`). V2에서는 이 fallback을 금지한다.

## Tristate

Kconfiglib expression은 tristate lattice를 사용한다.

```text
AND = min
OR  = max
NOT = 2 - value
```

평가는 `kconfiglib.py:5988` 부근에 있다.

Confit v2는 tristate value 자체는 지원하지만 bool 문맥에 자동 투입하지 않는다.
`enabled()`, `builtin()`, `module()`로 의도를 명시한다. Bool과 tristate promotion도
하지 않는다.

## Requested와 Effective

Kconfiglib은 `user_value`와 최종 `str_value`/`tri_value`를 구분한다. 이 분리는
Confit v2가 채택한다.

다만 Kconfiglib은 symbol이 visible한 경우에만 user value를 유효하게 취급한다.
Bool/tristate 계산은 `kconfiglib.py:4428` 부근에서 visible user value, active
default, imply/select 하한을 조합한다.

Confit v2는 visibility를 presentation으로만 사용한다. Requested value가 semantic
availability를 위반하면 조용히 무효화하지 않고 오류와 provenance를 출력한다.

## Select와 Imply

Kconfiglib `select`는 reverse dependency 하한을 올리며 target의 direct dependency를
위반해도 강제값을 유지할 수 있다. 관련 계산은 `kconfiglib.py:4479` 부근이다.

V2에는 `select`와 `forces`를 넣지 않는다.

```text
hard condition -> named constraint
UI 권고       -> suggestion
파생 internal -> computed option
```

`imply`도 값을 변경하는 weak reverse dependency로 도입하지 않는다. Suggestion은
자동 resolution이 아니라 사용자가 수락할 수 있는 편집 후보만 제공한다.

## Range와 Invalid Assignment

Kconfiglib의 int/hex user value와 active range 계산은 `kconfiglib.py:4313`
부근에 있다. 일부 invalid assignment는 default fallback으로 이어지고 range 밖
default가 clamp될 수 있다.

Confit v2는 다음을 모두 hard error로 처리한다.

- parse되지 않는 숫자
- type mismatch
- range 밖 requested/default/computed value
- finite하지 않은 float
- overflow와 0 division

잘못된 값을 고친 것처럼 성공 snapshot을 만들지 않는다.

## Choice

Kconfiglib Choice model은 `kconfiglib.py:4992`, selection 계산은
`kconfiglib.py:5424` 부근에 있다.

Kconfiglib에서는 choice/member type 추론, 첫 visible member default, menu
구조의 member 판정 같은 암묵성이 존재한다. Confit v2는 다음을 모두 명시한다.

```text
member_type
members
cardinality
conditional default와 priority
availability
visibility
```

Menu adjacency에서 member를 추론하거나 first-visible fallback을 하지 않는다.

## Reverse Index와 Incremental Invalidation

Kconfiglib은 prompt/default/reverse dependency/range/direct dependency가 참조하는
symbol에 reverse dependent index를 만든다(`kconfiglib.py:3466`).

값이 바뀌면 affected dependent cache만 invalidate하며 아직 계산되지 않은 branch의
전파를 줄인다(`kconfiglib.py:4865`). Cycle 검사는 DFS 상태를 사용하고 전체
cycle path를 출력한다(`kconfiglib.py:6559`).

Confit v2가 채택할 사항:

- immutable compiled expression graph
- reverse dependent index
- option 단위 incremental invalidation
- full cycle path와 source span
- profile/target matrix에서 compiled project 재사용
- TUI search/menu index cache

Confit은 evaluation, visibility, constraint, provenance graph를 분리해 invalidation
범위를 더 명확히 한다.

## Load와 Write

Kconfiglib의 주요 흐름:

- `load_config()` (`kconfiglib.py:1149`): replace 또는 fragment merge
- `write_config()` (`kconfiglib.py:1503`): full config, unchanged write 회피
- `write_min_config()` (`kconfiglib.py:1658`): default와 같은 값을 생략
- `sync_deps()` (`kconfiglib.py:1732`): symbol별 dependency stamp 갱신

Confit v2는 다음 아이디어를 채택한다.

- full immutable snapshot과 sparse profile 분리
- write-if-changed
- option별 semantic change manifest
- 모든 generator가 같은 snapshot 사용
- input file/hash manifest

Filesystem 작업은 기존 `src/host/` boundary를 통해서만 수행한다.

## Warning과 Error

Kconfiglib은 syntax와 cycle은 error로 처리하지만 undefined symbol, 일부 type
불일치, invalid assignment, select dependency 위반 등을 warning으로 남기고
진행할 수 있다. 관련 sanity/warning code는 `kconfiglib.py:3726`,
`kconfiglib.py:3969` 부근에 있다.

Confit v2 hard error:

```text
unknown reference
duplicate semantic definition
type mismatch
invalid range/default
ownership violation
unavailable assignment
evaluation cycle
choice violation
failed constraint
invalid artifact encoding
```

Style, deep menu, unused declaration, suggestion만 warning/lint 영역에 둔다.

## 성능 참고

Kconfiglib upstream 설명은 x86 계열 약 14,000 symbol 규모를 다룬다
(`README.rst:468`). 주요 이유는 parse 후 model 재사용, lazy cache, reverse
invalidation, expression short circuit, search/source ordering cache다.

Confit v2는 C model의 arena/index AST와 immutable graph를 사용하고, 20,000
option/100,000 edge 이상을 release stress 기준으로 잡는다.

## 채택 결정

### 채택

- parse/link/finalize/validate 단계 분리
- Symbol/Choice/MenuNode 역할 분리
- expression AST
- requested/effective 분리
- 조건부 default
- assignability API
- reverse dependency index와 incremental invalidation
- first-class choice
- source location/include provenance
- full snapshot과 sparse source 분리
- write-if-changed
- TUI와 resolver 분리
- cycle path와 dependency explanation

### 더 엄격하게 변경

- expression static type check
- undefined reference hard error
- duplicate semantic definition 금지
- type 추론 금지
- invalid assignment/range hard error
- visibility와 availability 분리
- requested/effective/provenance 전체 보존
- write domain 명시
- computed graph DAG 강제
- named constraint와 failure trace
- choice cardinality/default 명시
- generator 단일 snapshot

### 채택하지 않음

- `select`/`forces`
- 값을 변경하는 `imply`
- visibility에 따른 user value silent drop
- range fallback/clamp
- bool/tristate 자동 promotion
- undefined symbol 암묵 생성
- shell/environment preprocessor
- dependency 기반 implicit menu
- 동일 Symbol의 다중 semantic definition
- first-visible choice default
- warning 후 semantic recovery
- TUI 내부 resolution 규칙
