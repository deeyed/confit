# V1 동결 Golden

이 디렉터리의 파일은 `tests/fixtures/v1-baseline/project`를 현재 v1 Confit으로
두 번 resolve/generate해 얻은 byte 기준선이다.

`confit.regression.v1_baseline`은 다음을 비교한다.

- `resolve --format json`, `resolve --format text`
- `explain delos.debug.ddc`, `graph`
- `config.h`, reports, `config.cmake`, `config/config.qsm`, compatibility
  `config.qst`
- 두 독립 output directory에서 생성한 artifact의 상호 byte 동일성

v2 구현 때문에 이 파일을 갱신해서는 안 된다. v1 의미를 의도적으로 변경해야만
별도 승인과 regression 검토를 거쳐 갱신할 수 있다.
