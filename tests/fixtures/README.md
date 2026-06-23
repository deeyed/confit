# Confit Fixtures

이 디렉터리는 Confit test input project를 보관한다.

초기 규약:

- `delos/`: Delos 형태의 최소 fixture project.
- `parus/`: Parus 형태의 최소 fixture project.
- `invalid/`: parse, schema, graph, compatibility negative fixture.

실제 Parus/Delos `config/` tree를 직접 테스트 입력으로 쓰지 않는다. 필요한 입력은 이 디렉터리 아래에
작고 결정적인 fixture로 둔다.
