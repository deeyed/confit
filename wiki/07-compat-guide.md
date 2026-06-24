# 07. 호환성 검사 가이드

`confit compat`은 여러 project의 resolved config가 서로 맞는지 검사한다. Parus/Delos처럼 서로 독립된
source tree가 build-time 계약을 공유해야 할 때 필요하다.

## 기본 명령

```sh
confit compat \
  --parus tools/confit/tests/fixtures/realish/parus \
  --delos tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/realish/compat
```

성공:

```text
compat ok
```

JSON report:

```sh
confit compat \
  --parus tools/confit/tests/fixtures/realish/parus \
  --delos tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/realish/compat \
  --format json
```

## 왜 compat가 필요한가

한 프로젝트 안에서는 `requires`와 `conflicts`만으로도 많은 것을 막을 수 있다. 하지만 Parus와 Delos는
서로 다른 project다.

예를 들어:

```text
Delos profile에서 DSH debug shell을 켰다.
그러면 Parus 쪽 test pairing이나 executor 설정도 맞아야 한다.
```

이런 규칙은 Delos option schema만으로는 완전히 표현하기 어렵다. 그래서 compat rule이 따로 필요하다.

## compat rule 예시

```toml
[[assert]]
id = "debug-shell-requires-parus-pairing"
when = { project = "delos", option = "delos.debug.dsh", equals = true }
requires = [
  { project = "parus", option = "parus.test.qemu_pairing", equals = true }
]
message = "Delos DSH debug shell requires Parus QEMU pairing support."
```

의미:

```text
delos.debug.dsh == true 이면
parus.test.qemu_pairing == true 여야 한다.
```

## forbids 예시

```toml
[[assert]]
id = "release-must-not-enable-debug-shell"
when = { project = "delos", option = "delos.profile.release", equals = true }
forbids = [
  { project = "delos", option = "delos.debug.dsh", equals = true }
]
message = "Release profile must not expose DSH debug shell."
```

의미:

```text
release profile이면 delos.debug.dsh == true 조합을 금지한다.
```

## compat 실패를 다루는 방법

1. 먼저 `compat` error message를 읽는다.
2. 문제가 된 project/option을 각각 `explain`한다.
3. `diff`로 성공 profile과 실패 profile의 차이를 본다.
4. 필요한 경우 profile TOML을 수정한다.
5. 다시 `check`, `compat`, `gen --dry-run` 순서로 검증한다.

예시:

```sh
confit explain \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  delos.debug.dsh

confit explain \
  --project tools/confit/tests/fixtures/realish/parus \
  --profile parus-delos-debug \
  parus.test.qemu_pairing
```

## compat rule 작성 원칙

좋은 rule:

- id가 명확하다.
- message가 사용자가 바로 행동할 수 있게 설명한다.
- `when` 조건이 너무 넓지 않다.
- `requires`와 `forbids`가 실제 cross-project 계약을 표현한다.

피해야 할 rule:

- 한 rule에 너무 많은 의미를 넣는다.
- message가 “invalid config”처럼 막연하다.
- project 이름과 option id가 생략되거나 모호하다.
- 실제 build graph 문제를 compat rule로 억지로 숨긴다.
