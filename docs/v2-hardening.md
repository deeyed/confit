---
doc_type: security-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# schema v2 hardening

Confit은 untrusted configuration input을 host build boundary에서 처리한다. 따라서 path traversal,
symlink escape, duplicate canonical input, oversized file/count, import/dependency cycle, pathological expression,
duplicate component ID, ambiguous provider와 output collision은 partial semantic object를 반환하지 않고
실패해야 한다.

Generator는 output root가 source root와 겹치지 않는지 확인하고, temporary staging에서 complete artifact
set을 digest/size로 verify한 뒤 atomic publish한다. Existing digest directory가 다른 bytes를 가지면 collision
failure다. Consumer는 manifest에 없는 file을 accept하지 않는다.

Fuzz/unit test는 parser와 expression boundary를, integration test는 publication/old selector rejection을
검증한다. 이 coverage는 target kernel, DMA, IOMMU 또는 runtime isolation의 보안 증명이 아니다.
