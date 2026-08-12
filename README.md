# Confit

Confit은 Parus의 configure-time 선택 compiler다. Config v4의 option, constraint,
profile, target, selection과 product binding을 bounded하게 해석하고 immutable generation을
출판한다. Makefile, C source 목록, link 순서, test 목록, generator action과 Five-GEN action
graph는 해석하거나 소유하지 않는다.

Production 흐름은 `confit configure` 한 번과 이후의 `bmake` 소비 단계로 분리된다.
설정 입력이 바뀌지 않은 ordinary build에서는 resolver를 다시 실행하지 않는다. Config v2,
selectable component schema v3, build policy 생성기와 호환 reader는 제공하지 않는다.

독립 검증:

```text
bmake CONFIT_OBJROOT=/absolute/output \
  CONFIT_BMAKE_TOOL=/absolute/bmake \
  CONFIT_HOST_CC=/absolute/c-compiler check-host
```

현재 TUI는 구현하지 않는다. 다만 generation의 Preview, Cancel, Apply transaction과 option
provenance는 향후 TUI가 같은 semantic model을 소비할 수 있도록 C API로 유지한다.
