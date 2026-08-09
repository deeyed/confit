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

## component manifest v2

`component.toml`은 다음 네 배열까지 항상 명시하는 closed schema다. 빈 의미도 생략하지
않는다. Manifest에는 source, flag, command, selection predicate와 Makefile path를 넣지
않으며 sibling `Makefile`만 유효하다.

```toml
schema_version = 2

[component]
id = "sys.dev.audio.pci.cmi8738"
kind = "kernel_driver"

[requires]
components = []
kapi = ["parus.bus.pci.driver.v1"]

[provides]
capabilities = ["driver.audio.pci.cmi8738@1"]
kapi = []
```

`requires.components`는 private build/link edge이고 `requires.kapi`는 unique provider를
자동 선택하는 callable-contract edge다. Capability는 `@N`, KAPI는 `.vN`으로 양의
version을 명시한다. `[selection]`, `enabled_if`, `[dependencies]`, `makefile`과 field
alias는 없다. Test kind만 owner, lane, timeout과 evidence class가 닫힌 `[test]` table을
추가한다.

Catalog는 component 512개, 전체 edge 4096개, depth 32, list별 128개, manifest 128 KiB,
atom 127 bytes, path 1024 bytes로 제한한다. Duplicate ID/provider, missing provider,
self-edge, cycle, symlink/path escape와 limit 초과는 partial catalog 없이 실패한다.
Selection은 root/private/KAPI/capability reason과 manifest source 위치를 immutable
artifact에 보존한다.
