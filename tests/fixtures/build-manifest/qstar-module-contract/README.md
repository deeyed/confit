# QStar Module Contract Fixture

이 fixture는 Confit build manifest 계약을 고정하기 위한 초안이다. 현재
generator output의 golden 파일이 아니라, 이후 generator 라운드가 맞춰야 할
QStar import 형태를 보여준다.

핵심 규칙:

- canonical QStar manifest는 `generated/config/config.qsm`이다.
- QStar에서는 `qstar.import_module("generated/config")`로 읽는다.
- `qstar.import_module(...)`에는 `.qsm` file path를 직접 넘기지 않는다.
- `.qsm` file은 table을 반환하고 graph declaration을 하지 않는다.
- 기존 `config.qst` 형태의 table manifest는 compatibility artifact로만 본다.

Delos-specific build selection은 project-specific module로 분리한다.

```lua
local config = qstar.import_module("generated/config")
local selection = qstar.import_module("generated/delos_build_selection")
```

`selection.board.objects` 같은 object label은 `qstar.config`가 아니라 실제
target-local field에서 소비해야 한다. `qstar.config`는 compile/link option
bundle이며 source/object graph를 직접 만들지 않는다.
