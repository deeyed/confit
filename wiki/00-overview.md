# 00. Confit 개요

## Confit이 하는 일

Confit은 build 전에 실행되는 host-side configuration tool이다. Runtime service가 아니고, Delos나 Parus
실행 이미지 안에 들어가는 라이브러리도 아니다.

Confit은 다음 질문에 답한다.

```text
이 profile로 build하면 어떤 compile-time option 값이 되는가?
그 값은 default, target, profile, user override 중 어디서 왔는가?
서로 충돌하는 option 조합은 없는가?
Parus와 Delos의 설정은 서로 호환되는가?
C source와 build system이 읽을 generated artifact는 무엇인가?
```

Confit의 입력은 사람이 관리하는 TOML 파일이다.

```text
config/
  project.toml
  options/
  profiles/
  targets/
  compat/
```

Confit의 출력은 build tree나 명시한 output directory 아래에 생성되는 artifact다.

```text
config.h
config.report.json
config.explain.txt
config.graph.json
config.inputs.json
config.cmake
config/config.qsm
config.qst  # deprecated compatibility artifact
```

## 왜 kconfig를 그대로 쓰지 않는가

kconfig는 오래 검증된 훌륭한 설정 시스템이다. 특히 Linux kernel처럼 큰 menu tree, choice, help, search,
defconfig workflow가 필요한 환경에서는 강하다.

Confit은 kconfig의 범용 대체품이라기보다, Parus/Delos의 요구에 맞춘 configuration authority다.

Confit이 의도적으로 더 중요하게 보는 것은 다음이다.

- 두 프로젝트 사이의 compatibility check.
- 값의 provenance, 즉 값이 어디서 왔는지 추적하는 기능.
- generated input manifest와 deterministic output.
- TOML source-of-truth.
- host-only 단일 C executable.
- runtime image에 parser, TUI, solver를 넣지 않는 경계.
- `config.h`, report, CMake/QStar 보조 artifact를 한 번에 생성하는 workflow.

## 기본 용어

`project`

: Confit이 읽는 하나의 설정 단위다. Delos용 project, Parus용 project처럼 나뉠 수 있다.

`option`

: 하나의 설정 항목이다. 예를 들면 `delos.debug.dsh`, `parus.boot.direct_dtb` 같은 id를 가진다.

`schema`

: option의 type, default, prompt, help, category, tags, range, dependency를 정의하는 TOML이다.

`profile`

: 특정 build 목적을 위한 option 값 묶음이다. 예를 들면 `sim-dsh`, `debug`, `qemu-aarch64` 같은 이름을
  가진다.

`target`

: board, arch, output root처럼 대상 platform이나 build target에 가까운 값을 제공한다.

`resolve`

: default, base profile, target, selected profile, user override를 정해진 순서로 합쳐 최종 option 값을
  만드는 과정이다.

`compat`

: Parus와 Delos처럼 서로 맞아야 하는 project들의 resolved value를 함께 검사하는 과정이다.

`generated artifact`

: Confit이 만들어 주는 결과 파일이다. C source는 보통 `config.h`를 include하고, build system은 필요하면
  `config.cmake`나 QStar용 `config/config.qsm`을 명시적으로 읽는다. 기존 `config.qst`는 compatibility
  artifact다.

## Confit이 하지 않는 일

Confit은 다음 일을 하지 않는다.

- Delos runtime service를 만들지 않는다.
- Parus/Delos firmware 안에서 TOML을 parse하지 않는다.
- 실제 Parus/Delos source tree를 몰래 수정하지 않는다.
- network에서 configuration을 가져오지 않는다.
- hidden binary DB를 만들지 않는다.
- CMake나 QStar 자체를 대체하지 않는다.

Confit은 “설정 원본을 읽고, 검증하고, 설명하고, generated artifact를 만드는 도구”다.
