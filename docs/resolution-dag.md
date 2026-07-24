---
doc_type: semantics-index
status: accepted
authority: normative
last_verified: 2026-07-24
---

# Confit Resolution

Confit resolver 의미론은 schema major version별로 분리된다.

## V1

[resolution-v1.md](resolution-v1.md)는 현재 구현의 다음 동작을 고정한다.

```text
default
-> base profile
-> target
-> selected profile
-> user override
-> requires/conflicts validation
```

V1 `forces`와 `recommends`는 값을 변경하지 않는다.

## V2

[resolution-v2.md](resolution-v2.md)는 다음 pipeline을 정의한다.

```text
requested assignment
-> typed conditional default
-> input effective value
-> computed DAG
-> availability
-> choice
-> named constraint
-> immutable snapshot
-> provenance graph
```

V2는 `select`/`forces`, truthiness, silent fallback, cross-domain
last-write-wins를 허용하지 않는다.

Expression graph와 source architecture는
[expression-v2.md](expression-v2.md),
[architecture-v2.md](architecture-v2.md)를 따른다.
