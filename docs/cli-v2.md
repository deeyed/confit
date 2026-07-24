---
doc_type: cli-contract
status: accepted-design
authority: normative
implementation_status: not-implemented
last_verified: 2026-07-24
---

# Confit V2 CLI 동작

이 문서는 기존 top-level CLI가 `schema_version = 2` project를 처리할 때 추가로
지켜야 할 동작을 정의한다. Command 이름과 공통 exit code는
[cli-contract.md](cli-contract.md)를 유지한다.

## Version Dispatch

```text
confit check --project <root>
```

CLI는 `project.toml`의 `schema_version`을 읽고 v1 또는 v2 pipeline으로
dispatch한다.

금지:

```text
--schema-version 2
--legacy
--v1-fallback
```

Source가 선언한 major version을 command line으로 바꾸지 않는다.

## Check

```text
confit check \
  --project <root> \
  --profile <name> \
  --target <name> \
  [--set <id>=<value>] \
  [--unset <id>] \
  [--strict]
```

V2 check는 parse/link/type/ownership/evaluation/availability/choice/constraint/output
encoding을 모두 검증한다. 기본 동작은 가능한 독립 오류를 deterministic order로
모은다.

`--strict`는 lint warning을 오류로 승격하지만 suggestion 미적용을 오류로 만들지
않는다.

## Set과 Unset

`--set` value는 option type으로 parse한다. String/enum/path는 shell quoting과
상관없이 Confit value parser가 정확히 하나의 typed value를 만들어야 한다.

`--unset`은 optional non-computed option에만 사용할 수 있다. Required,
schema-domain, computed option은 unset할 수 없다.

Option의 `user_override`가 false면 두 option 모두 ownership error다.

## Resolve

```text
confit resolve \
  --project <root> \
  --profile <name> \
  --target <name> \
  --format text|json|toml
```

V2 JSON/TOML은 requested와 effective를 구분한다. `--effective-only` view를
추가할 수 있지만 정본 report에서 requested/provenance를 제거하지 않는다.

## Gen

```text
confit gen \
  --project <root> \
  --profile <name> \
  --target <name> \
  --out <dir> \
  --artifact header|reports|cmake|qstar|build-selection|changes|all
```

`changes`는 [artifacts-v2.md](artifacts-v2.md)의 semantic digest manifest다.
V2 artifact를 v1 schema id로 생성하는 option은 없다.

`--dry-run`은 file을 쓰지 않고 output plan, snapshot hash, 변경 예정 artifact를
출력한다.

## Explain

```text
confit explain ... <option-id>
confit explain ... --constraint <constraint-id>
confit explain ... --choice <choice-id>
```

Option explain은 requested chain, effective value, default/computed expression,
availability, choice, constraint causal slice를 출력한다.

## Graph

```text
confit graph ... --kind evaluation|choice|constraint|visibility|provenance
```

모든 graph를 하나의 dependency edge로 평탄화하지 않는다. JSON/DOT output은
graph kind와 schema id를 포함한다.

## Diff

V2 diff는 단순 text value뿐 아니라 다음 class를 제공한다.

```text
requested
effective
availability
choice
constraint
provenance
build-output
```

기본 diff는 semantic value와 build output 차이를 먼저 보여주고 provenance-only
변화는 별도 section에 둔다.

## Compat

V2 compatibility CLI는 [compat-v2.md](compat-v2.md)의 alias별 project/profile/
target mapping을 사용한다. 기존 `--parus`, `--delos` shorthand는 v1 command
surface로 유지할 수 있지만 v2 정본은 project alias 일반형이다.

## Profile

Profile command는 write domain을 검사한다.

- profile-domain option만 profile TOML에 저장
- target-domain edit는 target source 또는 ephemeral user override
- computed/schema option edit 거부
- sparse `[values]`/`[unset]` 유지
- save 전에 full v2 resolve

## TUI

V2 TUI는 normal edit 요청을 incremental resolver에 전달한다.

- Row에는 effective value를 표시한다.
- Inspector는 requested/effective/provenance를 구분한다.
- Unavailable edit는 dialog에서 causal diagnostic을 보여준다.
- Suggestion은 사용자가 명시 수락할 때만 user request가 된다.
- Choice cardinality는 renderer가 아니라 resolver가 판정한다.
- Save는 full validation 성공 뒤에만 atomic하게 수행한다.

## Migrate

```text
confit migrate schema \
  --project <v1-root> \
  --to 2 \
  --out <candidate-root> \
  --report <migration-report.json>
```

Migration은 in-place가 아니며 v2 runtime compatibility가 아니다. 자세한 계약은
[migration-v1-v2.md](migration-v1-v2.md)를 따른다.

## Doctor

`confit doctor`는 다음을 출력한다.

```text
supported schema versions
v1/v2 resolver ABI
artifact ABI
source revision
platform/TUI lane
```

Project가 지정되면 installed binary가 해당 schema와 artifact ABI를 지원하는지
검사한다.
