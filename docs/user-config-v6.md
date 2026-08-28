# Schema 6 user configuration and minimal serialization

이 문서는 schema 6의 사용자 값 파일을 typed assignment로 연결하고 다시 최소 TOML로
직렬화하는 R13 계층을 설명한다. 이 계층은 snapshot publisher, CLI command dispatcher,
TUI 또는 build integration이 아니다. Project source, Makefile, C source와 filesystem
directory를 탐색하지 않는다.

## 1. 닫힌 입력 형식

사용자 파일의 유일한 top-level key는 다음 두 개다.

```toml
schema_version = 6

[values]
ENABLE_LOGGING = true
WORKER_COUNT = 8
DEVICE_ID = 0x10e8
LOG_LEVEL = "normal"
```

`schema_version`은 필수 integer 6이다. `[values]`는 생략할 수 있고, 생략 또는 empty
table은 assignment가 없는 all-default intent를 나타낸다. 다른 table, array-of-table,
top-level key, invalid symbol과 duplicate TOML key는 structural loader에서 fail-closed한다.

`confit_user_config_load_relative()`는 caller가 명시한 project-root-relative TOML path만
한 번 읽는다. R08 `ConfitUserDocument`와 같은 exact byte image를 소유하고 각 key를 이미
loaded된 generic catalog의 exact symbol에 연결한다. Unknown과 removed symbol은 둘 다 stale
assignment error다. Alias, rename map, last-wins, profile merge와 ordered override는 없다.

각 value는 declaration type에 맞는 native TOML scalar여야 한다.

- `bool`: TOML `true` 또는 `false`
- `int`: TOML decimal signed integer
- `hex`: 같은 input image에서 확인한 nonnegative native `0x...` integer
- `string`: bounded safe UTF-8 TOML string
- `enum`: declaration domain 검증 전에 exact string atom으로 변환되는 TOML string

String-to-number, `0`/`1`-to-bool, quoted hexadecimal coercion은 없다. Loader가 만든
`ConfitAssignment` set은 lexical symbol order로 저장되지만 그 order가 precedence를 만들지
않는다. Range, enum domain과 dependency availability의 최종 authority는 같은 assignment
set을 받는 deterministic resolver다.

## 2. 성공한 resolution만 직렬화하는 이유

공유 serializer는 raw assignment나 TOML document를 직접 받지 않고 성공한
`ConfitResolution`을 받는다. 따라서 다음 invariant가 serializer 앞에서 이미 성립한다.

- 모든 symbol이 exact catalog member다.
- assignment kind, range와 enum domain이 유효하다.
- duplicate writer가 없다.
- unavailable option의 non-default user value가 없다.
- 각 value origin은 `default` 또는 `user` 하나다.

Minimal output은 lexical symbol order로 resolved record를 읽되 `origin == user`이고
effective value가 declaration default와 다른 경우만 쓴다. Explicit default-equal assignment는
valid input이지만 output filler가 아니므로 생략한다. Serializer는 effective build state를
새 authority로 만들지 않고 검증된 user intent의 최소 형태만 보존한다.

출력은 항상 다음 stable shape를 갖는다.

```toml
schema_version = 6

[values]
```

All-default configuration도 `[values]`를 쓴다. Bool은 lowercase, int는 canonical decimal,
hex는 lowercase `0x...`, enum과 string은 TOML basic string으로 출력한다. Quote, backslash,
tab, newline과 carriage return은 target grammar에 맞게 escape하고 UTF-8 non-ASCII text는
그 byte image를 보존한다. Source spelling, declaration order와 input assignment order는 output
identity를 바꾸지 않는다.

## 3. Memory와 explicit destination API

`confit_user_config_format_minimal()`이 CLI와 TUI가 공유할 유일한 memory serializer다.
Null buffer와 zero size는 exact output-size query이고, 작은 caller buffer는 byte 하나도
변경하지 않은 채 usage error다. 두 번째 pass만 실제 buffer를 채우므로 partial output은
성공 결과로 노출되지 않는다.

Serialized file은 다시 R06 input loader가 읽을 수 있어야 하므로 1 MiB one-TOML-file ceiling을
그대로 적용한다. 많은 maximum-length string assignment가 output을 이 ceiling 너머로 만들면
truncation하거나 unreadable file을 쓰지 않고 size preflight에서 실패한다.

`confit_user_config_write_minimal()`만 explicit destination을 바꾼다. Caller가 전달한 output-root
capability와 normalized relative path에 R05 `confit_host_atomic_replace()`를 적용한다. Loading,
resolution, size query와 memory formatting은 source user file을 수정하지 않는다. Destination
path도 `.toml`이어야 한다. Symlink, unsafe leaf 또는 write/publish failure는 partial user
file을 노출하는 fallback으로 이어지지 않으며 publication은 old 또는 complete-new image다.

R13은 `savedefconfig` CLI command를 등록하지 않는다. Current selected snapshot에서 어떤
resolution을 선택하고 `--destination`을 해석할지는 R17이 이 shared API 위에서 구현한다.
Menuconfig save는 R14 immutable snapshot transaction을 사용하며 source user file을 암묵적으로
overwrite하지 않는다.

## 4. 소유권과 non-claims

`ConfitUserConfig`는 exact input document와 copied typed assignments를 소유한다. Accessor가
반환한 input과 assignment pointer는 config destroy까지만 유효하다. Resolver는 assignment를
자기 candidate로 복사하므로 성공한 resolution을 만든 뒤 user config를 해제할 수 있다.
Resolution은 계속 catalog를 빌리므로 project/catalog가 resolution보다 오래 살아야 한다.

R13 test는 다섯 type native input, omitted/empty/default-equal intent, unknown/stale와 wrong type,
range 및 unavailable rejection, lexical determinism, TOML escaping, lowercase hex, source
non-mutation, explicit atomic write, parse-resolve-serialize-reparse와 output-size ceiling을 검증한다.
이 evidence는 immutable multi-file snapshot, emitter, CLI/TUI UX, ordinary project build,
consumer migration, boot, emulation 또는 hardware 동작을 증명하지 않는다.
