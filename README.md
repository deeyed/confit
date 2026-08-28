# Confit

Confit은 TOML로 작성한 명시적 configuration graph와 작은 사용자 값 파일을
해석하여, typed configuration data를 결정론적으로 확정하고 immutable snapshot으로
출판하는 generic configuration tool이다. Confit은 build를 실행하거나 project source,
Makefile, compiler invocation, link graph를 분석하지 않는다.

이 checkout의 `codex/confit-v6` 브랜치는 schema 6 개발선이다. 현재 브랜치가 따라야 할
정본 계약은 다음 두 문서다.

- [Schema 6 configuration contract](docs/config-v6.md)
- [Schema 6 architecture and security contract](docs/architecture-v6.md)
- [Schema 6 bootstrap contract](docs/bootstrap-v6.md)

## 현재 구현 상태

Schema 6는 22개 검증 라운드로 구현한다. R01은 위 계약을 고정했고 R02는 분기
기준점의 schema 5 parser, workflow, generator와 consumer-specific host capability를
제거했다. 현재 binary는 `help`와 `--version`만 성공하는 development skeleton이다.
Configuration command는 어떤 project 입력도 열지 않고 usage error로 종료한다.

따라서 이 문서는 다음을 주장하지 않는다.

- schema 6 parser, resolver, snapshot writer 또는 TUI가 이미 구현됨
- 기존 schema 5 configuration의 compatibility 또는 migration
- generic project의 build 성공이 Confit에 의해 검증됨
- schema 6 release candidate가 완성됨

## 목표 사용자 흐름

Schema 6가 구현되면 configuration과 ordinary build는 명시적으로 분리된다.

```text
bmake menuconfig
bmake all
```

또는 비대화형으로 다음처럼 사용한다.

```text
bmake configure
bmake all
```

`all`은 configuration이 없거나 stale할 때 configure를 암묵 실행하지 않는다.
Project의 Makefile은 `confit verify`로 선택된 value artifact를 검증한 뒤 소비한다.
Confit은 그 Makefile이나 project source를 읽지 않는다.

## Bootstrap 경계

제품과 필수 C test binary의 목표 bootstrap dependency는 provisioned clang toolchain,
bmake, shell, 이미 빌드된 Confit, 그리고 clang으로 빌드한 Confit-owned C binary뿐이다.
Python, CMake, Ninja, ncurses, parser generator와 외부 schema processor는 mandatory
build 또는 runtime dependency가 아니다. 정확한 claim과 non-claim은 architecture
contract와 bootstrap contract에 기록한다. Build target은 이미 존재하는 object root를
요구하며 외부 `mkdir`, `rm`, source/test discovery를 실행하지 않는다. Destructive clean
target 대신 fresh pre-created object root를 사용한다.
