---
doc_type: cli-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# schema v2 CLI

Confit CLI는 read-only inspection 또는 sealed generation만 수행한다. 지원 command는
`check`, `resolve`, `gen`, `explain`, `list`, `graph`, `diff`, `component`,
`doctor`, `help`, `--version`이다.

모든 project input은 `[project].schema_version = 2`여야 한다. source version 선택 option,
migration mode 및 alternate backend selector는 없다. 지원하지 않는 command, version 또는
artifact request는 nonzero status와 deterministic diagnostic으로 실패한다.

`gen`은 absolute `--out`과 `--artifact bundle`을 요구한다. 성공 시에만
[bmake-artifact-v4.md](bmake-artifact-v4.md)의 generation을 publish한다. `--dry-run`은
snapshot과 serialization 검증만 수행하며 public output을 만들지 않는다.

`component check|list|explain|why|deps|rdeps|providers`는 catalog/closure diagnostic
interface다. `why`는 immutable selection reason을, `deps`와 `rdeps`는 direct/transitive
edge를, `providers`는 versioned feature/KAPI candidate와 선택 이유를 출력한다. Component,
nucleus와 test Makefile은 실행하지 않고 bounded API 3 source-owner data와 public include를
검증한다.

ID를 받는 action은 positional exact ID 하나를 사용한다. 예를 들어
`confit component why sys.kern.vm --project /source --profile release --target qemu-virt-aarch64`
형식이다. Unknown ID는 최대 다섯 개의 bounded 후보를 출력하지만 자동 교정하지 않는다.
