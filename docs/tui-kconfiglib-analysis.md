---
doc_type: analysis
status: draft
authority: informative
last_verified: 2026-06-23
---

# Kconfiglib Menuconfig TUI Analysis

이 문서는 Confit TUI를 kconfiglib/menuconfig와 더 비슷하게 만들기 위한 reference 분석이다.
Reference source는 `/tmp/confit-kconfiglib-reference`에만 clone했고, Delos repository 안으로
외부 source를 복사하지 않았다.

분석 대상:

- Repository: `https://github.com/ulfalizer/Kconfiglib`
- Local reference path: `/tmp/confit-kconfiglib-reference`
- Reference commit: `061e71f7d78cb057762d88de088055361863deff`
- Reference state at analysis time: clean worktree
- License noted in clone: ISC

이 분석의 목적은 source를 베끼는 것이 아니라, kconfiglib가 실제로 TUI를 구성하는 script와 함수
흐름을 확인하고 Confit에 맞는 구조로 재해석하는 것이다.

## Relevant Scripts

`setup.py`의 console entry point는 `menuconfig = menuconfig:_main`이다. 따라서 terminal TUI의
실제 entry script는 top-level `menuconfig.py`이며, GUI counterpart는 `guiconfig.py`다.

중요 파일은 다음과 같다.

- `menuconfig.py`: curses 기반 terminal menuconfig 구현 전체.
- `kconfiglib.py`: `Kconfig`, `MenuNode`, `Symbol`, `Choice` model과 load/save/value/dependency logic.
- `examples/menuconfig_example.py`: menuconfig 표기법과 tree 출력의 최소 예제.
- `examples/print_config_tree.py`: menuconfig와 같은 value marker로 tree를 출력하는 예제.

Confit에 가장 직접적으로 유용한 reference는 `menuconfig.py`와 두 example file이다. `kconfiglib.py`는
model 개념을 이해하기 위한 reference로만 사용하고, Confit의 TOML schema/resolver/graph model을
대체하지 않는다.

## High-Level Flow

`menuconfig.py`의 top-level flow는 다음 순서다.

1. `_main()`이 `standard_kconfig(__doc__)`로 `Kconfig` instance를 만들고 `menuconfig(kconf)`를 호출한다.
2. `menuconfig(kconf)`가 global UI state를 초기화한다.
3. `standard_config_filename()`으로 `.config` save/load filename을 결정한다.
4. `_load_config()`가 existing config를 읽고 `_needs_save()` 결과로 dirty state를 만든다.
5. top menu에 visible item이 없으면 `_show_all = True`로 전환하고 다시 `_shown_nodes()`를 시도한다.
6. locale을 terminal에 맞추고 `ESCDELAY=0`을 설정한다.
7. `curses.wrapper(_menuconfig)`로 curses lifecycle을 맡긴다.
8. `_menuconfig(stdscr)`가 `_init()` 후 `draw -> read key -> transition` loop를 돌린다.

핵심 구조는 “model을 직접 그리는 TUI”가 아니라, model에서 현재 menu에 보여줄 node list를 계산하고,
그 list를 curses windows에 그린 뒤, key dispatch가 model update와 view recalculation을 호출하는
방식이다.

## Curses Window Layout

`_init()`은 main screen을 여러 window로 나눈다.

- `_path_win`: 최상단 menu path. 예: `(Top) -> Menu -> Submenu`.
- `_top_sep_win`: mainmenu title과 위쪽 scroll arrow.
- `_menu_win`: 현재 menu의 row list.
- `_bot_sep_win`: 아래쪽 scroll arrow와 enabled mode 표시.
- `_help_win`: 기본 key help 또는 show-help mode의 selected item help.

`_resize_main()`은 terminal size에 맞춰 각 window 크기와 위치를 다시 계산한다. show-help mode가 켜져
있으면 `_help_win` 높이를 `_SHOW_HELP_HEIGHT`로 고정하고, 아니면 key legend line 수만큼 둔다.
너무 작은 terminal에서는 예쁜 layout을 포기하고 error를 피하는 fallback을 둔다.

Confit 적용점:

- 현재 Confit의 단일 boxed screen보다 kconfiglib처럼 window를 분리한다.
- path/header/list/bottom/help 영역을 명시적으로 나눈다.
- resize와 작은 terminal fallback을 renderer 책임으로 둔다.

## Main Event Loop

`_menuconfig()`는 다음 형태의 loop다.

1. `_draw_main()`
2. `curses.doupdate()`
3. `_getch_compat(_menu_win)`
4. key별 state transition

주요 key binding은 다음과 같다.

- Down/Up, `j/k`: row 이동.
- PageDown/PageUp, Ctrl-D/Ctrl-U: 여러 row 이동.
- End/Home, `G/g`: list 끝/처음.
- Space: 가능하면 value toggle, 아니면 menu enter.
- Enter/Right/`l`: 가능하면 menu enter, 아니면 value toggle.
- Left/Backspace/Esc/`h`: parent menu로 이동. top menu에서 Esc는 quit dialog.
- `n/m/y`: selected bool/tristate 값을 직접 지정.
- `o`: config load dialog.
- `s`: config save dialog.
- `d`: minimal config save dialog.
- `/`: jump-to search dialog.
- `?`: selected item info dialog.
- `f`: show-help mode toggle.
- `c`: show-name mode toggle.
- `a`: show-all mode toggle.
- `q`: quit dialog.

Confit 적용점:

- Confit은 tristate와 minimal config가 없으므로 `m`, `d`는 그대로 가져오지 않는다.
- Space/Enter의 “toggle vs enter” 차이는 가져온다.
- show-help, show-name, show-all은 Confit에도 유용하다. 이름은 Confit option id, show-all은 hidden/disabled
  item 표시 mode로 해석한다.

## Menu Tree To Rows

TUI row list의 핵심은 `_shown_nodes(menu)`다. 이 함수는 현재 menu node 아래에서 실제로 보여줄
`MenuNode` 목록을 만든다.

중요 정책:

- `_visible(node)`가 참이거나 show-all mode면 row에 넣는다.
- visible node가 child를 갖고, 그 node가 별도 menu가 아니라면 child를 indentation으로 이어 붙인다.
- invisible symbol도 visible child가 있으면 parent row를 보여준다.
- choice menu는 여러 location의 choice symbol을 모아 중복을 줄인다.

`_parent_menu(node)`, `_enter_menu(menu)`, `_leave_menu()`, `_jump_to(node)`는 현재 menu와 selected index,
scroll 위치를 바꾸는 navigation helper다. 특히 parent menu를 오갈 때 `_parent_screen_rows` stack으로
화면상 row 위치를 기억해 scroll jump를 줄인다.

Confit 적용점:

- Confit에는 Kconfig `MenuNode`가 없으므로 TUI 전용 `ConfitTuiMenuNode` 또는 `ConfitTuiRow`를 둔다.
- `category`, `tag`, dependency visibility, profile state를 이용해 pseudo menu tree를 만든다.
- 선택 row, scroll offset, parent menu stack을 state에 넣어 navigation이 흔들리지 않게 한다.

## Row Rendering Notation

`_node_str(node)`와 `_value_str(node)`가 menuconfig 표기법의 중심이다.

주요 marker:

- `[*]`: enabled bool.
- `[ ]`: disabled bool.
- `<M>`, `<*>`, `< >`: tristate.
- `{M}`/`{*}`: m/y만 가능한 tristate.
- `-*-`, `-M-`: pinned value.
- `(foo)`: string/int/hex value.
- `(X)`/`( )`: y-mode choice symbol selection.
- `*** comment ***`: comment row.
- `--->`: enter 가능한 menu/menuconfig/choice.
- `----`: empty menu.
- `(NEW)`: `.config`에 user value가 없던 symbol.

Confit 적용점:

- Confit은 bool/string/int/uint/hex/float/path/enum만 있으므로 다음 표기부터 고정한다.
- bool: `[*]` / `[ ]`
- enum: `<choice>` 또는 `(choice)` 중 하나로 통일. menuconfig choice 느낌을 살리려면 enum edit popup에서
  selected item을 `(X)`로 표시한다.
- int/uint/hex/float/string/path: `(value)`
- forced/locked value: menuconfig의 `-*-`에서 착안해 `-*-` 또는 `-value-` 계열 marker를 검토한다.
- category/menu row: `--->`
- empty category: `----`
- unsaved profile override: `(modified)` 또는 row style로 표시한다.

## Value Change Flow

`_change_node(node)`는 selected node가 바뀔 수 있는지 `_changeable(node)`로 먼저 검사한다. 변경 가능한
node라면 type에 따라 분기한다.

- int/hex/string: `_input_dialog()`를 띄우고 `_check_valid()`로 형식/range를 검사한다.
- bool/tristate: `assignable` list에서 다음 값을 순환한다.
- y-mode choice symbol: 선택 후 parent menu로 즉시 나간다.
- `_set_val(sc, val)`이 실제 model update를 수행하고 `_conf_changed = True`로 만든다.
- 값 변경 후 `_update_menu()`가 visible row list, selected index, scroll을 다시 계산한다.

Confit 적용점:

- Confit도 edit 가능 여부를 먼저 계산한다. disabled/forced/conflicted option은 dialog를 열지 않고 reason을
  detail/help에 보여준다.
- bool은 즉시 toggle한다.
- enum은 choice popup을 별도로 둔다.
- number/string/path는 input dialog를 둔다.
- `confit check` 수준의 full validation은 저장 전에 수행하되, type/range/choice validation은 edit 즉시
  수행한다.
- 값 변경 후 resolver를 다시 실행하고 visible/blocked row를 재계산한다.

## Input And Key Dialogs

`_input_dialog()`는 main screen 위에 floating dialog를 그린다. resize가 발생하면 main screen과 dialog를
함께 resize한다. `_edit_text()`는 cursor movement, Home/End, Backspace/Delete, Ctrl-W, Ctrl-K,
Ctrl-U 같은 line editing을 처리한다.

`_key_dialog()`는 quit/load confirmation이나 message/error dialog에 쓰인다. ESC는 취소로 취급하고,
허용된 key만 caller에게 반환한다.

Confit 적용점:

- 모든 profile/schema edit input은 같은 dialog primitive를 사용한다.
- 입력 dialog는 현재 화면 위에 겹쳐 뜨며, resize에서도 깨지지 않아야 한다.
- quit dirty confirm, load confirm, save success/error는 key dialog primitive로 통일한다.

## Load, Save, Dirty State

`_load_config()`는 `Kconfig.load_config()` 메시지를 출력하고, `.config`가 없거나 `_needs_save()`가 참이면
dirty state를 켠다. `_needs_save()`는 missing symbol, user value와 effective value 차이, 새로 써야 할
symbol이 있는지 검사한다.

`_save_dialog()`는 filename input을 받은 뒤 `_try_save()`를 호출한다. `_try_save()`는 `write_config()`나
`write_min_config()` 같은 save function을 받아 실행하고, 실패하면 error dialog를 띄운다.

`_quit_dialog()`는 dirty state가 없으면 바로 종료 message를 반환한다. dirty state가 있으면
Yes/No/Cancel confirm을 띄운다.

Confit 적용점:

- Confit TUI dirty state는 “profile TOML에 아직 저장하지 않은 override가 있는지”로 둔다.
- save target은 `.config`가 아니라 `config/profiles/<profile>.toml`이다.
- save 전 full schema/graph/resolve validation을 강제한다.
- Confit의 기본 exit/back key는 `Esc`다. Root menu에서 dirty state가 있으면 save/discard/cancel dialog를
  띄운다. `q`는 호환 alias로만 둘 수 있다.
- 저장 성공 후 profile을 reload/resolve해서 화면 state와 파일 state를 일치시킨다.

## Search And Jump Flow

`_jump_to_dialog()`는 fullscreen search dialog다.

구성:

- top edit box
- match list
- bottom separator
- help window

동작:

- search text가 바뀔 때마다 regex list를 만든다.
- symbol/choice node는 name 또는 prompt에서 검색한다.
- menu/comment node는 prompt에서 검색한다.
- 여러 단어/regex는 모두 match해야 한다.
- bad regex는 match list 대신 error message로 표시한다.
- Enter는 selected match로 jump한다.
- Esc는 취소한다.
- Ctrl-F는 selected match의 info dialog를 search 안에서 연다.

Confit 적용점:

- Confit search는 option id, prompt, help, category, tag, source file을 대상으로 한다.
- regex를 바로 도입하면 escaping UX가 무거울 수 있으므로 첫 구현은 case-insensitive substring과 token AND
  match로 시작하고, regex mode는 나중에 검토한다.
- search result는 menu tree 위치로 jump해야 한다. 이를 위해 row마다 stable option id/category path를 둔다.
- search dialog 안에서 help/detail view를 열 수 있게 한다.

## Info And Help Flow

`_info_dialog(node, from_jump_to_dialog)`는 fullscreen information view다. `_info_str(node)`가 content를
구성하고, `_draw_info_dialog()`가 scrollable text window와 help footer를 그린다.

symbol info에 포함되는 항목:

- name
- prompt
- type
- current value
- help text
- direct dependencies와 현재 expression value
- defaults
- select/imply reverse dependency
- Kconfig definition, location, include path, menu path

Confit 적용점:

- Confit detail/help는 `confit explain` engine과 resolver source tracing을 사용한다.
- option id, prompt, type, current value, default value, source of value, category/tags, requires/conflicts,
  recommends/forces, visible reason, blocked reason, schema file path를 보여준다.
- schema edit mode에서는 option definition TOML path와 generated schema fragment를 보여준다.

## Styling And Portability

Kconfiglib는 style map을 두고 UI element별 curses attribute를 관리한다. color가 없으면 monochrome style로
fallback한다. `MENUCONFIG_STYLE`로 theme override도 지원한다. `_safe_addstr()`, `_safe_hline()` 같은 safe
wrapper는 작은 terminal이나 edge write에서 curses error가 나도 UI가 죽지 않게 한다.

Confit 적용점:

- 초기에는 fixed style map만 둔다.
- color가 없으면 reverse/bold 기반 monochrome fallback을 둔다.
- curses call은 renderer wrapper 안에서만 수행한다.
- 작은 terminal fallback과 clipping helper를 필수로 둔다.

## What To Adopt

Confit TUI에 직접 반영할 항목:

- `curses.wrapper` 또는 동등한 lifecycle wrapper.
- path/header/list/separator/help window 분리.
- `draw -> doupdate -> get key -> transition` loop.
- current menu, visible row list, selected index, scroll offset, parent row stack.
- Space/Enter behavior split.
- `:` command mode를 통한 verbose/noverbose/tree/flat/filter 같은 view control.
- menuconfig marker convention.
- fullscreen jump/search dialog.
- fullscreen info/help dialog.
- floating input/key dialog primitives.
- dirty exit confirm.
- save success/error dialog.
- safe curses drawing helpers.

## What Not To Adopt

Confit에 가져오지 않을 항목:

- Kconfig parser, Kconfig file language, `.config` format.
- tristate semantics and module mode.
- Kconfig `select`/`imply` semantics as-is. Confit has `forces`/`recommends` with explicit validation.
- `write_min_config()`/`savedefconfig` semantics.
- Python regex-based search as the default UX.
- `MENUCONFIG_STYLE` environment parsing in the first pass.
- Kconfiglib global variable structure as-is. Confit C implementation should keep state in explicit structs.

## Proposed Confit Mapping

Kconfiglib concept to Confit concept:

- `Kconfig`: `ConfitProject` plus loaded profile/target/resolved config.
- `MenuNode`: TUI-only `ConfitTuiNode` built from category/option/schema rows.
- `Symbol`: `ConfitOption`.
- `Choice`: enum option plus choice dialog.
- `prompt`: `ConfitOption.prompt`.
- `help`: `ConfitOption.help`.
- `visibility`: graph/resolver visibility and blocked reason.
- `assignable`: type/range/enum/force/conflict editability.
- `str_value`/`tri_value`: resolved `ConfitValue`.
- `user_value`: profile override value.
- `.config`: profile TOML.
- `write_config()`: save profile TOML then reload/check.
- `missing_syms`: unknown option id in profile/schema validation.

## Implementation Consequences For The Next Rounds

다음 구현 라운드는 먼저 TUI code를 나누어야 한다. kconfiglib는 `menuconfig.py` 하나에 전역 상태를 몰아도
Python script로는 버틸 수 있지만, Confit C code에서는 다음 분리가 필요하다.

- curses lifecycle/drawing: `src/tui/curses_*`
- menu state and row model: `src/tui/menu_*`
- profile editor commands: `src/tui/profile_*`
- schema editor commands: `src/tui/schema_*`
- dialogs: `src/tui/dialog_*`
- search/info views: `src/tui/search_*`, `src/tui/info_*`

이 분석을 기준으로 Confit TUI의 목표는 “Kconfiglib source clone”이 아니라 “menuconfig 사용자가 기대하는
화면 구조, 키 감각, 검색/도움말/저장 흐름”을 Confit model에 맞게 구현하는 것이다.
