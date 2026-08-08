---
doc_type: contributor-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# Confit 구현·문서 규칙

Confit C source는 host tool이지만 bounded parser와 provenance boundary를 다루므로 allocation,
path, size와 ownership failure를 명시적으로 처리한다. public header의 type, macro와 function은
한국어 Doxygen 주석으로 lifetime, ownership, failure를 먼저 설명한다.

새 feature는 schema v2 semantic layer, component catalog 또는 ABI v3 publication 중 정확히 하나의
authority를 가져야 한다. CLI나 bmake adapter에 semantic rule을 복제하지 않는다. Unbounded filesystem
scan, shell execution, environment-driven semantic input과 source tree write는 허용하지 않는다.

문서는 source schema, resolution, artifact, build/test 또는 historical tombstone 중 하나의 authority를
명시한다. release note와 local experiment는 normative contract를 변경하지 않는다. Unsupported surface는
문서에서 조용한 compatibility처럼 보이게 하지 말고 explicit rejection으로 적는다.
