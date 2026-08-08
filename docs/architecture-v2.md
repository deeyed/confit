---
doc_type: architecture-contract
status: accepted
authority: normative
last_verified: 2026-08-09
---

# Confit schema v2 구조

Confit은 host process 안에서 bounded configuration input을 immutable resolution snapshot으로
바꾸는 도구다. kernel·firmware 또는 user runtime에는 parser, resolver와 component catalog
loader를 넣지 않는다.

```text
source TOML + component.toml
      | bounded parse/link/type check
      v
typed schema-v2 project -- snapshot resolution -- component closure
      |                                      |
      +-------------- canonical snapshot ----+
                         |
                         v
                 sealed artifact ABI v3
                         |
                         v
                configured bmake consumer
```

`src/schema/v2`, `src/model/v2`, `src/expression/v2`, `src/constraint/v2`,
`src/resolver/v2`는 semantic pipeline을 소유한다. `src/component`는 manifest catalog와
dependency/capability closure를 소유한다. `src/generator/v2`는 이미 결정된 snapshot만
직렬화한다. `src/cli`는 command-line을 parse하고 diagnostic을 출력할 뿐 semantic rule을
복제하지 않는다.

Host filesystem code는 canonical root, file count/size, path escape와 encoding boundary를
강제한다. Resolver와 expression code는 clock, environment, shell, terminal과 mutable global
state를 직접 읽지 않는다. 이 분리는 deterministic regeneration과 input provenance를 위한
것이며, runtime security boundary의 증명은 아니다.
