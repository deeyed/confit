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

## Realish Shadow 리허설

Confit 저장소에는 실제 Parus/Delos source를 복사하거나 수정하지 않는 migration
rehearsal fixture가 있다.

```text
tests/fixtures/realish/       # 동결한 V1 mirror
tests/fixtures/realish-v2/    # 명시적 V2 candidate
tests/golden/migration-v2/    # semantic shadow 분류
```

V2 candidate는 `migrate`의 output을 그대로 승인한 결과가 아니다. V1의 category,
dependency, `forces`, profile/target precedence를 V2 menu, constraint, write-domain
계약으로 명시적으로 검토해 작성한 fixture다. C integration test
`confit.integration.v2_migration_shadow`는 현재 선언된 profile/target selection
matrix에서 V1/V2 `resolve --format toml`의 effective value table이 같은지, 그리고
양쪽 artifact bundle이 생성되는지를 확인한다. 같은 test는 realish V1 mirror에
`confit migrate`를 실행해 automatic candidate의 report가 `candidate only`임과
source `project.toml` bytes가 변하지 않음을 함께 확인한다.

```sh
ctest --test-dir build --output-on-failure \
  -R '^confit\.integration\.v2_migration_shadow$'
```

shadow report의 분류는 다음과 같다.

| 분류 | 의미 |
|---|---|
| `same` | V1과 V2의 effective value table이 같다. |
| `mechanical` | V1 source label과 V2 provenance origin은 표현 방식만 다르다. |
| `intentional-semantic` | 검토된 V2 hard cut으로 값 또는 validation 결과가 의도적으로 달라진다. |
| `unresolved` | 실제 전환 전에 사람이 해결해야 하며 cutover를 막는다. |
| `artifact-abi` | 같은 effective values를 다른 artifact ABI로 serialize하므로 bytes를 직접 비교하지 않는다. |

V1 resolve ABI에는 V2처럼 requested assignment ledger가 없으므로 이 rehearsal은
requested state를 `unavailable-in-v1-abi`로 기록한다. 이는 parity라고 추측하지
않는 보수적 표기다. 실제 migration review에서는 V2 requested/provenance report를
별도로 검토해야 한다.

## 의미 결정 예시

realish candidate는 V1 정보를 V2에 단순 복사하는 것이 아니라 다음 결정을
명시적으로 드러낸다.

- `forces`: V2 constraint는 값을 강제로 쓰지 않는다. Delos DDC와 debug surface의
  관계는 fixture matrix에서 두 값이 함께 설정되어야 한다는 constraint로 검증한다.
  실제 source에서는 computed option, explicit profile assignment, choice 중 어느
  구조가 소유할지 별도 결정해야 한다.
- truthiness: V2 expression에서는 bool 관계에 `enabled(option)`을 쓰고 enum이나
  string 관계는 `option == "value"`처럼 typed comparison으로 쓴다. 0, 빈 문자열,
  enum name을 bool로 암묵 변환하지 않는다.
- writer ownership: rehearsal candidate는 V1 최종 merge 결과를 비교하기 위해
  migrated option을 profile writer로 보수적으로 둔다. V2 실제 source에서는 board,
  CPU, linker처럼 target이 소유할 값과 profile이 소유할 값을 재분류해야 한다.
- enum/choice: V1 `choices`는 V2 enum의 `values`로 기계 변환된다. Kconfig-style
  mutually-exclusive bool group이 필요하면 enum으로 흉내 내지 말고 V2 `choice`
  declaration을 별도로 작성한다.

따라서 report의 `unresolved = 0`은 이 fixture selection의 effective value table에
남은 차이가 없다는 뜻일 뿐, 실제 project의 최종 menu와 writer design을 자동으로
승인하는 뜻은 아니다.

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

V2 TOML 1.x syntax의 허용 범위와 vendored parser provenance는
[vendor-tomlc17.md](vendor-tomlc17.md)를 따른다. TOML parser는 syntax tree만
제공하며 import, profile precedence, expression, build selection 의미를 해석하지
않는다. filesystem, path, stdout/stderr 같은 hosted service는
[`src/host/`](../src/host/) 경계에만 두며 core/resolver가 직접 호출하지 않는다.

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
