# V2 Migration Shadow 리허설

이 기록은 실제 Parus/Delos source tree를 변경하지 않는 fixture 검증 절차다.
정본 migration 규칙은 `docs/migration-v1-v2.md`를 따른다.

## 대상

- V1 baseline: `tests/fixtures/realish/delos`,
  `tests/fixtures/realish/parus`
- V2 candidate: `tests/fixtures/realish-v2/delos`,
  `tests/fixtures/realish-v2/parus`
- golden 분류: `tests/golden/migration-v2/realish-shadow.json`

## 실행

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure \
  -R '^confit\.integration\.v2_migration_shadow$'
```

이 integration test는 다음 V1/V2 selection 쌍을 확인한다.

```text
delos/debug/default
delos/release/default
delos/sim-dsh/sim-dsh
delos/parus-delos-debug/sim-dsh
delos/parus-delos-mismatch/qemu-mps2-an500
parus/bringup/default
parus/qemu-aarch64/qemu-virt-aarch64
parus/rpi5-direct-dtb/rpi5-direct-dtb
parus/parus-delos-debug/qemu-virt-aarch64
parus/parus-delos-mismatch/qemu-virt-aarch64
```

각 쌍마다 `resolve --format toml`의 `[values]` table을 비교하고, 양쪽에서
`config.h`, `config.report.json`, `config.cmake`, `config/config.qsm` bundle이
생성되는지 확인한다. 또한 V1 mirror에 `confit migrate`를 실행해 automatic
candidate report가 `candidate only`임과 source `project.toml` bytes가 변하지
않는지도 확인한다.

## 판정 경계

- V1에는 requested assignment ledger가 없으므로 requested state는
  `unavailable-in-v1-abi`로 기록한다.
- effective value table 일치는 `same`이다.
- V1 source label과 V2 provenance origin의 차이는 `mechanical`이다.
- artifact는 schema/resolver ABI가 다르므로 byte diff 대신 `artifact-abi`로
  분류한다.
- `unresolved`가 하나라도 생기면 실제 project cutover를 진행하지 않는다.

이 결과는 fixture parity만 뜻한다. 실제 Parus/Delos configuration, build graph,
submodule pointer, runtime source에는 아무 권한도 부여하지 않는다.
