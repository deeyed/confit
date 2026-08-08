---
doc_type: compatibility-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# schema v2 cross-project compatibility

Compatibility suite는 이미 independently resolved 된 schema v2 snapshot에 assertion을 적용한다.
project 값, profile, target, source file 또는 generation을 변경하지 않는다.

각 `ConfitV2CompatProject`는 alias, immutable snapshot, schema version 2와 artifact ABI v3 identity를
제공해야 한다. alias, source root/hash, snapshot hash 또는 artifact identity mismatch는 report 없이
schema error다. Constraint false는 causal trace를 담은 compatibility report와 failure status를 만든다.

Compatibility source는 `[compat]`, `[projects]`, `[[constraint]]`만 허용한다. constraint에는 stable
ID, optional `when`, 정확히 하나의 `require` 또는 `forbid`, message가 필요하다. Expression은 typed
v2 language를 사용하며 project alias와 option reference는 load 전에 resolve될 수 있어야 한다.

이 기능은 component selection, driver attach, runtime policy나 source migration authority가 아니다.
