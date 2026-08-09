---
doc_type: artifact-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# bmake adapter와 artifact ABI v4

Confit은 schema v2 resolution의 단일 semantic authority이며, ABI v4 bundle은 그
snapshot을 bmake consumer가 안전하게 읽도록 직렬화한 adapter다. bmake syntax 자체는
configuration 의미를 만들지 않는다.

## 입력·출력 경계

Confit은 source TOML, explicit component root, typed override와 sibling Makefile의 bounded
Build API v2 data만 읽는다. Makefile을 실행하거나 C contents를 scan하지 않으며,
environment의 임의 값, 이전 output 또는 directory traversal은 input이 아니다.
`gen --artifact bundle`은 다음 완전 집합만 publish한다.

```text
generations/<digest>/
  config.h                  # target C/ASM scalar/closed feature adapter
  config.selection.json     # canonical semantic snapshot
  config.reason.json        # component 선택 이유와 source span
  config.report.json        # deterministic diagnostic report
  config.inputs.json        # complete input provenance
  config.mk                 # bundle identity와 include entry
  config.values.mk          # safe scalar/list values
  components.mk             # ordered component IDs, sealed source와 public Build API mapping
  target.mk                 # 검증된 target/toolchain/image/package tuple
  tests.mk                  # bounded roots에서 발견한 complete test catalog와 selected-test subset
  component.catalog.json    # typed catalog/selected closure
  config.bundle.json        # ABI/version and all published artifact digests
selected -> generations/<bundle-digest>
                            # complete generation 뒤에만 atomic 교체되는 directory alias
```

`component.catalog.json`은 catalog schema v2로, 모든 available component와 dependency,
versioned KAPI/capability, test metadata, selected root 및 root/private/KAPI/capability
reason graph를 함께 싣는다. Reason은 source path와 line/column을 포함하며 consumer가
provider를 다시 추론하거나 first-match fallback을 수행하지 못하게 한다.

`tests.mk`는 중앙 test ID registry를 대체한다. `PARUS_TEST_IDS`는 bounded
`component_roots`에서 발견한 모든 `kind = "test"` component를 결정적으로 열거하고,
각 ID에 대해 owner, lane, evidence class, nonzero timeout, source directory와 sealed
source list를 낸다. `PARUS_SELECTED_TEST_IDS`는 production selection closure에 test가
침투했는지 fail-closed로 검사하기 위한 별도 subset이다. Test owner는 존재하는 non-test
component여야 하고 lane/evidence 조합은 closed compatibility table과 일치해야 한다.

`config.mk`, `config.values.mk`, `components.mk`, `target.mk`, `tests.mk`에는 assignment, include와 encoded data만
있을 수 있다. rule, recipe, conditional, shell expansion 또는 compiler flag는 허용하지
않는다. Confit은 Component Makefile을 Build API v2 data grammar로 검증하되 실행하지 않고,
configured bmake child는 원본 leaf Makefile을 다시 include하지 않는다.

## publication

Generator는 temporary directory에 모든 artifact를 쓰고 canonical read-back, digest/size,
cross-artifact identity를 검증한다. 성공한 immutable generation만 exact digest 이름으로
publish하고 나서 `selected` relative directory alias를 atomic 교체한다. 어떤 오류도 이전
selected generation을 mutate하거나 partial staging을 public path에 노출하지 않는다.

Consumer는 selected의 canonical target을 exact digest directory로 고정하고 `config.bundle.json`의
ABI version, expected profile/target, file list와 SHA-256/size를 검증한다. missing file,
extra unlisted file, unknown required field, digest mismatch 및 selection disagreement는
fallback 없이 failure다.

## cache와 identity

Bundle digest는 canonical input provenance와 semantic snapshot에 domain-separated strong
hash를 적용해 만든다. Host compiler와 bmake implementation은 consumer action
provenance다. Local Makefile의 semantic source/include mapping은 catalog artifact에 들어가며
주석·presentation text는 semantic identity가 아니다. 따라서 새 configure는 새
generation을 publish할 수 있어도 이미 configured child가 참조하는 generation은 바꾸지 않는다.
