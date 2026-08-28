# Confit active fixture corpus

이 디렉터리는 bmake canonical host suite가 실제로 읽는 작은 bounded input만 보관한다.

- `toml/`: schema 버전과 독립된 TOML adapter positive/negative input.

Fixture는 consumer source tree, runtime execution 또는 hardware support를 mirror하지 않는다.
과거 schema fixture나 converter는 active tree에 보존하지 않는다.
