---
doc_type: schema-index
status: accepted
authority: normative
last_verified: 2026-07-24
---

# Confit TOML Schema

Confit TOML 문법은 `schema_version`별로 분리되어 있다. 이 문서는 version
선택용 index이며 개별 field 의미를 직접 정의하지 않는다.

## Version 선택

```toml
[project]
name = "example"
schema_version = 1
```

Project의 `schema_version`이 parser, model, resolver, artifact schema를
결정한다. CLI가 source version을 강제로 변경하지 않는다.

## Schema Version 1

현재 실사용 중인 v1 정본:

- [schema-v1.md](schema-v1.md): source field와 type
- [resolution-v1.md](resolution-v1.md): merge와 dependency 검증

V1 문법과 의미는 동결한다. 장래 v2 기능을 v1 문서 예제로 쓰지 않는다.

## Schema Version 2

새 typed semantics의 v2 정본:

- [schema-v2.md](schema-v2.md): project/option/menu/choice/profile/target/constraint
- [expression-v2.md](expression-v2.md): expression grammar와 static type
- [resolution-v2.md](resolution-v2.md): requested/effective resolution
- [architecture-v2.md](architecture-v2.md): 구현 module과 graph 경계
- [artifacts-v2.md](artifacts-v2.md): generated artifact ABI
- [compat-v2.md](compat-v2.md): cross-project constraint
- [cli-v2.md](cli-v2.md): CLI dispatch와 edit/migration surface

V2 loader는 v1 compatibility field를 읽지 않는다.

## 공통 Version 계약

[schema-versions.md](schema-versions.md)는 version dispatch, mixed-tree 금지,
artifact ABI, v1 freeze, v2 hard cut을 정의한다.

V1 project를 v2로 바꾸는 절차는
[migration-v1-v2.md](migration-v1-v2.md)를 따른다.
