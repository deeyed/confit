# Schema 6 bounded dependency expressions

이 문서는 R11이 구현한 `depends_on` 컴파일·평가 계층의 소유권과 검증 증거를
기록한다. Public TOML surface는 `docs/config-v6.md`에 이미 고정된 `depends_on` 하나뿐이며,
이 구현은 새 field, alias, operator, build relation을 추가하지 않는다.

## 1. Closed language

문법은 다음으로 닫힌다.

```text
expression        := or-expression
or-expression     := and-expression ("||" and-expression)*
and-expression    := unary-expression ("&&" unary-expression)*
unary-expression  := "!" unary-expression
                   | "(" expression ")"
                   | SYMBOL
                   | SYMBOL "==" literal
                   | SYMBOL "!=" literal
literal           := true | false | integer | hexadecimal | quoted-string
```

`!`, `&&`, `||` 순으로 결합력이 낮아지고 괄호가 이를 덮어쓴다. Integer는 schema의
signed 64-bit decimal spelling, hexadecimal은 `0x` native spelling과 schema 6의
`INT64_MAX` 상한을 따른다. Quoted string은 TOML basic-string과 같은 double quote 및
escape spelling을 사용한다. Enum 비교도 quoted string으로 쓰지만 link 단계가 그 값을
enum atom으로 바꾸고 선언 domain membership을 검증한다. Symbol-to-symbol 비교, 단독
literal, 산술·관계·regex·함수·environment·file lookup은 문법 오류다.
Field 부재만 dependency 없음으로 해석하며 명시적인 empty string은 빈 expression으로
허용하지 않는다.

## 2. Compile transaction

`ConfitDependencyPlan`은 catalog를 빌려 보는 opaque immutable object다. Schema loader는
모든 reachable declaration과 type을 catalog에 소유시킨 뒤 plan을 한 번 만든다. Plan
생성은 다음 순서를 전부 통과해야만 publish된다.

1. 각 non-empty expression을 최대 4,096 bytes 안에서 lex/parse한다.
2. 최대 512 AST node와 32 nesting을 fail-closed로 적용한다.
3. short-circuit 여부와 무관하게 모든 reference를 catalog에 연결한다.
4. bare reference는 bool인지, 비교 literal은 exact declared kind인지 검사한다.
5. enum literal이 exact domain member인지 검사한다.
6. 같은 expression 안의 중복 reference를 하나의 graph edge로 정규화한다.
7. self-cycle과 multi-symbol cycle을 거부한다.
8. symbol lexical order로 tie-break한 prerequisite-first order를 만든다.

하나라도 실패하면 schema project도 publish되지 않는다. Declaration reorder는 catalog
index를 바꿀 수 있지만 topological symbol order와 availability 결과를 바꾸지 않는다.
Source array order는 값 precedence가 아니다.

## 3. Read-only evaluation

Evaluator는 catalog index와 정확히 정렬된 typed value array를 빌려 본다. Array 길이와
모든 kind를 평가 전에 검사하며 값을 copy하거나 고치거나 enable하지 않는다. Dependency가
없는 option은 literal true reason 하나를 갖는다. Dependency가 있으면 boolean 결과와
bounded reason tree를 함께 반환한다.

`&&`와 `||`는 실제로 short-circuit한다. 평가하지 않은 branch는 reason tree에 나타나지
않지만, 그 branch의 unknown symbol이나 type 오류는 이미 plan link 단계에서 거부되므로
short-circuit가 invalid declaration을 숨길 수 없다. AND가 왼쪽 false, OR가 왼쪽 true인
경우 root는 decisive child 하나만 가진다. 나머지 성공 경로는 stable child order를
유지한다.

Availability는 값의 소비 가능성만 설명한다. Evaluator는 referenced bool을 true로 만들지
않고, non-bool 값을 바꾸지 않으며, unavailable owner의 값을 default로 되돌리지 않는다.
그 default/user/unavailable 정책은 R12 resolver의 별도 책임이다.

Public evaluator는 독립 API caller를 위해 aligned array 전체의 kind를 검사한다. R12
resolver는 모든 default와 assignment candidate를 먼저 한 번 검증한 뒤 internal-only
prevalidated seam으로 같은 evaluator를 호출한다. 이는 최대 symbol graph에서 N개 option마다
N개 kind를 반복 검사하는 비용을 피하기 위한 implementation boundary이며 public 우회 API가
아니다.

## 4. Ownership and genericity boundary

Expression plan은 catalog보다 먼저 destroy되어야 한다. Evaluation은 plan과 catalog가
살아 있는 동안만 reason symbol pointer를 빌린다. 모든 AST, decoded literal, edge, order,
reason array allocation은 caller allocator capability를 사용하며 실패 시 partial object를
공개하지 않는다.

이 계층은 다음을 알지 못한다.

- project source, C header, object, Makefile 또는 linker input;
- driver, subsystem, architecture, target 또는 profile;
- source membership, compile/link order 또는 build dependency;
- filesystem path lookup, directory enumeration, subprocess 또는 compiler probe.

따라서 dependency edge는 configuration availability edge일 뿐 consumer build graph가
아니다.

## 5. R11 evidence

Direct C tests는 precedence/parentheses, bool/int/hex/string/enum equality와 inequality,
unknown reference, bare non-bool, wrong literal type, enum domain, self/multi cycle,
operator rejection, exact nesting/node 경계, short-circuit reason, evaluator input
불변성, declaration reorder의 stable order를 검증한다. Schema integration test는
reachable TOML의 expression이 project publication 전에 link됨을 검증한다. 별도의
deterministic 2,048-case expression corpus는 bounded parser와 evaluator를 반복한다.

이 증거는 resolver, CLI workflow, snapshot 또는 TUI가 완성되었다는 주장이 아니다.
