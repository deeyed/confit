# 08. TUI 사용 가이드

Confit TUI는 macOS/Linux에서 ncurses로 동작하는 terminal UI다. 목적은
Parus/Delos 설정을 keyboard 중심으로 탐색하고, profile TOML과 guarded schema
TOML을 안전하게 편집하는 것이다. Windows는 현재 CLI-only이며 TUI는
unsupported로 실패해야 한다.

## TUI 실행 전 확인

```sh
confit doctor
```

macOS/Linux에서 다음처럼 보이면 TUI를 사용할 수 있다.

```text
curses: available; TUI enabled
```

## Profile editor 열기

Fixture로 먼저 연습할 때:

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

Schema editor는 위험한 모드다. Option type, default, range, choices,
dependency 같은 project 의미를 바꿀 수 있으므로 진입 시 경고를 보여준다.
Schema edit 결과는 code review 대상으로 다루는 것이 원칙이다.

## 기본 화면 구성

TUI 기본 화면은 option list를 내부 box 안에 가두지 않는다. 화면은 위에서부터
다음처럼 나뉜다.

```text
title/path band
context header + breadcrumb
full-width list viewport
fixed inspector/status band
key legend 또는 : command line
```

현재 위치는 breadcrumb로 표시된다.

```text
Delos / Runtime / Trace
```

기본 list row는 한 줄 높이를 유지한다. 선택 포인터가 움직일 때 row 아래에
긴 id/type/deps/tags 설명이 붙지 않는다.

```text
[*] Trace enabled                         y
(4) Trace ring capacity                   4096
<hw> Execution mode                       hw
```

선택한 option의 짧은 정보는 화면 맨 아래의 고정 inspector/status 영역에
나온다.

```text
Trace ring capacity <delos.trace.capacity>  uint  deps ok
```

자세한 id/type/deps/tags/source 정보는 `:verbose`로 켠다.

```text
:verbose
```

다시 간단한 표시로 돌아가려면:

```text
:noverbose
```

## Menu tree 탐색

Confit TUI는 kconfiglib menuconfig처럼 menu 안으로 들어가는 탐색을 지원한다.
다만 너무 깊은 tree는 지양한다. 권장 depth는 2단계이며, 큰 영역에서만
3단계를 허용한다.

예:

```text
Main Menu
  Runtime
    Trace
      Trace ring capacity
```

조작:

```text
Enter 또는 Space  menu 안으로 진입
Esc 또는 Left     상위 menu로 이동
Esc at root       종료 흐름으로 진입
```

Search로 다른 menu 아래의 option을 찾으면 TUI가 그 menu 위치로 이동하고
breadcrumb를 갱신한다.

## 주요 키

```text
방향키 / j k       위아래 이동
PageUp/PageDown   페이지 이동
Home/End          현재 view의 처음/끝 이동
Enter/Space       menu 진입 또는 option edit/toggle
Left              상위 menu로 이동
Esc               취소, 뒤로, root에서는 종료 흐름
:                 command prompt
/                 search jump
n / N             다음/이전 search result
? 또는 h          help/detail view
s                 저장
```

`q`는 예전 menuconfig식 습관을 위한 alias로 남길 수 있지만, 사용자가 기억해야
하는 기본 종료 키는 `Esc`다.

## `:` command mode

`:`를 누르면 화면 아래에 command prompt가 열린다. 많은 view option을 단축키로
늘어놓지 않고 command로 켜고 끄기 위한 기능이다.

주요 command:

```text
:verbose       하단 inspector에 id/type/deps/tags/source를 자세히 표시
:noverbose     compact inspector로 복귀
:tree          menu stack view 사용
:flat          전체 option flat list 보기
:filter text   id/prompt/help/category/tag 대상으로 filter
:clear         search/filter 상태 해제
:help          command 목록 보기
:quit          종료 흐름 실행
```

Command 입력 중 `Esc`를 누르면 입력을 취소하고 이전 화면으로 돌아간다.

## Profile editor에서 하는 일

Bool toggle:

```text
선택 후 Space
```

Enum 선택:

```text
선택 후 Enter 또는 e
choice popup에서 후보 선택
```

Int/uint/hex/float/string/path 편집:

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
absolute path
빈 string/path
```

## 저장과 dirty exit

수정 후 `s`를 누르면 저장 confirmation이 뜬다. 저장 전 full validation을
수행한다.

수정한 뒤 저장하지 않고 root menu에서 `Esc`를 누르면 dirty confirmation이
뜬다.

```text
Save changes
Discard changes
Cancel
```

`Cancel`을 고르면 TUI로 돌아간다.

## Help/detail view

`?` 또는 `h`를 누르면 선택한 menu 또는 option의 자세한 정보를 본다.

표시되는 정보:

```text
prompt
id
type
current value
default
source
category path
tags
requires/conflicts/forces/recommends
blocked reason
help text
```

“왜 못 바꾸는가”를 볼 때는 하단 inspector보다 help/detail view가 더 유용하다.

## Search와 Filter

`/`는 search jump다. 전체 menu tree에서 일치하는 option을 찾고 그 위치로
이동한다.

검색 대상:

```text
id
prompt
help
category path
tag
```

예:

```text
/debug
/board
/trace
```

검색 결과가 여러 개이면 `n`, `N`으로 이동한다.

목록 자체를 줄이고 싶으면 `:filter`를 사용한다.

```text
:filter trace
:clear
```

## Schema editor에서 하는 일

Schema editor는 option을 만들거나 수정한다.

주요 작업:

```text
n      새 option
y      type 변경
d/e    default 변경
p      prompt 변경
h      help 변경
c      category path 변경
t      tags 변경
r      range 변경
o      enum choices 변경
s      저장
```

저장 전 schema와 graph validation을 수행한다. 저장 파일은 TOML이다. Hidden
binary DB는 없다.

## TUI 사용 원칙

- Profile 값 변경은 TUI로 해도 된다.
- Schema edit은 guarded workflow이며 code review 대상이다.
- Menu tree는 얕게 유지한다. 2단계를 기본으로 보고, 4단계 이상은 피한다.
- List row는 prompt와 value 중심으로 읽는다. 자세한 id/dependency 정보는
  inspector, `:verbose`, help/detail에서 본다.
- TUI가 저장한 뒤에도 중요한 profile은 `confit check`를 다시 실행한다.
- 중요한 변경은 `confit diff`와 generated report로 review한다.
