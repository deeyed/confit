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

## CLI와 TUI

`search`, `explain`, `why-unavailable`, `list-new`, `oldconfig`, `save-minimal`, `diff`와
`tui`는 catalog/evaluation 위의 presentation workflow다. 이 계층은 Makefile, source,
link/provider graph와 arbitrary probe를 읽지 않는다. Option row는 menu id, menu order와
symbol로 안정 정렬하며 prompt/help/tag를 bounded case-insensitive search한다. Detail은
owner, since, stability, declaration span과 resolver reason을 함께 표시한다.

Minimal KERNCONF는 default와 다른 effective value만 menu/order/symbol의 안정 순서로 출력한다.
CLI와 TUI가 별도 serializer를 갖는 것은 금지한다. 새 option은 assignment가 없으면
`list-new`/`oldconfig`에서 default와 함께 보이며, 삭제·rename된 symbol이 KERNCONF에
남으면 stale input으로 fail-closed한다. 자동 rename, unknown ignore와 silent last-wins는 없다.

TUI input은 closed action set으로 낮춘다. 화살표 및 `j`/`k`, `/search`,
`set SYMBOL VALUE`, help, preview, apply, cancel 이외의 control/escape byte는 거부한다.
화면은 최소 40x10, 최대 512x256이고 query는 127 bytes다. Text label로 user/default/derived와
unavailable을 구분하므로 색상이나 포인터 장치가 필수가 아니다. 현재 resolver는 hidden
select를 제공하지 않으므로 derived label은 미래의 명시적 source provenance를 위한 값이며
frontend가 임의로 만들 수 없다.

출력은 정확히 `config.h`, `config.mk`, `selection.json`, `provenance.json`,
`input-membership.txt`, `tool-identity.txt`, `config.seal`이다. 이것들은 configuration data일
뿐이며 Confit은 target record, Bake Makefile, source/link graph, tool argv나 runner plan을
생성하지 않는다. 자유 형식 string 값은 Make 문법 주입을 피하기 위해 `config.mk`에 원문을
싣지 않고 escaped `config.h`와 JSON selection에만 싣는다. Make source 선택은 bool,
placement와 closed enum/integer projection만 소비한다.
