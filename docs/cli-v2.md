---
doc_type: cli-contract
authority: normative
---

# Confit V2 CLI 동작

이 문서는 `schema_version = 2` project에 적용되는 Confit command 계약이다.
명령 이름과 exit code는 [cli-contract.md](cli-contract.md)를 따른다. V1과 V2는
같은 top-level command를 공유하지만, source가 선언한 schema version으로만
dispatch한다.

## Version Dispatch

```sh
confit check --project path/to/project
```

`config/project.toml`의 `[project].schema_version`이 `2`이면 V2
load -> link -> compile -> snapshot resolve pipeline을 사용한다. `1`이면 기존 V1
pipeline을 그대로 사용한다. `--schema-version`, `--legacy`, `--v1-fallback`처럼
command line이 source version을 바꾸는 option은 없다.

V2 project 안에 V1 import, profile, target을 넣거나 V1/V2 project를 `compat`에
섞는 것은 hard error다.

## 공통 입력과 diagnostic

V2 project를 처리하는 `check`, `resolve`, `gen`, `explain`, `list`, `graph`,
`diff`, `compat`는 `--project`, `--profile`, `--target`, `--set id=value`를
해석한다. profile과 target은 schema가 선언한 경우에만 선택한다. 따라서 default
assignment만 가진 V2 project는 `--profile` 없이 resolve할 수 있다.

`--set`은 in-memory user request이며 source TOML을 쓰지 않는다. option의
write-domain과 `user_override` 정책을 통과해야 한다.

```sh
confit resolve --project fixtures/delos-v2 \
  --set 'delos.target.cpu=cortex-m7' --format json
```

V2 CLI error에 `--diagnostic-format json`을 붙이면 stderr에 다음처럼
machine-readable error 하나만 출력한다.

```json
{
  "schema": "confit-diagnostic-v2",
  "status": "invalid-argument",
  "message": "...",
  "path": "...",
  "line": 0,
  "column": 0
}
```

## Check와 Resolve

```sh
confit check --project fixtures/delos-v2 --profile release
confit resolve --project fixtures/delos-v2 --profile release --format json
```

`check`는 parse, import, link, static type, ownership, expression,
choice/constraint, requested/effective snapshot resolve를 검증한다. 성공하면
`check ok`를 출력한다. `--strict`은 V2에서 제공되는 warning을 failure로
승격한다.

`resolve --format text|json|toml`은 option의 type, requested 존재 여부,
effective value와 origin을 출력한다. JSON의 schema는
`confit-resolved-v2`이다. output은 deterministic order이며 source TOML을
수정하지 않는다.

## Gen

```sh
confit gen --project fixtures/delos-v2 --profile release \
  --out build/generated/delos/release --artifact all
```

V2 generator는 immutable snapshot 하나에서 canonical full bundle을 만든다.
현재 V2 CLI에서 허용하는 artifact selector는 `--artifact all`뿐이다. 부분
artifact selector는 unsupported error로 거부하여 서로 다른 partial bundle이
생기지 않게 한다. `--dry-run`은 snapshot과 bundle serialization만 검증하고
file을 쓰지 않는다.

생성되는 파일은 `config.h`, `config.report.json`, `config.explain.txt`,
`config.graph.json`, `config.inputs.json`, `config.changes.json`,
`config.cmake`, `config/config.qsm`, `build_selection/build_selection.qsm`이다.
각 file은 write-if-changed atomic publish를 사용하며 timestamp나 absolute host
path를 넣지 않는다.

## Explain, List, Graph, Diff

```sh
confit explain --project fixtures/delos-v2 --profile release delos.debug.trace
confit list --project fixtures/delos-v2 --kind options --tag debug
confit graph --project fixtures/delos-v2 --format json
confit diff --project fixtures/delos-v2 --base base --profile release --format json
```

`explain`은 type, availability, visibility, value, effective origin, source
span, requested 상태를 보여준다. `list`는 options, categories, tags와 schema가
선언한 profile/target directory를 조회한다. options에는 category/tag/query
filter를 적용할 수 있다.

`graph --format json|dot`은 evaluation, visibility, choice, constraint edge를
kind와 함께 출력한다. `diff`는 `--base`와 `--profile` snapshot의 effective
values를 결정적으로 비교한다. JSON schema는 각각 `confit-graph-v2`와
`confit-diff-v2`이다.

## Compat

```sh
confit compat \
  --parus fixtures/parus-v2 \
  --delos fixtures/delos-v2 \
  --compat fixtures/parus-delos.toml \
  --format json
```

`compat`는 두 root가 모두 V2일 때만 V2 compatibility suite를 실행한다. 하나만
V2인 mixed input은 명시적인 schema error다. 결과는 text 또는
`confit-compat-report-v2` JSON이며, compatibility violation은 exit code `5`를
유지한다.

## V1 Candidate Migration

```sh
confit migrate --project path/to/v1-project --out /tmp/project-v2-candidate
```

`migrate`는 V1 source tree를 read-only로 읽고, 반드시 별도 `--out` root에
candidate만 쓴다. source root와 같은 output은 error다. 결과는 다음이다.

```text
<out>/config/project.toml
<out>/config/options.toml
<out>/migration-report.json
<out>/migration-inputs.json
```

자동 변환 대상은 V1 option type/default/range/enum candidate/prompt/help/tag
metadata다. V2 namespace는 V1 option id의 첫 segment에서 결정한다. dependency,
category menu, profile, target, `forces`, writer conflict처럼 의미 결정을 요구하는
요소는 candidate에 추측해 넣지 않고 `migration-report.json`의 TODO로 남긴다.
Candidate가 load된다고 실제 project migration이 승인된 것은 아니다. 전체
profile/target matrix와 generated artifact를 별도로 검토해야 한다.
