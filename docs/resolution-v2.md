---
doc_type: semantics-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# schema v2 resolution

Resolution은 source bytes를 immutable effective snapshot으로 바꾸는 순서가 고정된 pipeline이다.

1. project/profile/target/import provenance를 canonical root 안에서 snapshot한다.
2. source schema, symbol, menu, choice, constraint와 write-domain을 link/type-check한다.
3. typed profile/target/user assignment를 ledger에 기록하고 duplicate/unauthorized writer를 거부한다.
4. requested value, effective value, availability, choice와 constraint를 deterministic order로 평가한다.
5. component root, required/optional capability request와 catalog closure를 resolve한다.
6. snapshot, report와 complete input provenance를 artifact generator에 넘긴다.

이 과정은 source TOML을 수정하지 않으며, missing required capability/ambiguous provider, cycle, path
escape, unknown field, invalid expression과 incomplete provenance를 fail-closed한다. Optional capability
absence는 snapshot에 explicit absence로 기록될 수 있으나 fallback source 또는 runtime attach를 뜻하지
않는다.
