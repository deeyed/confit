---
doc_type: implementation-note
status: active
authority: r02-clean-schema6-line
depends_on:
  - docs/config-v6.md
  - docs/architecture-v6.md
---

# R02: clean schema 6 development line

R02는 이전 schema 구현을 호환 모드로 감추지 않고 제품의 공개 헤더, source
manifest, CLI dispatch와 test authority에서 함께 제거했다. 현재 binary는 schema 6
계약의 완성 구현이 아니라 다음 제한을 의도적으로 가진 development skeleton이다.

- `help`와 `--version`만 성공한다.
- 계약에 예약된 configuration command는 입력 파일을 열지 않고 usage error로
  종료한다.
- schema parser, resolver, emitter, snapshot publisher와 TUI는 아직 구현하지 않았다.
- 이전 schema를 받아들이는 parser, alias, converter 또는 dual dispatch는 없다.
- process 실행, directory enumeration과 consumer admission API는 제품에 없다.

이 라운드에서 보존한 구현은 consumer-neutral diagnostic/status, memory byte image를
입력으로 받는 bounded TOML adapter, SHA-256 byte digest뿐이다. TOML adapter는 파일을
직접 열지 않는다. File ownership과 descriptor-rooted I/O는 이후 전용 라운드가
정의하며, configuration membership은 literal `source` graph 구현 전까지 존재하지
않는다.

## Evidence boundary

R02 test는 다음 사실만 증명한다.

1. development binary와 public headers가 C17로 compile/link된다.
2. help에는 동결된 command 이름만 있고 제거된 command가 없다.
3. schema 5 입력을 지정해도 configuration command가 성공하거나 파일을 parse하지
   않는다.
4. TOML memory parser, diagnostic/status와 SHA-256 known-answer test가 통과한다.
5. explicit product manifest에 process, directory, 이전 schema/workflow/generator source가
   없다.

이 결과는 schema 6 parsing, resolution, snapshot publication, emitter 또는 TUI가
구현되었다는 증거가 아니다. Release version과 artifact ABI도 아직 확정하지 않는다.
