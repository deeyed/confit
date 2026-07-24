---
doc_type: tool-spec
authority: operational
language: ko
---

# Schema V2 TUI Adapter

이 문서는 `schema_version = 2` project에 적용되는 `confit tui`의 정본 동작을 설명한다. V1 category-path UI의 동작은 [cli-tui.md](cli-tui.md)를 따른다. V2는 V1 graph 의미를 재사용하지 않는다. 모든 표시와 edit preview는 V2 linker, compiled structure, immutable snapshot, constraint report를 통해서만 얻는다.

## 지원 호스트와 진입

macOS와 Linux에서 ncurses build를 사용한다. Windows clang preview는 CLI-only이며 `confit tui`는 exit code `8`로 unsupported를 반환한다.

```sh
confit tui --project config/my-project --profile debug
confit tui --project config/my-project --profile debug --target sim
confit tui --project config/my-project --schema-edit
```

`--project`는 project root 또는 `config/project.toml`이 있는 config root다. `--profile`은 profile editor에서 필수다. `--target`은 그 session의 target selection만 지정하며 profile TOML의 `[profile].target`을 바꾸지 않는다.

## 화면과 탐색

V2 profile editor는 explicit `[menu."id"]` tree를 그대로 얕게 표시한다. root에서는 child menu와 `option.menu`가 없는 option을, submenu에서는 직접 child menu와 그 menu에 배치된 option만 표시한다.

```text
Main Menu
  Core
    Enable feature
    Execution mode
  Tuning
    Trace capacity
    Advanced
```

- `Enter` 또는 `Space`: menu 진입, option 편집.
- `Left` 또는 `Esc`: submenu에서는 부모 menu로 이동한다.
- root의 `Esc`: clean session은 종료하고 dirty session은 Save/Discard/Cancel을 묻는다. `q`는 호환 alias다.
- `/`: id, prompt, help, menu id, tag 전체에서 search하고 해당 menu로 jump한다. `n`/`N`은 다음/이전 결과다.
- `?`/`h`: option detail을 연다. detail은 requested/effective value와 각 source, write domain, available/visible 상태, help를 따로 보여준다.
- `v`, `:verbose`, `:noverbose`: 하단 inspector의 compact/verbose 표시를 바꾼다.
- `:tree`, `:flat`: menu tree 또는 전체 option flat view를 고른다. `:filter text`는 현재 view의 id/prompt/help/menu/tag를 줄이고, `:clear`는 search/filter 상태를 지운다. `:help`는 지원 command를 status row에 표시하고 `:quit`은 Esc root exit와 같은 흐름을 시작한다.

V2 menu depth는 schema author가 제한해야 한다. 보통 2단계, 필요한 경우 3단계를 권장한다. 탐색의 주 수단은 얕은 menu와 search이며, 깊은 tree는 사용성을 높이지 않는다.

## 표시 의미

row의 marker와 흐린 style은 resolver가 publish한 snapshot만 반영한다.

- `[*]`, `[ ]`: effective bool.
- `[y]`, `[m]`, `[n]`: effective tristate.
- `unavailable`: `available_if`가 false다. 값이 있어도 edit할 수 없다.
- `hidden`: `visible_if`가 false다. TUI는 이유를 설명할 수 있도록 흐리게 표시한다.
- `computed`: computed write-domain이다. profile editor에서는 read-only다.
- `read-only target domain`, `read-only schema domain`: 현재 profile TOML writer가 소유하지 않는 option이다.

하단 inspector는 compact mode에서 prompt/id/type/effective state를, verbose mode에서 requested origin/source와 effective origin/source까지 분리해 표시한다. requested와 effective가 다른 것은 오류가 아니다. conditional default, computed value, unset, availability가 이런 차이를 만들 수 있다.

## Transactional Profile Edit

Profile editor는 source TOML을 즉시 바꾸지 않는다. 각 edit는 memory의 mutable profile transaction에만 추가된다. V2 resolver는 profile writer 권한으로 그 transaction을 현재 selected profile chain보다 높은 preview priority에 놓고 `confit_v2_snapshot_reconcile_edit()`를 호출한다. 이 함수가 계산하는 affected set은 incremental boundary이며 publish되는 snapshot은 반드시 full resolve와 동일하다.

편집 정책은 다음과 같다.

- bool: 즉시 toggle.
- tristate: `n`/`m`/`y` popup.
- enum: schema candidate만 선택하는 popup.
- int, uint, hex, float, string, path, string_list, path_list, enum_set: value dialog. collection은 TOML array literal로 입력한다. 예: `["src", "include"]`.
- range, finite float, enum candidate, path policy, availability, choice, named constraint 실패는 dialog를 닫지 않고 그 dialog 안에서 오류를 보인다.

profile-domain option만 저장 가능한 edit 대상이다. computed/schema/target domain, hidden, unavailable option은 resolver diagnostic과 함께 거부한다. 이 규칙을 TUI 자체에서 재구현하지 않는다.

## Save 보장

`s` 또는 dirty exit의 Save는 다음 순서다.

1. transaction 전체를 full V2 resolve한다.
2. complete constraint validation과 V2 artifact input serialization을 실행한다.
3. 기존 leaf profile TOML을 parser adapter로 읽어 `[profile]`, sparse `[values]`, `[unset]`을 canonical TOML로 재직렬화한다. transaction이 고친 id만 바꾼다.
4. candidate TOML을 memory에서 다시 parse한다.
5. 위 단계가 모두 성공한 경우에만 host atomic write-if-changed로 leaf profile을 replace한다.

따라서 validation 또는 TOML serialization이 실패하면 원본 profile file은 변경하지 않는다. full resolved snapshot 전체를 profile에 덮어쓰지 않으며, hidden binary database도 만들지 않는다. 저장 뒤 `confit check --project ... --profile ...`를 CI 또는 review hook에서 다시 실행하는 것을 권장한다.

## Schema Edit Guard

`confit tui --schema-edit`를 V2 project에 실행하면 화면 상단에 항상 `SCHEMA EDIT MODE - guarded`가 표시된다. 이 frontend는 V2 profile transaction을 안전하게 지원하지만 V2 schema source mutation은 아직 TUI에서 persist하지 않는다. V2 schema 자체는 reviewed TOML edit와 다음 검증을 사용한다.

```sh
confit check --project config/my-project --strict
confit graph --project config/my-project
```

이는 profile value authority와 schema authoring authority를 혼동하지 않기 위한 의도적인 경계다. V2 schema editor가 source mutation을 추가하는 경우에도 같은 link/compile/constraint/full-transaction validation과 atomic TOML write를 만족해야 한다.
