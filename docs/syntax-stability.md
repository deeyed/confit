---
doc_type: stability-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# syntax와 ABI 안정성

Schema v2와 artifact ABI v4은 별도의 versioned boundaries다. Source schema change는 parser/linker/resolver
semantics를 바꾸고, ABI change는 sealed bundle/consumer conformance를 바꾼다. 어느 쪽도 CLI alias,
silent default 또는 prior generation fallback으로 숨기지 않는다.

새 required field, changed canonical encoding, ordering/atomicity/fault semantics change와 component catalog
meaning change는 explicit version negotiation과 conformance corpus를 필요로 한다. Unknown required field나
ABI version은 consumer가 fail-closed해야 한다. Historical documentation 또는 archived output은 supported
syntax를 뜻하지 않는다.
