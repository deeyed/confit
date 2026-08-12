---
doc_type: contract
status: accepted
authority: confit-config-v5
---

# Config v5

Config v5는 선택 가능한 기능의 의미와 availability만 소유한다. Mandatory nucleus는
Config.toml 없이 Bake Makefile에 존재한다. Local `constraints.all`은 prerequisite symbol의
AND이며 어떤 option도 자동 enable하지 않는다. 과거 `needs`는 unknown field로 거부한다.
`[target]`, `[profile]`, `[selection]`, product/interface/provider table도 같은 방식으로
거부한다. External `ARCH`는 read-only catalog scope이고 KERNCONF는
`config/kernconf/<ARCH>/<name>.toml`에서만 읽는다.

Discovery root의 `OWNERS.toml`은 반복 metadata의 source-local 기본값을 제공할 수 있다.
Confit은 regular file, canonical relative path, bounded count/size와 membership digest를
봉인한다. KERNCONF의 명시적인 assignment만 default를 바꾸며 hidden select, first-match,
fragment override와 discovery-order override가 없다.

Preview는 immutable candidate를 만들고 selected generation을 바꾸지 않는다. Apply만
selected pointer를 atomic publish한다. Cancel은 candidate를 publication authority로 만들지
않는다. Ordinary verifier는 resolver나 Bake를 실행하지 않고 sealed input membership,
artifact digest와 verifier identity만 다시 측정한다.

출력은 정확히 `config.h`, `config.mk`, `selection.json`, `provenance.json`,
`input-membership.txt`, `tool-identity.txt`, `config.seal`이다. 이것들은 configuration data일
뿐이며 Confit은 target record, Bake Makefile, source/link graph, tool argv나 runner plan을
생성하지 않는다. 자유 형식 string 값은 Make 문법 주입을 피하기 위해 `config.mk`에 원문을
싣지 않고 escaped `config.h`와 JSON selection에만 싣는다. Make source 선택은 bool,
placement와 closed enum/integer projection만 소비한다.
