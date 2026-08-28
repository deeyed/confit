---
document: confit-schema-v6-types
status: implemented-r09
authority: config-v6-and-model-v6
schema_version: 6
---

# Confit v6 typed declaration semantics

## 1. 구현 경계

R09는 reachable schema declaration을 `bool`, `int`, `hex`, `string`, `enum`
중 하나로 검증하고 generic `ConfitCatalog`에 owned typed declaration으로 게시한다.
이 단계는 dependency expression을 해석하거나 user assignment를 declaration에 연결하거나
resolved value를 만들지 않는다. Build source, Makefile, compiler, object, link graph도 읽거나
추론하지 않는다.

Schema loader는 R07/R08이 이미 소유한 TOML byte image만 사용한다. 특히 tomlc17이 모든
TOML integer를 `int64_t`로 정규화한 뒤에도 `hex`와 decimal spelling을 혼동하지 않도록,
integer datum의 recorded line/column에서 동일한 owned byte image의 token prefix를 bounded하게
확인한다. 이 과정은 path reopen, 두 번째 parse, filesystem scan을 수행하지 않는다.

## 2. 닫힌 type set과 default

허용되는 type spelling과 `default` 생략 의미는 다음과 같다.

| type | native TOML value | omitted default | optional constraint |
| --- | --- | --- | --- |
| `bool` | boolean | `false` | none |
| `int` | signed 64-bit integer | `0` | inclusive `range` |
| `hex` | native `0x...` integer | `0x0` | inclusive hex `range` |
| `string` | UTF-8 string | empty string | none |
| `enum` | string atom | forbidden | required `values` domain |

`tristate`, `placement`, `uint`, `float`, `path`, `file`, `directory`,
`object`, `target`, `driver`, `module`은 unknown type error다. Default의 TOML
표현을 보고 type을 추론하거나 다른 type으로 coercion하지 않는다.

`bool`은 TOML `true`/`false`만 받는다. Integer 0/1과 quoted boolean은 실패한다.

`int`는 TOML parser가 loss 없이 표현한 signed 64-bit integer만 받는다. Numeric string,
boolean, float는 실패하고 int64 범위를 벗어난 token은 TOML parse 단계에서 실패한다.

`hex`는 TOML `0x` spelling으로 입력된 nonnegative integer만 받는다. Decimal,
quoted hex, negative value, float는 실패한다. Semantic domain은 `0x0`에서
`0x7fff_ffff_ffff_ffff`까지이며 canonical core identity는 underscore를 제거한 lowercase
`hex:0x...`다. 생략 default `0x0`은 schema가 직접 만드는 typed value이므로 source token을
요구하지 않는다.

`string`은 최대 4,096 UTF-8 byte다. NUL, ESC, DEL, unsafe C0/C1 control은 실패한다.
Layout whitespace는 value data로 유지되며 향후 각 emitter가 자신의 grammar에 맞게 escape할
책임을 진다. Core type acceptance가 Make emitter 지원을 뜻하지는 않는다.

`enum`은 explicit `values`와 `default`가 모두 필요하다. Domain은 1–256개의 unique ASCII
atom으로 구성되고 atom은 1–128 byte, `[A-Za-z0-9_.+-]+`다. Object/label form, duplicate,
empty domain, domain 밖 default는 실패한다.

## 3. Field applicability

`values`는 enum에만 허용된다. bool/int/hex/string declaration에 `values`가 있으면 값의
모양이나 내용과 관계없이 error다.

`range`는 int와 hex에만 허용된다. Inline table은 정확히 `min`과 `max`를 가져야 한다.
두 endpoint는 declaration과 같은 native type이어야 하고 `min <= max`,
`min <= default <= max`를 모두 만족해야 한다. Diagnostic은 다음 원인을 구분한다.

- missing 또는 extra range field;
- min의 native type mismatch;
- max의 native type mismatch;
- min/max 역전;
- default의 inclusive range 이탈.

Range는 arithmetic, step, unit, expression, symbol reference를 갖지 않는다.

## 4. Ownership과 canonical identity

검증 중에는 default, range endpoint, enum domain을 private temporary value로 만든다. 모든
검증과 allocation이 성공한 뒤 `confit_catalog_add_config`가 symbol, prompt/help, typed
default, range, enum atoms, unparsed dependency text와 source span을 deep-copy한다. 실패한
project load는 catalog를 포함한 전체 candidate를 파기하며 partial typed declaration을
caller에게 공개하지 않는다.

R08의 raw config record는 R09 이후 제품 상태로 남지 않는다. Project config accessor는
catalog의 `ConfitConfigView`를 그대로 빌려 주며 별도의 raw/default candidate 표현이나 두
개의 declaration count를 유지하지 않는다.

Core canonical identity는 emitter syntax가 아니다.

```text
bool:false
int:-12
hex:0x10e8
string:5:value
enum:6:normal
```

Equality는 kind를 먼저 비교하므로 같은 numeric magnitude를 가진 int와 hex, 같은 bytes를
가진 string과 enum은 서로 같지 않다. Numeric formatting은 locale에 의존하는 stdio 변환을
사용하지 않고 fixed ASCII digit table로 생성한다.

## 5. 실행된 경계와 non-claim

R09 focused corpus는 omitted/explicit default, int64 min/max/overflow, hex zero/max/decimal/
quoted/negative/overflow, exact/one-over string limit, range shape/type/order/default,
enum empty/duplicate/bad atom/non-member/exact/one-over count와 atom limit, 모든 excluded type,
field applicability, canonical format을 포함한다. TOML fuzz seed에는 native type boundary와
overflow input을 추가한다.

이 증거는 실행된 host와 corpus에서 declaration type semantics와 cleanup을 뒷받침한다.
다음은 아직 주장하지 않는다.

- dependency expression의 parse, type check, cycle detection 또는 availability;
- user configuration의 symbol linking, type validation 또는 precedence;
- resolution, savedefconfig, snapshot, emitter 또는 configuration CLI;
- arbitrary locale/host 조합에서의 완전한 portability;
- consumer build 또는 source membership의 정확성;
- schema 5 compatibility 또는 consumer migration.
