---
doc_type: analysis
status: draft
authority: informative
last_verified: 2026-06-24
---

# Confit TUI Terminal Layout Reference

이 문서는 Confit TUI의 시인성을 개선하기 위해 Python kconfiglib `menuconfig.py`와
btop의 terminal renderer/theme/layout source를 분석한 결과다. 목적은 source를 복사하는 것이 아니라,
실제 terminal TUI가 어떤 경계와 흐름으로 화면을 구성하는지 확인하고 Confit에 맞는 적용 원칙을
고정하는 것이다.

이번 라운드의 수정 범위는 문서로 제한한다. Reference source는 `/tmp/confit-tui-reference` 아래에만
clone했으며, Confit repository 안으로 외부 source를 복사하지 않는다.

## Reference State

- kconfiglib repository: `https://github.com/ulfalizer/Kconfiglib`
- kconfiglib local path: `/tmp/confit-tui-reference/Kconfiglib`
- kconfiglib commit: `061e71f7d78cb057762d88de088055361863deff`
- kconfiglib primary file: `menuconfig.py`
- btop repository: `https://github.com/aristocratos/btop`
- btop local path: `/tmp/confit-tui-reference/btop`
- btop commit: `8813e1ca0a3a7439af09f76c6de0616a2961b14e`
- btop primary files: `src/btop_draw.cpp`, `src/btop_theme.cpp`, `src/btop_menu.cpp`,
  `src/btop_config.cpp`

## Current Confit Problem

현재 Confit의 browse 화면은 `src/tui/curses_frontend.c`에서 title/header를 그린 뒤 다시 중앙
`Menu` box를 그리고, 그 안쪽에 option row를 고정 폭 문자열로 출력한다. 이 구조는 화면이 작을수록
다음 문제가 커진다.

- terminal 전체 폭을 이미 쓰고 있는데, 안쪽 box 때문에 좌우 여백과 border가 한 번 더 소비된다.
- option row가 marker, label, detail을 한 줄에 모두 넣어 `prompt`보다 부가 정보가 먼저 눈에 들어온다.
- header와 key legend가 길어서 실제 option 목록이 차지하는 세로 공간이 줄어든다.
- disabled, forced, warning, dirty, search 상태가 의미별 style이 아니라 일부 bold/dim/reverse에 기대고 있다.
- schema editor detail line은 `schema-edit | default=... | prompt=...` 형태로 지나치게 빽빽하다.

따라서 다음 TUI 개선 라운드의 기본 방향은 "내부 box를 제거하고, 의미별 영역과 style을 분리하며,
terminal 폭에 따라 정보를 줄이는 것"이다.

## kconfiglib Menuconfig Flow

kconfiglib의 curses entry point는 top-level `menuconfig.py`다. Terminal TUI의 핵심 함수 흐름은 다음과
같다.

- `_menuconfig(stdscr)`: main loop. `_draw_main()`, `curses.doupdate()`, key read, state transition을
  반복한다.
- `_init()`: curses style 초기화, cursor 숨김, `_path_win`, `_top_sep_win`, `_menu_win`,
  `_bot_sep_win`, `_help_win` window 생성, 초기 menu state 계산.
- `_resize_main()`: terminal 크기에 맞춰 path, top separator, menu list, bottom separator, help
  window의 높이와 위치를 다시 계산한다.
- `_draw_main()`: scroll arrow, centered mainmenu title, visible row list, enabled mode indicator,
  key/help area, menu path를 각 window에 그린다.
- `_shown_nodes(menu)`: 현재 menu에서 보여줄 node list를 만든다. visible node, show-all mode,
  visible child를 가진 invisible parent, implicit submenu indentation을 처리한다.
- `_enter_menu()`, `_leave_menu()`, `_update_menu()`: 현재 menu, selected index, scroll offset,
  parent row stack을 갱신한다.
- `_node_str(node)`, `_value_str(node)`: row text와 value marker를 만든다.
- `_input_dialog()`, `_key_dialog()`: main display를 다시 그린 위에 dialog를 얹고 resize를 처리한다.
- `_jump_to_dialog()`: edit box, match list, separator, help window를 별도로 두고 incremental search를
  처리한다.
- `_info_dialog()`: fullscreen information view를 top line, text window, bottom separator, help
  window로 나누어 보여준다.

Confit에 중요한 점은 kconfiglib가 "box 안에 전체 UI를 넣는 방식"이 아니라는 것이다. Main browse 화면은
window band를 위에서 아래로 나누며, list 영역은 terminal 폭을 거의 그대로 사용한다. Dialog와 info view는
별도 overlay/fullscreen mode로 처리한다.

## kconfiglib Layout Lessons

Confit에 적용할 원칙은 다음과 같다.

- `path/header/list/footer/help`를 renderer 내부 layout unit으로 명확히 나눈다.
- 기본 browse 화면의 option list는 내부 border 없이 full-width viewport로 만든다.
- scroll arrow와 range indicator는 list box border에 붙이지 말고 separator/status band에 둔다.
- resize 시 selected row가 화면 밖으로 밀리지 않도록 scroll offset을 보정한다.
- 작은 terminal에서는 예쁜 layout보다 안전한 fallback을 우선한다.
- help/detail/search는 기본 list를 압축하지 말고 별도 mode 또는 bottom pane으로 제공한다.
- row string 생성과 curses 출력 책임을 분리한다.

## kconfiglib Style Lessons

kconfiglib는 `_STYLES`, `_parse_style()`, `_style_to_curses()`, `_style_attr()`, `_init_styles()`를 통해
style을 의미 단위로 다룬다. 중요한 style key는 `path`, `separator`, `list`, `selection`,
`inv-list`, `inv-selection`, `help`, `show-help`, `frame`, `body`, `edit`, `jump-edit`, `text`다.
Terminal이 color를 지원하지 않으면 `monochrome` style을 강제한다.

Confit에 적용할 원칙은 다음과 같다.

- raw color pair 번호를 화면 코드 곳곳에서 직접 쓰지 않는다.
- `title`, `path`, `header`, `separator`, `list`, `selection`, `category`, `disabled`, `forced`,
  `warning`, `dirty`, `status`, `key`, `help`, `dialog`, `edit`, `search_match` 같은 semantic style을 둔다.
- macOS/Linux ncurses에서 `use_default_colors()` 실패가 UI 전체 실패로 이어지지 않게 한다.
- no-color terminal에서는 색상 대신 bold, dim, reverse, underline만으로 구분한다.
- disabled와 selected가 겹칠 때도 selected row가 읽히도록 attribute merge policy를 정한다.

## kconfiglib Row Lessons

`_node_str()`와 `_value_str()`는 row를 "value marker + prompt + optional name + optional suffix"로 구성한다.
대표 marker는 다음과 같다.

- bool: `[*]`, `[ ]`
- fixed bool/tristate: `-*-`, `- -` 계열
- string/int/hex: `(value)`
- choice selected: `(X)`, `( )`
- menu enter: `--->`
- empty menu: `----`
- new/user value 없음: `(NEW)`

Confit에는 tristate가 없으므로 그대로 가져오지 않는다. 대신 다음 원칙을 고정한다.

- bool은 `[*]`와 `[ ]`를 유지한다.
- enum은 browse row에서는 `<choice>` 또는 `(choice)` 중 하나로 통일하고, choice popup에서는 `(X)`로 선택
  상태를 보여준다.
- int/uint/hex/float/string/path는 `(value)` 형태를 기본값으로 둔다.
- forced/locked value는 별도 marker나 style을 사용하되, prompt를 압도하지 않게 짧게 표시한다.
- category/menu row는 `--->` 감각을 살리되 Confit category/tag 기반 pseudo tree임을 내부 model로만
  표현한다.
- 긴 id, dependency detail, source 정보는 기본 row에 다 넣지 않고 폭이 넓을 때만 suffix로 보여준다.

## kconfiglib Search And Help Lessons

`_jump_to_dialog()`는 fullscreen search view를 사용한다. Edit box, match list, separator, help window를
나누고, 입력이 바뀔 때마다 match list를 다시 만든다. 검색 대상은 symbol/choice name, prompt, menu,
comment다. `Ctrl-F`는 search result에서 info dialog를 열 수 있게 한다.

`_info_dialog()`는 selected node 정보를 fullscreen text view로 보여준다. Scroll 가능한 text window와
아래 help line을 분리한다.

Confit에 적용할 원칙은 다음과 같다.

- `/` search는 prompt 한 줄만 띄우고 끝내는 기능이 아니라, 결과 수와 현재 결과 위치를 명확히 보여주는
  search workflow여야 한다.
- 검색 대상은 id, prompt, help, category, tag, dependency text로 고정한다.
- search jump mode와 filter mode를 UI에서 구분한다.
- `?`/`h` detail view는 id/type/value/default/source/dependency/help를 섹션으로 나눠 읽히게 한다.
- blocked reason, force source, conflict source는 detail view 안에서 바로 확인 가능해야 한다.

## btop Draw/Layout Lessons

btop의 핵심은 "box를 많이 그린다"가 아니라, box와 content를 의미 단위로 나누고 terminal 크기에 따라
폭과 높이를 다시 계산한다는 점이다.

관련 흐름은 다음과 같다.

- `Draw::createBox()`: 위치, 크기, line color, fill 여부, title, numbering을 받아 box string을 만든다.
- `Draw::calcSizes()`: terminal 크기와 표시할 panel 조합에 맞춰 각 panel의 x/y/width/height를 계산한다.
- 각 panel namespace는 `width_p`, `height_p`, `min_width`, `min_height`, `x`, `y`, `width`, `height`를 둔다.
- `Term::width`, `Term::height`와 panel min size를 비교해 불가능한 layout을 피한다.
- 작은 폭에서는 meter, graph, detail width를 줄이고, 필요하면 표시 정보를 줄인다.

Confit에 적용할 원칙은 다음과 같다.

- 기본 browse 화면에서는 내부 box를 줄이지만, dialog/help/schema warning처럼 사용자의 focus를 강하게
  바꿔야 하는 곳에는 box를 계속 쓴다.
- TUI renderer에 `ConfitTuiLayout` 같은 계산 결과를 두고, draw 함수는 계산된 rect만 사용하게 한다.
- 최소 폭/높이 정책을 코드에 명시한다. 예: compact, standard, wide, split-detail.
- terminal 폭이 넓을 때만 오른쪽 detail pane 또는 extended columns를 보여준다.
- terminal 폭이 좁을 때는 prompt와 marker만 남기고 id/detail은 detail view로 이동한다.

## btop Theme Lessons

btop은 `Theme::Default_theme`, `Theme::TTY_theme`, `generateColors()`, `generateTTYColors()`, `setTheme()`로
semantic color table을 관리한다. 대표 key는 `main_bg`, `main_fg`, `title`, `hi_fg`, `selected_bg`,
`selected_fg`, `inactive_fg`, `meter_bg`, `div_line`, panel-specific box color 등이다.

Confit에 적용할 원칙은 다음과 같다.

- color는 "파랑 몇 번"이 아니라 "selection foreground/background", "inactive foreground",
  "warning foreground", "separator"처럼 의미로 참조한다.
- no-color/TTY fallback을 처음부터 별도 theme으로 둔다.
- btop처럼 사용자 theme file까지 즉시 구현할 필요는 없다. 이번 9라운드에서는 built-in semantic palette가
  목표다.
- 색상은 정보 위계에만 사용하고, config value 자체를 색만으로 구분하지 않는다.

## Layout Rules For Upcoming Rounds

다음 라운드에서 적용할 layout 규칙은 다음과 같다.

1. 기본 profile editor browse 화면에서 중앙 `Menu` box를 제거한다.
2. 화면은 위에서부터 title/path, context header, list viewport, separator/status, key legend 순서로 둔다.
3. list viewport는 terminal 좌우 폭을 최대한 사용한다.
4. option row의 1차 정보는 marker와 prompt다.
5. id, source, dependency badge, range, tags는 wide terminal에서만 보조 정보로 표시한다.
6. 작은 terminal에서는 compact fallback을 쓰며, fallback에서도 prompt가 먼저 보여야 한다.
7. 도움말/detail/search/schema warning/input dialog는 focus 전환이 필요한 view이므로 box 또는 fullscreen
   view를 사용할 수 있다.
8. renderer는 layout 계산, style 선택, row clipping을 책임지고 core/profile/schema model을 직접 변경하지
   않는다.

## Width Policy

Confit TUI는 terminal 폭을 다음 네 등급으로 다룬다.

- `compact`: 40-79 columns. title, one-line status, marker + prompt 중심 list, 최소 key hint.
- `standard`: 80-119 columns. marker + prompt + short value/source badge.
- `wide`: 120-159 columns. id/category/search/filter/dirty 상태를 header와 row suffix에 일부 표시.
- `expanded`: 160 columns 이상. 오른쪽 detail pane 또는 extended row detail을 검토한다.

이 정책은 magic number가 아니라 test matrix의 기준이 되어야 한다. 각 라운드는 최소 80/100/120/160 columns
rendering smoke를 갖는 것을 목표로 한다.

## Status And Key Legend Rules

상단 context header에는 비교적 안정적인 상태를 둔다.

- project
- profile
- target
- dirty
- search/filter enabled state
- current mode: profile editor, schema editor, help, search, dialog

하단 status line에는 즉시 사라져도 되는 이벤트를 둔다.

- saved
- invalid input
- blocked reason summary
- search result count
- validation error summary

하단 key legend는 항상 짧아야 한다. 모든 키를 한 번에 나열하지 않고 현재 mode에서 중요한 키만 보여준다.
전체 keymap은 docs에 두고, TUI footer는 현재 조작에 필요한 힌트만 담당한다.

## What Confit Should Not Copy

- Kconfig model, Kconfig parser, tristate semantics는 가져오지 않는다.
- btop의 process/graph/meter rendering model은 Confit의 configuration browser와 목적이 다르므로 가져오지
  않는다.
- btop의 custom theme file loader는 이번 9라운드의 목표가 아니다.
- Unicode-heavy graph/meter symbol은 필수가 아니다. Confit row marker는 ASCII를 기본으로 유지한다.
- 실제 Parus/Delos config, runtime source, CMake/QStar generator는 이 TUI 개선 범위가 아니다.

## Acceptance Criteria For The TUI Readability Work

9라운드가 끝났을 때 다음 조건을 만족해야 한다.

- 기본 browse 화면에는 option list를 감싸는 내부 `Menu` box가 없다.
- 80 columns에서도 option prompt와 current value가 먼저 읽힌다.
- disabled/forced/blocked/dirty/search 상태가 semantic style과 detail view로 설명된다.
- profile editor와 schema editor가 같은 renderer/key legend/status 원칙을 공유한다.
- help/search/input dialog는 macOS/Linux ncurses에서 resize 후에도 깨지지 않는다.
- no-color terminal에서도 selected row, disabled row, warning 상태가 구분된다.
- scripted TUI test와 manual transcript가 새 layout을 기준으로 갱신된다.

## Round 2 Implementation Notes

다음 라운드에서는 `src/tui/curses_frontend.c`의 `confit_tui_curses_render()`를 중심으로 다음 작업부터
시작한다.

- layout 계산 helper를 추가한다.
- title/header/list/status/key legend 영역을 rect로 계산한다.
- 기본 browse path에서 `confit_tui_curses_draw_box(..., "Menu")` 호출을 제거한다.
- list row 출력 위치를 `menu_left + 2` 같은 box 내부 좌표가 아니라 list viewport 좌표로 바꾼다.
- scroll indicator를 list box border가 아닌 separator/status 영역으로 옮긴다.
- compact fallback도 prompt-first row format을 쓰게 한다.
