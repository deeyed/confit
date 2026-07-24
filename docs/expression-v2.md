---
doc_type: language-spec
status: accepted-design
authority: normative
implementation_status: partially-implemented
last_verified: 2026-07-24
---

# Confit V2 Expression Language

이 문서는 `schema_version = 2`의 expression syntax, type system, evaluation
규칙을 정의한다. Expression은 TOML string 안에 작성하지만 TOML parser와 별도의
Confit expression lexer/parser가 typed AST로 compile한다.

## 기본 예

```toml
available_if = 'delos.target.arch == "armv7m" && enabled(delos.driver.uart)'
visible_if = 'enabled(delos.debug.console)'
computed = 'delos.memory.page_size * delos.memory.page_count'
```

Expression은 runtime code가 아니다. Confit host tool이 build 전에 평가한다.

현재 구현은 lexer/parser, source-local AST span, explicit binding environment를
받는 static typecheck와 pure evaluator를 제공한다. Symbol linking과 evaluation DAG
ordering은 후속 단계가 소유한다. parser는 유효한 identifier나 function name을
semantic하게 승인하지 않으며, typecheck가 binding environment를 기준으로 승인한다.

## 구현 경계

현재 C API의 `ConfitV2ExpressionEnvironment`는 option id, declared expression
type, effective value를 명시적으로 받는다. Typecheck는 이 environment의 type만
읽고 AST node별 type table을 만든다. Evaluator는 같은 canonical binding을 받아
값을 deep-copy하여 계산한다. 따라서 이 단계는 filesystem, 환경 변수, clock,
terminal locale 또는 전역 mutable state를 읽지 않는다.

다음 symbol linker 단계는 project의 canonical symbol table을 이 environment로
변환한다. 이 분리는 expression module이 import traversal, profile ownership,
evaluation DAG 순서를 임의로 결정하지 않게 한다.

## Lexical 규칙

Option reference는 dot-separated ASCII id다.

```text
delos.debug.ddc
parus.boot.direct_dtb
delos.target.arch
```

각 segment는 소문자 ASCII letter로 시작하며 소문자 letter, digit, underscore를
가질 수 있다. 대문자, 공백, slash, dash를 option id에 허용하지 않는다.

지원 literal:

```text
true false
n m y
123 -123
0x1000
1.25
"UTF-8 string"
["armv7m", "aarch64"]
```

`n`, `m`, `y`는 string이 아니라 tristate literal이다.

## Grammar

아래 grammar는 의미를 설명하기 위한 EBNF다.

```text
expression     = conditional ;
conditional    = logical_or [ "?" expression ":" expression ] ;
logical_or     = logical_and { "||" logical_and } ;
logical_and    = equality { "&&" equality } ;
equality       = relation { ( "==" | "!=" ) relation } ;
relation       = additive
                 [ ( "<" | "<=" | ">" | ">=" | "in" ) additive ] ;
additive       = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative = unary { ( "*" | "/" | "%" ) unary } ;
unary          = ( "!" | "-" | "+" ) unary | primary ;
primary        = literal
                 | option_reference
                 | function_call
                 | list_literal
                 | "(" expression ")" ;
function_call  = identifier "(" [ expression { "," expression } ] ")" ;
list_literal   = "[" [ expression { "," expression } ] "]" ;
```

`? :`의 true/false branch는 같은 static type이어야 한다.

## Static Type

V2 expression type은 다음과 같다.

```text
bool
tristate
int
uint
float
string
enum<id>
path
list<string>
list<path>
set<enum<id>>
```

`unset`은 static type이 아니라 optional option의 runtime value state다. Reference는
선언된 static type을 유지하며, `defined()` 이외의 연산에서 unset value를 읽으면
evaluation error다.

서로 다른 type 사이의 암묵 변환은 없다.

- bool과 tristate는 자동 변환하지 않는다.
- int, uint, float는 자동 승격하지 않는다.
- enum과 string을 자동 비교하지 않는다.
- path와 string을 자동 결합하지 않는다.
- 숫자 변환 실패 시 string 비교로 fallback하지 않는다.
- non-bool을 `&&`, `||`, `!`에 넣으면 compile error다.

명시적 변환 함수는 v2.0에 제공하지 않는다. Schema 작성자는 option type을
일치시켜야 한다.

## Tristate

Tristate는 `n`, `m`, `y` 세 값을 가진다.

Expression에서 tristate 자체를 bool 문맥에 넣지 않는다. 다음 함수를 사용한다.

```text
enabled(x) -> x == m 또는 x == y
builtin(x) -> x == y
module(x)  -> x == m
```

Kconfig의 `&& = min`, `|| = max`, `! = 2 - value` 같은 암묵 tristate 논리는
채택하지 않는다. Tristate 조합이 필요하면 명시적으로 비교하거나 조건식을
사용한다.

```toml
computed = 'enabled(driver.usb) ? y : n'
```

## Operator 규칙

### Boolean

`&&`, `||`, `!`는 bool에만 적용한다. Evaluation은 left-to-right short circuit다.

### Equality

`==`, `!=`는 양쪽 type이 같아야 한다. 같은 enum declaration에 속한 enum만
비교할 수 있다.

### Ordering

`<`, `<=`, `>`, `>=`는 다음 type에만 적용한다.

```text
int
uint
float
string
path
```

String과 path 비교는 UTF-8 byte sequence의 lexical order다. Unicode
normalization과 locale collation은 수행하지 않는다.

Tristate ordering이 필요하면 `n`, `m`, `y`와 equality를 사용한다. Enum declaration
순서를 크기 비교 의미로 사용하지 않는다.

### Membership

`value in list`에서 list element type은 value type과 같아야 한다.

```toml
available_if = 'delos.target.arch in ["armv7m", "aarch64"]'
```

### Arithmetic

`+`, `-`, `*`, `/`, `%`는 같은 숫자 type끼리만 사용할 수 있다. Float에는 `%`를
허용하지 않는다.

- signed/unsigned overflow는 hard error다.
- 정수 0 division과 modulo는 hard error다.
- finite하지 않은 float 결과는 hard error다.
- float `-0.0`은 artifact serialization에서 `0.0`으로 canonicalize한다.

String `+`와 path `+`는 허용하지 않는다.

## Built-in Function

v2.0 built-in은 다음으로 제한한다.

| Function | Signature | 의미 |
|---|---|---|
| `enabled` | `tristate -> bool`, `bool -> bool` | 활성 여부 |
| `builtin` | `tristate -> bool` | `y` 여부 |
| `module` | `tristate -> bool` | `m` 여부 |
| `defined` | `option_reference -> bool` | unset이 아닌지 검사 |
| `len` | `string/list/set -> uint` | element 또는 byte 길이 |
| `contains` | `(list<T>, T) -> bool` | element 포함 여부 |
| `starts_with` | `(string, string) -> bool` | byte prefix |
| `ends_with` | `(string, string) -> bool` | byte suffix |
| `concat` | `(string, ...) -> string` | 명시적 문자열 연결 |
| `enum_name` | `enum<T> -> string` | enum의 canonical candidate text |

`defined`는 reference에만 적용한다. `len`의 string 길이는 Unicode scalar 수가
아니라 UTF-8 byte 수다.

`enum_name`은 명시적인 호환성/report key를 만들 때만 사용한다. String을 enum으로
역변환하는 함수는 제공하지 않는다.

`path_join`, `file_exists`, `glob`, `getenv`, `shell`, clock, random 함수는 제공하지
않는다. Host 상태가 resolution 결과를 바꾸면 reproducible build를 보장할 수
없기 때문이다.

## Reference Resolution

모든 option reference는 link 단계에서 존재해야 한다. Unknown reference는 hard
error이며 undefined symbol을 constant나 false로 암묵 생성하지 않는다.

Reference는 option의 effective value를 읽는다. Requested value를 expression에서
직접 읽는 기능은 제공하지 않는다. Requested/effective 차이는 constraint와
explanation engine이 구조적으로 다룬다.

## Unset

Default가 없고 required가 아닌 option은 unset일 수 있다. Unset value에 산술,
비교, string 함수를 적용하면 evaluation error다.

Schema가 unset을 허용하는 경우 먼저 `defined()`로 guard해야 한다.

```toml
visible_if = '!defined(debug.output_name) || debug.output_name != ""'
```

Short circuit 때문에 왼쪽 guard가 false인 branch는 평가하지 않는다.

## Expression 사용 위치

| 위치 | 기대 type | 값 생산 여부 |
|---|---|---|
| `available_if` | bool | 아니오 |
| `visible_if` | bool | 아니오 |
| conditional default `when` | bool | default 선택 |
| `computed` | option type | 예 |
| choice default `when` | bool | choice default 선택 |
| constraint `when` | bool | 아니오 |
| constraint `require` | bool | 아니오 |
| suggestion `when` | bool | 아니오 |
| suggestion `value` | option type | UI 제안만 |

## Conditional Default

Conditional default는 declaration order가 아니라 numeric priority로 결정한다.

```toml
[[option."scheduler.tick_hz".defaults]]
when = 'delos.target.kind == "sim"'
value = 1000
priority = 100

[[option."scheduler.tick_hz".defaults]]
when = 'delos.target.kind == "hardware"'
value = 100
priority = 100
```

참인 default 중 가장 높은 priority를 선택한다. 같은 최고 priority에서 서로 다른
값이 나오면 ambiguity error다. 같은 값이면 중복 warning을 낼 수 있다.

## Computed Expression

`write_domain = "computed"` option은 정확히 하나의 `computed` expression을
가진다.

```toml
[option."delos.memory.total_bytes"]
type = "uint"
write_domain = "computed"
computed = 'delos.memory.page_size * delos.memory.page_count'
```

Computed expression dependency는 evaluation DAG에 들어간다. Self-reference와
cycle은 source span을 포함한 hard error다.

## Source Span

모든 AST node는 다음 위치를 보존한다.

```text
source file
TOML key line/column
expression 내부 start/end byte offset
```

Type error와 evaluation error는 expression 전체가 아니라 문제가 된 operand와
reference 위치를 가리켜야 한다.

## Determinism

- Evaluation order는 import order나 hash iteration order에 의존하지 않는다.
- 같은 priority tie는 조용히 첫 항목을 선택하지 않는다.
- Float parse/format은 locale-independent다.
- String 비교는 bytewise다.
- Environment와 filesystem을 읽지 않는다.
- Diagnostic은 code, source path, line, column, option/constraint id 순서로
  deterministic하게 정렬한다.
