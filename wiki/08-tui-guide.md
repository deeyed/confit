# 08. TUI 사용 가이드

Confit TUI는 macOS/Linux에서 ncurses로 동작하는 menuconfig-style interface다. Windows에서는 현재
CLI-only이며 TUI는 unsupported로 실패한다.

## TUI 실행 전 확인

```sh
confit doctor
```

macOS/Linux에서 다음처럼 보이면 TUI를 사용할 수 있다.

```text
curses: available; TUI enabled
```

## Profile editor 열기

```sh
confit tui \
  --project tools/confit/tests/fixtures/tui/profile-editor \
  --profile edit
```

실제 project에서는:

```sh
confit tui --project config/confit --profile sim-dsh
```

## Schema editor 열기

```sh
confit tui \
  --project tools/confit/tests/fixtures/tui/schema-editor \
  --schema-edit
```

schema editor는 위험한 모드다. option type, default, range, choices, dependency 같은 project 의미를 바꿀
수 있기 때문에 진입 시 경고를 보여준다.

## 기본 화면 구성

TUI는 다음 구조를 가진다.

```text
상단 title
project/profile/target header
중앙 boxed menu
하단 key legend
status line
```

option 표시 예:

```text
[*] delos.debug.dsh
[ ] delos.profile.release
(sim) delos.target.kind
"build/generated/config" delos.generated.config_root
```

## 주요 키

```text
방향키 / j k       위아래 이동
PageUp/PageDown   페이지 이동
Home/End          처음/끝 이동
Enter/Space       bool toggle 또는 edit
e                 선택 option edit
/                 search
n / N             다음/이전 search result
? 또는 h          help/detail view
s                 저장
q                 종료
Esc               dialog 취소
```

## Profile editor에서 하는 일

bool toggle:

```text
선택 후 Space
```

enum 선택:

```text
선택 후 Enter 또는 e
choice popup에서 후보 선택
```

int/uint/hex/float/string/path 편집:

```text
선택 후 e
입력 dialog에 새 값 입력
```

잘못된 값은 저장 단계까지 가지 않고 edit dialog에서 막는다.

예:

```text
int range 밖 값
uint 음수
hex range 밖 값
float NaN 또는 infinity
path absolute path
빈 string
```

## 저장과 dirty quit

수정 후 `s`를 누르면 저장 confirmation이 뜬다. 저장 전 full validation을 수행한다.

수정한 뒤 저장하지 않고 `q`를 누르면 dirty quit confirmation이 뜬다.

```text
Save changes
Discard changes
Cancel
```

## Help/detail view

`?` 또는 `h`를 누르면 선택한 option의 자세한 정보를 본다.

표시되는 정보:

```text
prompt
id
type
current value
default
source
category
tags
requires/conflicts/forces/recommends
help text
blocked reason
```

“왜 못 바꾸는가”를 볼 때 help/detail view가 가장 유용하다.

## Search

`/`를 누르고 검색어를 입력한다.

검색 대상:

```text
id
prompt
help
category
tag
```

예:

```text
/debug
/board
/generated
```

검색 결과가 여러 개이면 `n`, `N`으로 이동한다.

## Schema editor에서 하는 일

schema editor는 option을 만들거나 수정한다.

주요 작업:

```text
n      새 option
y      type 변경
d/e    default 변경
p      prompt 변경
h      help 변경
c      category 변경
t      tags 변경
r      range 변경
o      enum choices 변경
s      저장
```

저장 전 schema와 graph validation을 수행한다. 저장 파일은 TOML이다. hidden binary DB는 없다.

## TUI 사용 원칙

- profile 값 변경은 TUI로 해도 된다.
- schema edit은 guarded workflow이며 code review 대상이다.
- TUI가 저장한 뒤에도 `confit check`를 다시 실행한다.
- 중요한 변경은 `confit diff`와 generated report로 review한다.
