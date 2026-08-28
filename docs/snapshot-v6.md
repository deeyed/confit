# Schema 6 immutable selected snapshots

이 문서는 R14에서 구현한 generic snapshot publication과 exact-input verify 경계를 설명한다.
Snapshot은 configuration 결과의 불변 data bundle이며 build rule, source membership, compiler
명령 또는 consumer-specific 의미를 담지 않는다. 이 계층은 project directory를 열거하지 않고
resolver를 verify 경로에서 다시 실행하지 않는다.

## 1. Public model

`include/confit/snapshot.h`는 두 작업만 노출한다.

- `confit_snapshot_publish()`는 이미 검증된 `ConfitSchemaProject`, optional
  `ConfitUserConfig`, 그 catalog의 immutable `ConfitResolution`과 inert optional data
  artifact를 받아 하나의 snapshot을 출판한다.
- `confit_snapshot_verify()`는 explicit project/output root와 expected entry path를 받아
  현재 `selected`가 가리키는 snapshot 및 manifest-listed input만 검증한다.

Publication request의 project catalog와 resolution catalog는 pointer identity가 같아야 한다.
User configuration이 있으면 explicit assignment와 resolution의 `user` origin/effective value도
exact하게 일치해야 한다. 따라서 다른 user file을 manifest에 기록하면서 unrelated resolution을
출판하는 요청은 I/O를 시작하기 전에 거부된다.

Optional artifact는 다음 field만 가진 inert byte record다.

| Field | 의미 |
| --- | --- |
| `role` | bounded lowercase generic data role |
| `name` | slash-free bounded snapshot leaf |
| `bytes`, `size` | 실행하거나 해석하지 않는 exact byte image |
| `printable` | successful verify 뒤 path를 반환할 수 있는지 여부 |

Optional artifact는 required core name을 대체할 수 없다. R14 자체는 Make/C emitter를 구현하지
않는다. R15가 같은 slot에 안전하게 직렬화한 value artifact를 전달한다. Optional bytes는
command, callback, plugin 또는 build dependency가 아니다.

## 2. Logical layout and activation

Output root는 다음 Confit-owned 구조를 가진다.

```text
output/
├── .confit-snapshot.lock
├── snapshots/
│   └── <lowercase-sha256>/
│       ├── inputs.manifest
│       ├── provenance.json
│       ├── resolved-values.json
│       ├── user-values.toml
│       ├── <requested optional data>
│       └── snapshot.seal
└── selected
```

`selected`만 activation point다. 정확히 64자의 lowercase SHA-256과 newline을 담는 regular
file이며 symlink, directory 또는 다른 file type은 거부된다. `snapshots/<digest>`가 완전하고
검증 가능해도 `selected`가 그 digest를 가리키지 않으면 active configuration이 아니다. 이는
publication 실패 뒤 남을 수 있는 complete orphan과 이전 selected를 명확히 구분한다.

Snapshot directory와 file은 publication 시 0555/0444 mode로 닫는다. 이 mode는 same-identity
owner에 대한 cryptographic immutability가 아니다. Create-only name, exact digest, seal 검증과
selected atomic replacement가 protocol authority다.

## 3. Canonical core artifacts

### 3.1 `user-values.toml`

R13 shared serializer가 만든 schema 6 minimal `[values]` document다. Default와 같은 filler는
없고 lexical symbol order와 native TOML type을 보존한다. Source user file을 복사하거나
overwrite하지 않는다.

### 3.2 `resolved-values.json`

모든 resolved symbol을 lexical order로 기록한다. 각 record는 `symbol`, `type`, effective
`value`, declaration `default`, `origin`, `available`을 갖는다. Bool과 int는 JSON native scalar,
hex는 type identity가 보이는 lowercase quoted `0x...`, string/enum은 independent JSON escaping을
사용한다. 이 required core JSON은 review/identity data다. R14의
`resolved_values_printable = false`이면 seal에는 존재하지만 artifact-path output은 거부된다.
R15의 explicit JSON emitter request가 같은 core role을 consumer projection으로 표시하며 두 번째
JSON file을 만들지는 않는다.

### 3.3 `inputs.manifest`

Manifest는 private closed line protocol이다.

```text
confit-inputs-v1
entry<TAB><size><TAB><sha256><TAB><relative-path>
fragment<TAB><size><TAB><sha256><TAB><relative-path>
user<TAB><size><TAB><sha256><TAB><relative-path>
```

첫 record는 caller-selected entry이고 다음 record는 source graph DFS presentation order의
reachable fragment다. Explicit user configuration이 있을 때만 마지막 `user` record가 존재한다.
Path는 normalized project-root-relative TOML path다. Absolute project root, inode, timestamp와
host location은 semantic digest에 들어가지 않는다. 동일 relative inputs와 values를 다른
absolute project root에서 검증할 수 있다.

Manifest 생성은 R06 input image의 already-owned bytes, size와 SHA-256을 사용한다. Publication
중 project path를 다시 열어 hash하지 않는다. Definition graph와 user input의 combined byte
ceiling도 publication 전에 검사한다.

### 3.4 `provenance.json`

`confit-provenance-v1`, schema version 6과 Confit build/version identity를 기록한다. Project의
absolute host path, Git state, environment selector, compiler 또는 consumer identity는 기록하지
않으며 snapshot semantic identity를 host location에 결속하지 않는다.

### 3.5 `snapshot.seal`

Seal은 다음 private closed line protocol이다.

```text
confit-snapshot-seal-v1
<role><TAB><name><TAB><printable-0-or-1><TAB><size><TAB><sha256>
```

Entry는 artifact name lexical order이며 required/optional artifact 각각의 role, name, byte size,
digest와 path-output eligibility를 결속한다. `snapshot.seal`은 자기 자신을 line으로 포함하지
않는다. Snapshot directory name이 seal exact bytes의 SHA-256이므로 seal 자체는 content address로
직접 결속되고, seal은 나머지 모든 member를 결속한다. 이 구조는 self-hash cycle을 만들지 않는다.

Public snapshot ceiling은 artifact 64개, artifact name 128 bytes, role 64 bytes, seal을 포함한
snapshot bytes 합계 64 MiB다. 초과는 truncation이 아니라 deterministic failure다.

## 4. Publication protocol

Publication은 다음 순서로 실행된다.

1. required/optional artifact와 canonical seal을 전부 memory에서 완성한다.
2. output root의 no-follow regular `.confit-snapshot.lock`에 `fcntl` write lock을 잡는다.
3. `snapshots` directory를 descriptor-rooted no-follow 방식으로 열거나 만든다.
4. process/counter name의 private directory를 `mkdirat` create-only로 만든다.
5. 각 file을 `O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`로 만들고 완전 write, mode 설정,
   `fsync`, reopen, identity/size/byte comparison한다.
6. `snapshot.seal`까지 같은 방식으로 검증한 뒤 directory mode와 directory `fsync`를 닫는다.
7. candidate 경로를 exact하게 다시 읽어 expected bytes와 비교한다.
8. seal digest 이름으로 create-only directory publish를 시도한다. Tier-1 Darwin은
   `renameatx_np(..., RENAME_EXCL)`로 existing directory를 교체하지 않는다.
9. 같은 digest directory가 이미 있으면 candidate만 제거하고 existing seal/member를 expected
   bytes와 exact 비교한 경우에만 reuse한다.
10. final directory를 다시 검증한다.
11. digest plus newline regular candidate를 R05 atomic replace primitive로 `selected`에 publish한다.
12. output parent sync를 완료하고 lock을 해제한다.

Private transaction cleanup은 directory enumeration을 사용하지 않는다. Transaction이 실제로
create한 bounded leaf 목록만 역순으로 `unlinkat`하고 자기 candidate directory만 제거한다.
Unrelated output path와 pre-existing directory를 cleanup 대상으로 삼지 않는다.

표준 POSIX `renameat()`만 있는 host에서는 사전 존재 검사와 rename 사이의 race 없이 directory를
atomic no-replace할 수 없다. 그런 host의 R14 publisher는 안전하지 않은 교체 fallback을 사용하지
않고 `CONFIT_ERR_IO`로 fail-closed한다. 새 host backend는 atomic no-replace primitive와 동일한
회귀 증거를 제공한 뒤에만 publication 지원을 선언할 수 있다.

Failure가 selected replacement보다 앞이면 old selected는 변하지 않는다. Directory publish 뒤
failure는 complete orphan을 남길 수 있지만 active state는 바뀌지 않는다. 동일 digest의 다음
publication은 orphan을 exact verify한 뒤 재사용할 수 있다. Selected replacement 뒤 parent sync나
lock release가 실패하면 operation은 I/O failure를 보고할 수 있으나 mixed bundle을 활성화하지는
않는다.

## 5. Verify protocol

Verify request는 project root, output root와 expected entry relative path를 명시한다. 실행 순서는
다음으로 닫힌다.

1. `selected` exact regular bytes를 bounded read한다.
2. `snapshots/<digest>/snapshot.seal` exact path만 열고 seal SHA-256이 selected와 같은지 확인한다.
3. Seal을 bounded parser로 읽고 required core role, name order, size와 digest를 검사한다.
4. Seal에 열거된 exact artifact path만 reopen/hash한다.
5. `inputs.manifest`를 bounded parser로 읽는다.
6. 첫 entry가 caller의 expected entry와 같은지 확인한다.
7. Manifest에 열거된 relative TOML path만 project root에서 no-follow reopen/read/hash한다.
8. 모두 성공한 뒤에만 requested printable artifact의 output-root-relative path를 반환한다.

`inputs.manifest`의 artifact digest 검증과 manifest field 해석은 반드시 같은 한 번의
read에서 얻은 byte image를 사용한다. 검증 뒤 manifest를 다시 열어 경로를 해석하지 않으므로,
두 read 사이의 파일 교체가 봉인되지 않은 project path를 주입할 수 없다.

Verify는 다음을 하지 않는다.

- schema project 또는 catalog 생성;
- TOML schema parse;
- dependency expression link/evaluate;
- resolver 실행;
- current filesystem에서 새 fragment 검색;
- directory enumeration, glob 또는 recursive walk;
- C source, header, Makefile, object 또는 build artifact 검사;
- manifest 밖 poison file open;
- consumer build 실행.

Manifest-listed definition/user bytes가 바뀌면 stale error다. Manifest 밖 invalid TOML, C source와
Makefile이 바뀌어도 verify I/O ledger에 나타나지 않으며 result에 영향을 주지 않는다.
`--print-artifact`의 canonical absolute path 조립과 stdout one-line contract는 R16 CLI가 이 API의
verified relative path와 explicit output absolute path를 결합해 소유한다.

## 6. Security and evidence boundary

R14 integration corpus는 다음을 직접 검증한다.

- first publication, selected digest와 independent seal digest;
- identical snapshot exact reuse;
- changed typed value가 다른 content address를 만듦;
- lock/candidate/files/directory publication/selected 직전 failure에서 old selected 보존;
- complete orphan이 inactive이고 다음 exact publication에서만 선택됨;
- selected symlink와 directory rejection 및 symlink victim non-mutation;
- corrupt existing digest directory rejection;
- two process publisher에서 one complete selected state;
- manifest-listed stale input rejection;
- unlisted invalid TOML/C/Makefile non-observation;
- exact entry/fragment/user read ledger;
- unknown, missing, non-printable artifact path rejection;
- identical relative tree의 absolute-location relocation verify;
- optional artifact가 core role을 대체하지 못함.

이 evidence는 실행한 local filesystem transaction과 bounded corpus에 한정된다. SHA-256은
signature나 authenticity가 아니며 malicious same-identity peer를 trust boundary 밖으로 제거하지
않는다. `fsync`/rename protocol은 모든 filesystem, hardware cache와 arbitrary power-loss model의
certification이 아니다. R14는 Make/C emitter, conventional CLI, migration workflow, TUI,
consumer build, boot, emulation 또는 hardware evidence를 제공하지 않는다.
