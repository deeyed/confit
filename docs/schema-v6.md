---
document: confit-schema-v6-minimal-loader
status: implemented-r09
authority: config-v6-and-architecture-v6
schema_version: 6
---

# Confit v6 minimal structural loader

## 1. 구현 범위

R08은 R07이 명시적으로 읽어 소유한 input image만 사용하여 schema 6 문서의 구조를
검증한다. Loader는 project directory를 열거하거나 다른 파일을 추측하지 않으며, 이미
구성된 source graph의 byte image를 다시 열지 않는다. Project load가 성공하면 opaque
`ConfitSchemaProject`가 source graph와 typed presentation catalog를 함께 소유한다.
실패하면 partial project를 공개하지 않는다.

이 단계가 허용하는 entry key는 정확히 다음 셋이다.

```text
schema_version
mainmenu
source
```

Entry는 integer `schema_version = 6`, bounded one-line `mainmenu`, string array
`source`를 모두 가져야 한다. Source의 경로 안전성과 reachability는 R07이 먼저
검증하며, R08은 source field가 schema shape와 일치하는지도 다시 확인한다. Entry에
`config`, `menu`, metadata 또는 다른 table을 넣으면 unknown-field error다.

Fragment의 top-level key는 다음 둘뿐이다.

```text
menu
config
```

Fragment는 `[menu]` 하나, `[[config]]` 하나 이상, 또는 둘 모두를 가질 수 있다.
둘 다 없는 fragment는 error다. Menu가 없는 config-only fragment는 source graph에서
자신을 포함한 가장 가까운 parent menu에 붙는다. Menu와 config가 같은 fragment에
있으면 그 config는 그 fragment의 menu에 붙는다. Root mainmenu를 제외한 presentation
menu depth는 public limit 3을 넘을 수 없다.

Menu key는 정확히 `prompt`, `help`, `source`다. `prompt`와 `help`는 필수다. Prompt는
bounded one-line UTF-8 text이고 help는 bounded non-empty UTF-8 text다. Help만 tab과
line break를 포함할 수 있다. Terminal escape, NUL, DEL, C0/C1 control은 prompt와 help
모두에서 거부된다. `source`는 optional string array이며 실제 child membership은 R07이
소유한다.

Config declaration key는 정확히 다음 여덟 개다.

```text
symbol
type
prompt
help
default
depends_on
values
range
```

`symbol`, `type`, `prompt`, `help`는 필수다. Symbol은 generic public symbol grammar를
따르고 reachable graph 전체에서 유일해야 한다. Type은 `bool`, `int`, `hex`,
`string`, `enum`으로 닫혀 있고 R09 type contract에 따라 default, range, values의
적용 가능성을 검증한다. Prompt와 help의 text 정책은 menu와 같다. `depends_on`은
bounded single-line string으로 소유하고 layout/control byte를 거부하지만 expression
parsing은 R11에 맡긴다.

`default`는 scalar TOML node, `values`는 array node, `range`는 table node여야 한다.
R09는 이 node를 같은 input image에서 type-check한 뒤 generic catalog가 default, range와
enum domain을 owned typed value로 deep-copy하게 한다. 자세한 규칙은
`docs/types-v6.md`가 기록한다.

## 2. User document shape

명시적으로 전달된 project-root-relative `.toml` 파일만 user document로 읽는다.
검색, 기본 파일명 추측, environment lookup은 없다. 허용되는 top-level surface는
다음뿐이다.

```toml
schema_version = 6

[values]
OPTION = true
```

`schema_version`은 필수 integer 6이다. `[values]`는 optional table이며 생략하면 모든
symbol이 후속 resolver에서 default로 시작한다. Value key는 public symbol grammar를
따르고 value는 native TOML scalar여야 한다. R08은 user symbol을 project catalog와
연결하거나 type conversion을 하지 않는다. Unknown symbol, stale assignment, exact
type checking과 minimal `savedefconfig` serialization은 R13의 책임이다.

성공한 `ConfitUserDocument`는 input image와 symbol copy를 함께 소유한다. Value node와
declaration span은 그 input image를 빌려 보며 document destroy 뒤에는 사용할 수 없다.
실패 diagnostic의 path는 input owner를 해제하기 전에 diagnostic-owned bounded buffer로
stabilize된다.

## 3. Unknown-field와 legacy rejection

Loader는 허용 목록에 없는 key를 모든 계층에서 거부한다. Prefix 비교나 C string의
우연한 NUL 종료에 의존하지 않고 TOML adapter가 제공하는 decoded key byte length와
exact bytes를 비교한다. Vendored TOML parser가 duplicate key를 거부하므로 first-wins,
last-wins 또는 override 의미는 존재하지 않는다.

다음과 같은 이전 또는 consumer-specific field는 config declaration에서 unknown-field
error다.

```text
owner since stability tags menu_order placement allowed enabled_values
cardinality namespace target profile selection build source_file object
provider driver visible_if needs select imply choice rule assert inherit
extends override source_if conditional_source
```

이는 compatibility warning이 아니다. Parser는 alias, legacy fallback 또는 숨은 변환을
제공하지 않는다. User에게 owner나 lifecycle metadata를 채우게 하지 않고, false default
assignment를 요구하는 구조도 만들지 않는다.

## 4. Transaction과 ownership

`confit_schema_project_load`는 caller allocator를 하나의 ownership domain으로 사용한다.
Source graph, catalog, attachment table, copied text와 declaration record 중 어느 단계에서
allocation이 실패해도 published project는 null이고 그 시도에서 소유한 allocation은
모두 해제된다. Failing allocator test가 각 allocation point를 순서대로 실패시켜 cleanup
closure를 검사한다.

Catalog에는 fragment, menu와 검증이 끝난 typed config가 함께 들어간다.
`confit_schema_project_config_count()`와 `confit_catalog_config_count()`는 같은 declaration
set을 본다. R08의 raw candidate 배열은 type transition을 위한 임시 구조였으며 R09에서
제거되었다. Unknown type이나 잘못된 default는 catalog에 반쯤 게시되지 않는다.

Diagnostic의 file, line, column은 TOML adapter가 보존한 exact source span에서 온다.
Transactional failure cleanup이 source graph 또는 user document를 해제해야 할 때는
bounded relative path만 diagnostic-owned storage로 복사한다. File을 재개방하거나 path를
normalize하는 동작은 이 안정화 과정에 없다.

## 5. 검증 경계

R08 focused evidence는 다음을 포함한다.

- menu와 config가 함께 있는 fragment 및 config-only fragment attachment;
- entry, fragment, menu, config, user document의 unknown-field rejection;
- 모든 legacy config field rejection;
- missing required field와 잘못된 structural node shape;
- graph 전체 duplicate symbol rejection;
- presentation depth limit;
- duplicate TOML key rejection;
- prompt/help terminal-control rejection;
- declaration path lifetime;
- allocator failure cleanup;
- public C header compilation.

R07 source-graph test와 함께 실행하면 unreachable invalid TOML, C source, Makefile과
sibling file이 loader 결과에 관찰되지 않는다는 경계도 유지된다. Product object에는
schema loader가 추가한 direct file open, directory enumeration 또는 subprocess import가
없어야 한다.

R09 focused evidence는 omitted/explicit default, five-type native TOML identity, int64와
hex boundary, exact/one-over string·enum limits, range 원인별 diagnostic, enum domain,
excluded type와 wrong-field applicability를 추가한다. Typed boundary와 integer overflow는
TOML fuzz seed에도 포함된다. 상세 evidence boundary는 `docs/types-v6.md`에 기록한다.

## 6. Non-claims

R08 완료는 다음을 의미하지 않는다.

- dependency expression이 parse, type-check 또는 evaluate되었다;
- user value가 project symbol과 연결되거나 resolved value가 생성되었다;
- config file이 serialize되거나 snapshot, Make, C header, JSON이 생성되었다;
- CLI configuration command 또는 TUI가 project loader를 호출한다;
- project source, Makefile, object, compiler 또는 build graph가 검사되었다;
- 이전 schema input이나 snapshot이 호환된다.

이 loader는 configuration data의 닫힌 TOML 구조만 소유한다. Consumer source selection,
compile/link ordering과 ordinary build는 계속 consumer build system의 책임이며 Confit의
schema나 runtime capability가 아니다.
