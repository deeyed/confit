# Confit active fixture corpus

이 디렉터리는 bmake canonical host suite가 실제로 읽는 작은 bounded input만 보관한다.

- `host/`: canonical path, file/read/list boundary.
- `toml/`: schema 버전과 독립된 TOML adapter positive/negative input.

Fixture는 source Parus/Delos tree, runtime execution 또는 hardware support를 mirror하지 않는다.
Config v5 unit은 독립 temporary repository를 만들어 role-root discovery, selection과 hostile
corpus를 검증한다. 과거 schema fixture나 converter는 active tree에 보존하지 않는다.
