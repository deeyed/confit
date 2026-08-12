---
doc_type: contract
status: accepted
authority: confit-config-v4
---

# Config v4

Config v4는 선택 가능한 기능의 의미와 availability만 소유한다. Mandatory nucleus는
Config.toml 없이 Bake Makefile에 존재한다. Local `constraints.all`은 prerequisite symbol의
AND이며 어떤 option도 자동 enable하지 않는다. 과거 `needs`는 unknown field로 거부한다.

Discovery root마다 `OWNERS.toml`이 trust-root metadata를 제공한다. Confit은 regular file,
canonical relative path, bounded count/size와 membership digest를 봉인한다. Profile과 selection은
명시적인 value만 선택하며 hidden select, first-match와 discovery-order override가 없다.

Preview는 immutable candidate를 만들고 selected generation을 바꾸지 않는다. Apply만 product
binding receipt를 검증한 뒤 selected record를 atomic publish한다. Cancel은 candidate를
publication authority로 만들지 않는다.

Generated `config.mk`, `config.h`, `selection.mk`, `target.mk`, provenance와 config seal은 data다.
Confit이 Bake Makefile이나 source/link graph를 생성하지 않는다.
