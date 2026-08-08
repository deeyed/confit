---
doc_type: language-spec
status: accepted
authority: normative
last_verified: 2026-08-09
---

# schema v2 source

Confit project는 canonical root의 `config/project.toml`에서 시작하며 `[project].schema_version = 2`를
명시한다. Project, option, profile, target, import와 constraint field는 schema-owned allowlist를 따른다.
Unknown field, duplicate canonical path, absolute/parent path, symlink escape, case collision, malformed UTF-8
및 bounded input limit 초과는 fail-closed한다.

Profile/target은 typed `values`와 bounded component selection request만 표현한다. source list, compiler
flag, backend label, generated artifact path 또는 physical device attach rule은 source schema가 아니다.
Component 의미는 explicit `component.toml`이 소유하고 source shape는 local Makefile이 소유한다.

Component ID는 lower-case dot-separated atom이다. Required capability의 missing/ambiguous provider는 error다.
Optional capability absence는 success일 수 있지만 selected source, fallback component나 driver policy를
암시하지 않는다.
