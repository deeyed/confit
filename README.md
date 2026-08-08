---
doc_type: tool-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# Confit

Confit은 Parus의 **host-side configuration resolver**다. 사람이 작성한 schema v2
TOML과 component manifest를 검증·해결하고, immutable selection snapshot을 sealed
artifact ABI v3 bundle로 publish한다. Confit은 kernel, firmware, runtime service 또는
build graph executor가 아니다.

## 고정 경계

- source project는 `schema_version = 2`만 허용한다. 다른 schema source, migration
  command와 compatibility dispatch는 없다.
- host build의 유일한 engine은 pinned BSD `bmake 20240909`다.
- `gen`은 `--artifact bundle`만 허용한다. partial artifact 또는 backend selector는
  fail-closed한다.
- Confit은 TOML과 component identity를 해석하지만 Makefile을 평가하거나 source를
  scan하지 않는다. bmake는 sealed `components.mk`와 scalar adapter만 소비한다.
- generated file은 caller가 지정한 output root 아래에서만 생성한다. source tree와
  sibling project를 변경하지 않는다.

## 빠른 사용

Parus checkout에서는 root가 제공하는 pinned host tool을 사용한다.

```sh
build/host/Darwin-arm64/bin/bmake -r -C tools/confit -f Makefile \
  CONFIT_OBJROOT=/private/tmp/confit-build check

/private/tmp/confit-build/bin/confit check \
  --project tests/fixtures/schema-v2/valid

/private/tmp/confit-build/bin/confit gen \
  --project tests/fixtures/schema-v2/valid \
  --out /private/tmp/confit-generation --artifact bundle
```

Standalone clone은 동일 release의 pinned `bmake 20240909`를 `PATH`에서 제공하거나
`CONFIT_BMAKE`로 명시해야 한다. Homebrew의 임의 버전 bmake는 canonical input이
아니다.

## sealed bundle

`gen`은 완전한 generation을 임시 위치에서 검증한 뒤에만 publish한다.

```text
<out>/
  generations/<bundle-digest>/
    config.h
    config.selection.json
    config.report.json
    config.inputs.json
    config.mk
    config.values.mk
    components.mk
    component.catalog.json
    config.bundle.json
  selected -> generations/<bundle-digest>
```

`selected`는 편의 discovery alias일 뿐이다. configured child는 exact digest directory,
ABI version, profile/target과 manifest digest를 다시 확인해야 한다. 누락, unknown
required field, extra unlisted artifact 또는 digest mismatch는 fallback 없이 실패한다.

## 문서

- [bmake-artifact-v3.md](docs/bmake-artifact-v3.md): bmake adapter와 sealed bundle
  계약.
- [schema-v2.md](docs/schema-v2.md): source schema와 bounded input 규칙.
- [resolution-v2.md](docs/resolution-v2.md): immutable resolution pipeline.
- [architecture-v2.md](docs/architecture-v2.md): host/process/module authority.
- [cli-v2.md](docs/cli-v2.md): supported command surface와 failure rule.
- [local-build-and-test.md](docs/local-build-and-test.md): local build/test gate.

Parus integration의 normative authority는
`docs/contracts/config/config-system-contract.md`와
`docs/contracts/config/generated-config-abi.md`다. 이 repository의 문서는 그
contract를 약화하거나 별도 backend를 다시 도입하지 않는다.

## 비주장

Confit `check` 또는 `gen` 성공은 component가 compile되었거나, QEMU에서 실행되었거나,
physical hardware에서 동작한다는 증거가 아니다. 각각 Parus build, QEMU 및 hardware
evidence lane이 별도로 증명한다.
