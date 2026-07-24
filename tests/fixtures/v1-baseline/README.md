# V1 동결 입력 기준선

이 디렉터리는 `schema_version = 1`의 동작을 v2 구현 동안 보존하기 위한 입력
기준선이다.

- `project/`는 profile inheritance, target 선택, dependency graph, strict warning,
  generator를 함께 사용하는 완결 v1 project fixture다.
- `toml/`은 현재 v1 scanner가 허용하거나 거부하는 최소 syntax corpus다.
- `duplicate/`, `unknown-field/`, `missing-type/`, `invalid-id/`는 source path와
  핵심 diagnostic을 동결하는 schema negative corpus다.
- 이 입력은 v2 문법 실험용으로 수정하지 않는다. v1 의미의 의도적 변경이 필요하면
  별도 승인, 변경 이유, golden 재생성 근거가 필요하다.
- 실제 Delos/Parus source가 아니라 Confit test fixture만 포함한다.

`confit.regression.v1_baseline` C integration test가 이 project를 CLI로 읽고,
resolve/explain/graph/artifact 결과를 `tests/golden/v1-baseline/`과 byte 단위로
비교한다.
