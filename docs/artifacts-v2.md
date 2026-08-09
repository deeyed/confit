---
doc_type: artifact-contract
status: superseded
authority: historical
last_verified: 2026-08-09
supersedes: none
---

# 이전 v2 serializer 표면

이 파일은 경로 안정성을 위한 historical tombstone이다. partial serializer와 backend-specific
artifact surface는 삭제되었고 normal generation에 존재하지 않는다. 현재 normative artifact
계약은 [bmake-artifact-v4.md](bmake-artifact-v4.md)다.

Consumer가 이 문서에 나온 과거 filename이나 artifact identity를 요구하면 migration adapter를
제공하지 않고 unsupported error로 거부해야 한다.
