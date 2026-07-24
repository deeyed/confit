---
doc_type: migration-contract
authority: normative
---

# Confit V1에서 V2로 Migration

V2 loader에는 V1 compatibility layer가 없다. migration은 V1 source를 자동으로
덮어쓰는 기능이 아니라, 검토 가능한 V2 candidate를 별도 directory에 만드는
offline workflow다.

## 안전 경계

- V1과 V2 source file을 하나의 project에 섞지 않는다.
- `confit migrate`는 source project를 write하지 않는다.
- `--out`은 source root와 달라야 한다.
- 실제 Parus/Delos source 수정은 해당 project의 별도 승인과 review가 필요하다.
- Candidate를 생성했다고 semantic parity나 build graph parity가 자동 보장되지는
  않는다.

## Command

```sh
confit migrate \
  --project path/to/v1-project \
  --out /tmp/project-v2-candidate
```

Candidate root에는 `config/project.toml`, `config/options.toml`,
`migration-report.json`, `migration-inputs.json`이 생성된다. 같은 command를
같은 input과 output으로 반복해도 바뀐 bytes가 없으면 atomic write를 생략한다.

`migration-report.json`은 source schema version, option count, dependency,
category, profile, target, force, writer conflict TODO count를 기록한다.
`migration-inputs.json`은 source root와 manual semantic comparison이 필요한
사실을 기록한다.

## 자동 변환

다음 V1 option 정보는 V2 candidate의 explicit option declaration으로 옮긴다.

| V1 source | V2 candidate |
|---|---|
| bool/int/uint/hex/float/string/enum/path | 동일 type과 default |
| numeric range | `range = { min, max }` |
| enum `choices` | `values` |
| prompt/help/tags | 동일 metadata |
| owner/since/stability | 존재하면 그대로, 없으면 review 표시 metadata |

Candidate option은 자동으로 `write_domain = "profile"`을 사용한다. 이것은
실제 ownership 결론이 아니라 V2 schema가 load될 수 있도록 만든 temporary
candidate policy다. migration report의 TODO를 검토해 schema, target, profile,
computed domain으로 재분류해야 한다.

## 수동 결정 항목

다음은 자동으로 V2 의미로 추측하지 않는다.

- `requires`, `conflicts`, `recommends`, `forces`, `visible_if`
- V1 category에서 V2 menu tree로의 구조화
- profile inheritance와 target selection
- profile/target이 같은 option을 쓰는 writer conflict
- non-bool truthiness에 의존하는 dependency
- deprecated id, alias, generated build-selection mapping

이 항목은 V2 expression, constraint, choice, menu, write-domain 계약에 맞춰
명시적으로 설계해야 한다. `forces`는 computed option, explicit assignment,
constraint 또는 suggestion 중 한 가지로 의도를 재표현한다.

## 검증과 Cutover

Candidate를 만든 뒤에는 실제 profile/target matrix마다 다음을 비교한다.

```text
effective value와 provenance
config.h
config.report.json
config.cmake
config/config.qsm
build selection module
constraint/graph 결과
```

차이는 `identical`, `provenance-only`, `intentional-semantic`,
`unexpected-semantic`, `artifact-abi`로 분류한다. `unexpected-semantic`이
남아 있으면 cutover하지 않는다. V2로 전환할 때는 project, import, profile,
target을 모두 V2 문법으로 바꾸고 source-control rollback point를 남긴다.
