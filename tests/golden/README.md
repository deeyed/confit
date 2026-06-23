# Confit Golden Outputs

이 디렉터리는 deterministic generated output의 기준 파일을 보관한다.

초기 규약:

- `config-h/`: generated `config.h` 기준 파일.
- `reports/`: `config.report.json`, `config.inputs.json` 기준 파일.
- `explain/`: 사람이 읽는 explanation text 기준 파일.
- `graph/`: `config.graph.json` 또는 DOT output 기준 파일.
- `realish/`: 실전 mirror fixture에서 생성한 config/report/build fragment 기준 파일.

Golden output에는 timestamp와 absolute path를 넣지 않는다.
