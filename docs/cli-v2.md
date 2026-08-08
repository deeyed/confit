---
doc_type: cli-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# schema v2 CLI

Confit CLI는 read-only inspection 또는 sealed generation만 수행한다. 지원 command는
`check`, `resolve`, `gen`, `explain`, `list`, `graph`, `diff`, `component`, `compat`,
`doctor`, `help`, `--version`이다.

모든 project input은 `[project].schema_version = 2`여야 한다. source version 선택 option,
migration mode 및 alternate backend selector는 없다. 지원하지 않는 command, version 또는
artifact request는 nonzero status와 deterministic diagnostic으로 실패한다.

`gen`은 absolute `--out`과 `--artifact bundle`을 요구한다. 성공 시에만
[bmake-artifact-v3.md](bmake-artifact-v3.md)의 generation을 publish한다. `--dry-run`은
snapshot과 serialization 검증만 수행하며 public output을 만들지 않는다.

`component check|list|explain`은 catalog/closure diagnostic interface다. component Makefile을
실행하거나 source list를 discovery하지 않는다. `compat`는 동일 schema v2 snapshot 사이의
read-only assertion이며 project source를 변경하지 않는다.
