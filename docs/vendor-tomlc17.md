---
doc_type: dependency-record
status: accepted
authority: normative
last_verified: 2026-08-09
---

# tomlc17 dependency boundary

`vendor/tomlc17`은 TOML syntax tree를 제공하는 vendored dependency다. Confit schema, import path,
expression, resolver, component catalog, output encoding과 security policy를 소유하지 않는다. Adapter는
vendor value를 bounded read-only view로 바꾸고 Confit-owned diagnostic/path policy를 적용한다.

Vendor update는 upstream revision, license, changed parser behavior와 schema v2 conformance/fuzz result를
같은 review unit에 기록해야 한다. Vendor parser success만으로 schema success 또는 safe output publication을
주장할 수 없다.
