---
doc_type: artifact-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# bmake adapter와 artifact ABI v3

Confit은 schema v2 resolution의 단일 semantic authority이며, ABI v3 bundle은 그
snapshot을 bmake consumer가 안전하게 읽도록 직렬화한 adapter다. bmake syntax 자체는
configuration 의미를 만들지 않는다.

## 입력·출력 경계

Confit은 source TOML, explicit component root와 typed override만 읽는다. Makefile,
C source, environment의 임의 값, 이전 output 또는 directory traversal은 input이 아니다.
`gen --artifact bundle`은 다음 완전 집합만 publish한다.

```text
generations/<digest>/
  config.h                  # target C/ASM scalar/closed feature adapter
  config.selection.json     # canonical semantic snapshot
  config.report.json        # deterministic diagnostic report
  config.inputs.json        # complete input provenance
  config.mk                 # bundle identity와 include entry
  config.values.mk          # safe scalar/list values
  components.mk             # ordered component IDs와 local manifest/Makefile path
  component.catalog.json    # typed catalog/selected closure
  config.bundle.json        # ABI/version and all published artifact digests
selected                    # complete generation 뒤에만 바뀌는 discovery alias
```

`config.mk`, `config.values.mk`, `components.mk`에는 assignment, include와 encoded data만
있을 수 있다. rule, recipe, conditional, shell expansion 또는 compiler flag는 허용하지
않는다. Component Makefile의 restricted grammar 검사는 Parus가 소유하며 Confit은 이를
평가하지 않는다.

## publication

Generator는 temporary directory에 모든 artifact를 쓰고 canonical read-back, digest/size,
cross-artifact identity를 검증한다. 성공한 immutable generation만 exact digest 이름으로
publish하고 나서 `selected`를 갱신한다. 어떤 오류도 이전 selected generation을 mutate하거나
partial artifact를 public path에 노출하지 않는다.

Consumer는 selected를 읽은 직후 exact digest directory를 고정하고 `config.bundle.json`의
ABI version, expected profile/target, file list와 SHA-256/size를 검증한다. missing file,
extra unlisted file, unknown required field, digest mismatch 및 selection disagreement는
fallback 없이 failure다.

## cache와 identity

Bundle digest는 canonical input provenance와 semantic snapshot에 domain-separated strong
hash를 적용해 만든다. Host compiler, bmake implementation, local component Makefile은
consumer action provenance이지 config bundle identity가 아니다. 따라서 새 configure는 새
generation을 publish할 수 있어도 이미 configured child가 참조하는 generation은 바꾸지 않는다.
