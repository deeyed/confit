# 02. CLI 빠른 실습

이 문서는 Confit을 처음 실행해 보는 사람을 위한 walkthrough다. 실제 Parus/Delos source tree가 아니라
`tools/confit/tests/fixtures/` 아래 fixture project를 사용한다.

설치 전이면 build binary를 변수로 잡는다.

```sh
CONFIT_BIN=${TMPDIR:-/tmp}/confit-build/confit
```

설치 후라면 다음처럼 잡아도 된다.

```sh
CONFIT_BIN=confit
```

## 1. 도구 상태 확인

```sh
$CONFIT_BIN doctor
```

예상:

```text
doctor ok
```

project까지 함께 확인하려면:

```sh
$CONFIT_BIN doctor --project tools/confit/tests/fixtures/realish/delos
```

## 2. Delos fixture profile 검사

```sh
$CONFIT_BIN check \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh
```

예상:

```text
check ok
```

`check`는 TOML parse, schema validation, dependency graph validation, profile resolve를 한 번에 수행한다.
가장 먼저 돌려야 하는 안전한 명령이다.

## 3. 최종 resolved value 보기

```sh
$CONFIT_BIN resolve \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --format json
```

출력은 `confit-resolved-v1` JSON이다.

```json
{
  "schema": "confit-resolved-v1",
  "values": [
    {"id": "delos.debug.dsh", "value": true, "source": "profiles/sim-dsh.toml"}
  ]
}
```

값마다 `source`가 붙는다. 이 source가 나중에 debug와 review에서 중요하다.

## 4. 특정 option이 왜 그 값인지 설명하기

```sh
$CONFIT_BIN explain \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  delos.debug.dsh
```

`explain`은 현재 값, 값의 출처, dependency나 force/recommend trace를 사람이 읽기 쉽게 보여준다.

## 5. 두 profile 비교하기

```sh
$CONFIT_BIN diff \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --base debug
```

JSON으로 받고 싶으면:

```sh
$CONFIT_BIN diff \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --base debug \
  --format json
```

profile 변경 review에서는 `diff`가 유용하다. `check`는 “유효한가”를 답하고, `diff`는 “무엇이 바뀌는가”를
답한다.

## 6. option 목록 조회하기

debug category만 본다.

```sh
$CONFIT_BIN list \
  --project tools/confit/tests/fixtures/realish/delos \
  --kind options \
  --category debug
```

tag로 볼 수도 있다.

```sh
$CONFIT_BIN list \
  --project tools/confit/tests/fixtures/realish/delos \
  --kind options \
  --tag generated
```

## 7. dependency graph 출력하기

JSON:

```sh
$CONFIT_BIN graph \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --format json
```

DOT:

```sh
$CONFIT_BIN graph \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --format dot
```

DOT output은 graphviz 같은 도구로 시각화할 수 있다.

## 8. generated artifact 만들기

오타를 피하기 위해 output directory를 변수로 잡고, 그 directory만 삭제한다.

```sh
OUT=/tmp/confit-quickstart-delos
rm -rf "$OUT"

$CONFIT_BIN gen \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --out "$OUT" \
  --artifact all
```

생성 파일:

```sh
find "$OUT" -maxdepth 1 -type f | sort
```

예상:

```text
config.cmake
config.explain.txt
config.graph.json
config.h
config.inputs.json
config.qst
config.report.json
```

## 9. Parus/Delos compatibility 검사

```sh
$CONFIT_BIN compat \
  --parus tools/confit/tests/fixtures/realish/parus \
  --delos tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/realish/compat
```

예상:

```text
compat ok
```

## 10. TUI 열기

profile editor:

```sh
$CONFIT_BIN tui \
  --project tools/confit/tests/fixtures/tui/profile-editor \
  --profile edit
```

schema editor:

```sh
$CONFIT_BIN tui \
  --project tools/confit/tests/fixtures/tui/schema-editor \
  --schema-edit
```

schema editor는 위험한 모드이므로 진입 경고가 뜬다. 실제 project에서는 schema 변경을 code review 대상으로
취급해야 한다.
