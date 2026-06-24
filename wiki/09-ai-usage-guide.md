# 09. AI 사용 가이드

이 문서는 AI 에이전트가 Confit을 다룰 때 지켜야 하는 규칙이다. 목표는 자동화가 실제 Parus/Delos source를
실수로 오염시키지 않고, 항상 검증 가능한 순서로 움직이게 만드는 것이다.

## 가장 중요한 원칙

AI는 Confit 작업에서 다음 경계를 지킨다.

```text
Confit 자체 수정: tools/confit/ 안에서만 수행
실제 Parus/Delos config 적용: 사용자가 명시적으로 요청한 별도 integration 작업에서만 수행
generated output: 반드시 --out 아래에 생성
runtime source: Confit 작업 중 수정하지 않음
hidden DB: 만들지 않음
network config fetch: 하지 않음
```

## AI가 먼저 읽어야 할 파일

작업 전에 목적에 맞는 문서를 읽는다.

일반 사용:

```text
tools/confit/wiki/README.md
tools/confit/wiki/02-cli-quickstart.md
tools/confit/docs/cli-contract.md
```

schema 작성:

```text
tools/confit/wiki/04-schema-guide.md
tools/confit/docs/toml-schema.md
tools/confit/docs/syntax-stability.md
```

generation:

```text
tools/confit/wiki/06-generation-guide.md
tools/confit/docs/generators.md
```

Parus/Delos 적용:

```text
tools/confit/wiki/11-parus-delos-adoption.md
tools/confit/docs/final-release-note.md
tools/confit/docs/cutover-dry-run.md
tools/confit/docs/rollback.md
```

## 안전한 기본 명령 순서

AI가 profile이나 schema 변경을 평가할 때는 다음 순서를 기본으로 한다.

```sh
confit doctor --project <project>
confit check --project <project> --profile <profile> --strict
confit resolve --project <project> --profile <profile> --format json
confit diff --project <project> --profile <profile> --base <base-profile>
confit gen --project <project> --profile <profile> --out <tmp-out> --artifact all --dry-run
```

compat가 있는 경우:

```sh
confit compat \
  --parus <parus-project> \
  --delos <delos-project> \
  --profile <profile> \
  --compat <compat-dir>
```

## AI가 해도 되는 일

- fixture project에서 schema/profile 예시를 만든다.
- `confit check`, `resolve`, `diff`, `explain`, `compat`, `gen --dry-run`을 실행한다.
- 명시적인 output directory에 generated artifact를 만든다.
- wiki, docs, manpage를 갱신한다.
- TUI scripted test를 실행한다.

## AI가 하면 안 되는 일

- 실제 Parus/Delos runtime source를 Confit 작업 중 수정한다.
- 사용자 승인 없이 실제 `config/` source tree를 adoption한다.
- generated output을 source `config/` 아래에 쓴다.
- `--force`를 기본값처럼 쓴다.
- compatibility failure를 무시하고 generated output만 만든다.
- schema edit warning을 우회한다.

## AI에게 줄 수 있는 좋은 요청 예시

좋은 요청:

```text
tools/confit fixture 안에서 Delos debug profile에 새 bool option 예시를 추가하고,
check/resolve/diff/gen --dry-run까지 검증해줘.
```

좋은 요청:

```text
realish Parus/Delos fixture에서 parus-delos-debug compat rule을 하나 추가하고,
성공/실패 fixture를 모두 검증해줘.
```

좋은 요청:

```text
실제 Delos config adoption 계획을 작성하되 아직 source는 수정하지 말고,
어떤 confit 명령을 CI에 넣을지 정리해줘.
```

위험한 요청:

```text
Confit generated 파일을 바로 Delos config 폴더에 넣어줘.
```

위험한 이유: generated output과 source config가 섞인다. 반드시 별도 `--out`과 integration review가 필요하다.

## AI 응답 checklist

AI는 Confit 작업 후 다음을 보고해야 한다.

```text
수정 범위
실행한 confit 명령
검증 결과
generated output 위치
남은 위험
실제 Parus/Delos source를 수정했는지 여부
```

작업이 docs-only라면:

```text
문서 경로
설치/manpage 검증 여부
git diff --check 결과
```
