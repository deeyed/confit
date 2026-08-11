# Confit active fixture corpus

이 디렉터리는 bmake canonical host suite가 실제로 읽는 작은 bounded input만 보관한다.

- `host/`: canonical path, file/read/list boundary.
- `toml/`: schema 버전과 독립된 TOML adapter positive/negative input.
- `schema-v2/`, `schema-v2-link/`, `schema-v2-structure/`: loader, import,
  structural bound과 fail-closed corpus.
- `schema-v2-availability/`, `schema-v2-choice/`, `schema-v2-constraint-runtime/`,
  `schema-v2-evaluation/`, `schema-v2-ledger/`: immutable resolution semantics.

Fixture는 source Parus/Delos tree, runtime execution 또는 hardware support를 mirror하지 않는다.
기존 project fixture는 schema v2를 사용하며, Config v4 unit은 독립 temporary repository를
만들어 role-root discovery와 hostile corpus를 검증한다. Component manifest version은 project
schema와 별개인 `component.toml` document version이며, catalog parser가 명시적으로 검증한다.
