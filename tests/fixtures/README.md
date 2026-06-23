# Confit Fixtures

이 디렉터리는 Confit test input project를 보관한다.

현재 규약:

- `schema/valid/basic/`: Delos 형태의 단일 project positive fixture.
- `schema/invalid/`: parse, schema, profile, target negative fixtures.
- `graph/`: dependency graph positive/negative fixtures.
- `compat/parus/`, `compat/delos/`: Parus/Delos 형태의 compatibility fixture project.
- `compat/rules/`: compatibility rule positive/negative fixtures.
- `realish/delos/`: 실제 Delos migration을 준비하기 위한 Delos schema mirror fixture.
- `realish/parus/`: 실제 Parus migration을 준비하기 위한 Parus schema mirror fixture.
- `tui/`: scripted TUI profile/schema editing fixtures.

실제 Parus/Delos `config/` tree를 직접 테스트 입력으로 쓰지 않는다. 필요한 입력은 이 디렉터리 아래에
작고 결정적인 fixture로 둔다.
