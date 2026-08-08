---
doc_type: language-spec
status: accepted
authority: normative
last_verified: 2026-08-09
---

# schema v2 expression

Expression은 TOML string에서 parse되는 typed, side-effect-free language다. Lexer, parser,
type-checker와 evaluator는 deterministic AST를 공유하며 resolver가 결과를 다시 해석하지 않는다.

Expression은 option reference, scalar/list literal, boolean/ordering comparison과 schema가 명시한
pure helper만 사용할 수 있다. filesystem, current time, process environment, shell command, mutable
global value와 untyped coercion은 semantic input이 아니다. Unknown option, type mismatch, overflow,
excessive token/AST depth와 invalid UTF-8은 fail-closed한다.

`when`, constraint와 availability는 boolean result를 요구한다. Evaluation은 requested/effective
provenance를 mutate하지 않으며 diagnostic span은 source file/line/column을 보존한다.
