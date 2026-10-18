# Milestone 2 Blueprint — The Manna Compiler (host-side, macOS)

Started as a text-only blueprint; the lexer and parser it describes (`tools/mannac/`) are now both fully implemented against it (`docs/STATUS.md` has the step-by-step log - this file stays the design doc, not a running history of what's built). See `CLAUDE.md` for the hard constraints this must not violate, and `VISION.md` for longer-range context this does *not* assume.

## 1. Implementation language: C vs. Rust vs. other

**Decision for the bootstrap compiler: C.**

| | C | Rust |
|---|---|---|
| Portability into Deus | Already the bootstrap language (CLAUDE.md: "minimal assembly core stays permanently... keep the C bootstrap buildable"). A C compiler frontend can eventually be ported into the freestanding kernel environment with the same `-ffreestanding`/`-mcmodel=kernel` discipline already in use. | Needs `core`/`alloc` without `std`, a working `#[no_std]` target, and a much heavier toolchain bootstrap story to ever run *inside* Deus. Plausible, but a second hard problem stacked on the first. |
| Compile times (this machine) | Fast — every `.c` file in this project compiles in well under a second. | Direct, lived data point from this session: Rust/C++ toolchains with deep dependency graphs can take hours to build from source on this unbottled (Tier 3) machine. A Rust-written frontend inherits that risk for every contributor without a bottle. |
| Path to self-hosting in Manna | Manna descends from HolyC, itself a C-like, pointer-and-struct language. A C-written frontend is a much shorter conceptual hop to "now written in Manna" than a Rust one. | Self-hosting a Rust-shaped compiler in a C-like language means either giving Manna Rust-like ownership semantics (scope creep the v0 spec below avoids) or a much bigger rewrite at the self-hosting step, not a port. |
| Safety during bootstrap | None for free — exactly why the static pointer verifier (stage 3) exists as its own explicit pass rather than leaning on the host language's type system. | Memory safety for free in the *frontend's own code*, but that safety doesn't transfer to Manna source being compiled — the verifier still has to exist regardless. |

The verifier does the safety work either way, so Rust's main advantage isn't decisive here — C wins on the two things that matter most for this project: portability into a freestanding kernel, and not re-running this session's build-time pain on every future contributor's machine.

**Standing constraint (CLAUDE.md, reaffirmed here): the C bootstrap compiler stays permanently buildable.** Even after Manna is capable of self-hosting (v1, see §2), the original C-written `mannac` is never retired. Two reasons: it's the reproducible bootstrap path (anyone can rebuild the whole toolchain from a C compiler and nothing else), and it enables **diverse double-compiling** — compiling `mannac` with the C bootstrap *and* with the self-hosted Manna build and diffing the two compilers' output, a direct countermeasure to a Thompson "Reflections on Trusting Trust"-style compromise of a single trusted build path. This only works if the C version is kept alive and working, not archived once self-hosting lands.

## 2. Language Spec v0 — minimal subset only

No feature creep. v0 needs exactly enough to express the loop-counter-injected, pointer-verified kernel functions M1's C code already represents — nothing more:

- **Types:** `u8`/`u16`/`u32`/`u64`, `i8..i64`, `bool`, fixed-size arrays, structs (fixed layout, no tagged unions/variants yet). No generics, no traits, no interfaces. **`u8`..`u64`, `i8`..`i64`, `bool`, and `unit` are reserved words** (implemented in #4 step 1; the constructors and conversions below are still specified only - see §4 "Reserved words, constructors, and conversions"): they lex as keywords, never as identifiers, so `u64(x)` is a conversion *syntactically*, telling it apart from a call needs no name resolution. Struct values are built with `Point(x: 1, y: 2)`, array values with `[1, 2, 3]` or `[v; N]`, integer conversions are explicit `u64(x)`; there is no implicit conversion anywhere (principle 2).
- **Functions:** named functions, fixed arity, no overloading, no closures, no varargs.
- **Loops:** `for` (C-style three-clause) and `while` only. No `foreach`/iterators — those need a trait/protocol system v0 excludes.
- **Safe pointers:** a single pointer kind, bounds-carrying (pointer + length, not a bare address) for anything outside `[kernel_hook]`. Raw/bare pointers don't exist in v0 source — they're an IR-level concept the verifier permits only inside verified `[kernel_hook]` regions.
- **`[kernel_hook]` attribute:** marks a function as allowed raw pointer arithmetic and hardware intrinsics (port I/O, `cpuid`, MSR access — the exact primitives `kernel/arch/x86/*.c` already uses). Everything else in a `[kernel_hook]` function is still typed-IR-checked; it's an escape hatch from the safe-pointer rule only, not from the pipeline. **v0 reading (decided 2026-10-11):** the v0 grammar and AST have no pointer type, dereference or intrinsic syntax, so nothing a `[kernel_hook]` function can say differs from any other function; in v0 the attribute is a flag the IR carries and whose spelling it validates (any other attribute is an error), with no call restriction. Raw pointer arithmetic and intrinsics arrive with the syntax to express them (not v0). The Wasm backend still rejects every `[kernel_hook]` function (§3). See "Typed IR (stage 3): decided rules".
- **Explicitly excluded from v0:** generics, traits/interfaces, closures, exceptions/unwinding, operator overloading, modules/namespaces, dynamic dispatch, heap allocation, strings.
- **Wasm backend target (v0): Wasm MVP only** — no threads, no SIMD, no GC proposal features. (Already noted inline in the pipeline-stages diagram below; restated here as an explicit scope decision, not just an implementation detail of one pipeline box.)
- **Signed integers, overflow, and the negative-min literal:** `i8`/`i16`/`i32`/`i64` are first-class alongside `u8`-`u64` (already listed above). Unary `-` applied to an unsigned-typed operand is a type error, caught in the typed-IR stage — the parser has no type information yet, so it parses identically to the signed case; rejection happens one stage later. All arithmetic overflow, signed and unsigned, **traps at runtime** — no silent wraparound, no undefined behavior, in either direction. The most-negative `i64` literal (`-9223372036854775808`) needs special handling in the typed-IR stage specifically: the lexer only ever produces unsigned-magnitude `NUMBER` tokens, so `9223372036854775808` (2^63) lexes fine as an in-range `u64` value with no lexer-level overflow (confirmed: well under `2^64-1`); the IR stage must recognize negation applied directly to a literal of magnitude exactly `2^63` in an `i64` context as meaning `i64::MIN` (**generalized, decided 2026-10-11, to every signed width:** magnitude exactly `2^(N-1)` directly negated in an `iN` context is `iN::MIN` for N in 8, 16, 32, 64, so `-128` is a valid `i8`) — not validate the unsigned magnitude as a standalone `i64` first (which would wrongly fail, since positive `i64` only reaches `2^63-1`) and negate second.

  **"Directly negated" means exactly that - no parens in between.** `-9223372036854775808` is `i64::MIN`; `-(9223372036854775808)` is a plain overflow error, since the magnitude goes through a parenthesized sub-expression rather than being the unary minus's immediate, literal operand (there's no principled way to call the parenthesized form `i64::MIN` too without the same reasoning eventually applying to arbitrarily-indirect forms like `-((9223372036854775808))`, which should obviously still be rejected). The parser (fresh-eyes audit, r1) already carries the exact mechanism the IR stage needs for this, since there's no dedicated Paren AST node to hang a "was this parenthesized" fact on otherwise: `Expr.parenthesized` (`ast.h`) is set, and the node's span widened to include the parens, by `parse_primary`'s `'('` case (`parser.c`) - mutated in place on whatever expression came back from inside the parens, idempotently correct through nested parens too (`((42))` ends up `parenthesized == true` with a span covering both pairs). The IR stage's exact test, written with the real `Expr` fields (`ast.h`; for an `iN` context, `2^(N-1)` below is `9223372036854775808` when N is 64): `e->kind == EXPR_UNARY && e->as.unary.op == UNOP_NEG && e->as.unary.operand->kind == EXPR_NUMBER && !e->as.unary.operand->parenthesized && e->as.unary.operand->as.number.value == 2^(N-1)`. Verified directly (`tools/mannac/parser_fixes_test.c`, not a golden test - the flag has no S-expression dump syntax of its own) for both the bare and parenthesized forms, including the exact widened span in the parenthesized case.

- **Integer literal typing (decided 2026-10-11): no default type.** A literal takes the integer type its context expects, which it must fit, or the type of its sibling operand (`0 == x`, `1 + x`, `10 < n`); with no type from anywhere (`let i = 0;`, `0 == 1`, a conversion's bare-literal operand) it is a compile error asking for an annotation. Literal suffixes were considered and not adopted. Details: "Typed IR (stage 3): decided rules".

**Self-hosting requires v1 features (strings, heap). Structs moved to v0.** Structs are in v0 (see Types, above) — driver and microkernel task formatting need them now, not at some later self-hosting milestone. What v0 still doesn't have, and what self-hosting `mannac` itself in Manna still needs, is a heap allocator (for AST nodes and other dynamic structures) and strings (for identifiers, source text, diagnostics). Until v1 adds those, self-hosting isn't attempted — v0's C bootstrap compiler is the only `mannac` that exists, by design.

## 3. Pipeline stages

```
source (.mn)
   │
   ▼
┌─────────────────┐
│ Lexer             │  tokenizes; tracks line:col per token for every
│                   │  later error message
├─────────────────┤
│ Parser             │  recursive-descent, builds untyped AST
│                   │  (also the AI-generated-AST entry point from
│                   │  VISION.md — an external AST slots in here, after
│                   │  the parser's own output, before anything below -
│                   │  same downstream pipeline, no shortcut)
├─────────────────┤
│ Typed IR builder    │  AST -> typed IR; resolves names, checks types,
│                   │  rejects anything outside the v0 spec
│                   │  (decided rules: "Typed IR (stage 3)", §4)
├─────────────────┤
│ Loop-counter        │  <-- HERE, BEFORE the verifier, not after.
│ injection           │  Every loop in the typed IR gets an instrumented
│                   │  counter write to the telemetry ring (sched.c's
│                   │  existing TELEM_BASE mechanism). Injected code is
│                   │  itself just more typed IR at this point - it has
│                   │  not been checked yet, and it must be, like
│                   │  anything else the compiler emits. Placing
│                   │  injection before the verifier means the verifier's
│                   │  single pass covers it for free; there is no
│                   │  path where injected code reaches the backend
│                   │  unverified. (If a future pipeline change ever
│                   │  needs post-verification structural info and
│                   │  moves injection later, the verifier must be
│                   │  re-run afterward - injection bypassing
│                   │  verification, in either direction, is not
│                   │  acceptable.)
├─────────────────┤
│ Static pointer       │  walks the typed IR (counters already present);
│ verifier            │  proves every non-[kernel_hook] pointer access
│                   │  in-bounds where it statically can, and REJECTS
│                   │  the program outright for anything it can prove
│                   │  is out-of-bounds. An access it can't prove
│                   │  either way (e.g. arr[user_input]) isn't a
│                   │  compile error - it gets a runtime bounds check
│                   │  inserted, trapping on failure, same precedent
│                   │  as arithmetic overflow (no silent wrap, no UB).
│                   │  The verifier's job is to eliminate as many of
│                   │  those runtime checks as it can prove
│                   │  unnecessary, not to be the only enforcement
│                   │  mechanism. [kernel_hook] functions get a
│                   │  narrower check: raw ops are allowed, everything
│                   │  else is still verified the same way.
├─────────────────┤
│ Wasm backend         │  typed, verified IR -> .wasm module.
│                   │  Target: the Wasm MVP subset needed for v0 - no
│                   │  SIMD, no threads, no GC proposal features.
│                   │  Rejects any [kernel_hook] function outright,
│                   │  with the diagnostic "native backend required" -
│                   │  port I/O, cpuid, and MSR access have no Wasm
│                   │  sandboxed equivalent, so silently miscompiling
│                   │  or stubbing them out is not an option. A
│                   │  separate native backend (kernel code's actual
│                   │  compile target, per CLAUDE.md's "LBS and the
│                   │  language" dual-target split) is what eventually
│                   │  handles these - not scoped here. Each v0
│                   │  construct's machine cost (MANNA_PRINCIPLES.md
│                   │  principle 10 / Admission Test item 4) gets
│                   │  documented as part of writing this backend -
│                   │  the Wasm instruction sequence each construct
│                   │  lowers to is the natural place that cost
│                   │  becomes concrete and checkable, not a separate
│                   │  retroactive pass.
│                   │  Backend requirement (Typed IR rules, §4):
│                   │  every local lives until its function
│                   │  returns, inner-block locals included -
│                   │  never reuse or pop an inner block's slot
│                   │  early, or a slice of an inner-block array
│                   │  could dangle.
└─────────────────┘
          │
          ▼
   .wasm module
```

### Lexer decisions (stage 1, implemented)

- **v0 source is ASCII-only.** Any byte ≥ 0x80 is a lex error (`ERROR_BAD_CHAR`, distinct message), not the start of a multi-byte identifier or anything else. No Unicode identifiers, no UTF-8 decoding, in v0.
- **Block comments (`/* */`) do not nest**, matching C: the first closing delimiter ends the comment regardless of any opening delimiter seen since the outermost one. Tested explicitly (`tests/lexer/nested_comment.mn`) rather than left as incidental behavior.
- **Numeric literals that don't fit in 64 bits are a lex error** (`ERROR_NUMBER_OVERFLOW`), not a silent wraparound - checked for both decimal and hex literals, including at the exact `2^64-1` boundary.
- **Tabs count as 1 column**, not expanded to a tab-stop width. Simplest option consistent with "no feature creep"; revisit only if a real need for aligned-error-column tooling appears.
- **Ten more reserved words (implemented, #4 step 1):** `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `bool`, `unit`. Each becomes its own `TOK_KW_*` kind, exactly like `fn`/`let`/`true` already are (`keyword_kind` in `lexer.c`), so an identifier position can never hold one: `let u8 = 1;`, `fn bool()`, a field called `unit`, and an attribute `[u8]` are all errors (generic E0105, "expected identifier, found 'u8'" - see "Diagnostics added by r1 Phase B" for why that is a documented exception, not a gap). Matching is on the whole token: `u8x`, `i64_val`, `booleans`, `unit_count`, `bool_`, `U8` and `Unit` stay `IDENT` (`tests/lexer/keywords_types.mn`). Consequences, verified in step 1: `tests/lexer/basic.tokens` changed in exactly 8 lines, each `IDENT u32`-style to `KW_U32`-style and nothing else; all 201 pre-existing parser goldens, including every type-using one, stayed byte-identical because types render inline as bare text (§4, golden format).

## 4. Parser grammar (v0, approved)

Recursive-descent, built on the lexer above; implemented in `tools/mannac/parser.c` (tag `m2-parser`, extended in the #4 parser step). Everything in the grammar block below is implemented; comments marked **r1-B** record that a production was specified in r1 Phase B and built in the #4 step (struct construction, arrays, conversions, reserved words, the relational level, `-> unit`).

```ebnf
translation_unit  ::= version_decl { top_level_item } ;
version_decl      ::= 'manna' IDENT ';' ;
  (* First non-comment line of every file, required - e.g. "manna v0;".
     A missing or wrong version is a compile error. See "Language
     design decisions" below. *)
top_level_item    ::= struct_decl | function_decl ;

attribute         ::= '[' IDENT ']' ;

struct_decl       ::= 'struct' IDENT '{' { field_decl } '}' ;
field_decl        ::= IDENT ':' type ',' ;

function_decl     ::= { attribute } 'fn' IDENT '(' [ param_list ] ')' [ '->' type ] block ;
  (* Absent '-> type' means the function returns nothing (unit) -
     stated explicitly, not inferred. Principle 15 (types at every
     boundary) only permits inference inside function bodies; a
     function's own signature, return type included, is never one of
     those bodies.

     Implemented (#4 step 6): writing `-> unit`
     explicitly is a dedicated error (E0109) - `unit` is reserved but is
     not a type name anywhere in source; the one way to say "returns
     nothing" is to omit the annotation (principle 1). *)
param_list        ::= param { ',' param } ;
param             ::= IDENT ':' type ;

type              ::= PRIM_TYPE                          (* implemented, #4 step 1 *)
                    | IDENT
                    | '[' INTEGER ']' type   (* fixed-size array *)
                    | '[' ']' type           (* safe slice *)
                    ;
PRIM_TYPE         ::= 'u8' | 'u16' | 'u32' | 'u64' | 'i8' | 'i16' | 'i32' | 'i64' | 'bool' ;
  (* Implemented (#4 step 1): these nine are keyword tokens, no longer IDENT;
     `parse_type` builds the same TypeIdent node from them as from an IDENT. `unit` is reserved too but is deliberately NOT in
     PRIM_TYPE - it is not writable as a type. The two '[' alternatives
     share a prefix; `parse_type` already implements the left-factored
     form ('[' then branch on INTEGER vs ']'), which is the actual LL(1)
     shape - see "LL(1) re-verification" below. *)

block             ::= '{' { statement } '}' ;

statement         ::= let_stmt | if_stmt | while_stmt | for_stmt
                    | return_stmt | break_stmt | continue_stmt
                    | postfix_stmt | expr_stmt ;
  (* postfix_stmt and expr_stmt have disjoint FIRST sets - postfix_stmt
     starts with whatever primary/postfix starts with (NUMBER, KW_TRUE,
     KW_FALSE, IDENT, '('; the nine PRIM_TYPE keywords and
     '[' were added in the #4 step - `starts_postfix` in parser.c grew to match); expr_stmt,
     reached only when none of those applied, starts with a unary prefix
     ('!', '-', '~'). Genuinely LL(1) at this level, unlike the
     assign_stmt/expr_stmt split it replaces. *)

let_stmt          ::= 'let' IDENT [ ':' type ] '=' expression ';' ;

postfix_stmt      ::= postfix expr_tail ';' ;
  (* Left-factored (principle 5, LL(1)): assign_stmt and expr_stmt used
     to be separate top-level `statement` alternatives, but both can
     start with the exact same tokens (anything that starts a
     `postfix`) - a single token of lookahead can't tell them apart
     until AFTER the shared `postfix` prefix has been consumed. Naming
     them as two alternatives at the `statement` level was therefore
     not naively LL(1): an LL(1) table built straight off that FIRST-set
     overlap doesn't exist. This merged form states the actual decision
     point where it really is - right after `postfix`, with exactly one
     token of lookahead ('=' or not) - which is both what any real
     implementation already does and what makes the grammar honestly
     LL(1) as written, not just LL(1) in spirit.

     Consequences, all structural, none needing a bespoke error message
     except the one noted under "Dedicated diagnostics" below:
       - `if x = 5 { ... }`: if_stmt's condition is `expression`, which
         can never produce '='. Parsing the condition stops at `x`,
         then '{' is expected and '=' is found instead.
       - `a = b = c;`: the assignment branch's RHS is `expression` (no
         assignment inside it), so a second '=' after the first RHS is
         simply unconsumable - parse error at that '='.
       - `1 + 2 = x;`: `1 + 2` cannot reduce to `postfix` (postfix has
         no '+' production), so `postfix_stmt` never matches starting
         there at all; it's parsed as a plain expression instead, and
         the '=' right after is unexpected where ';' was expected. *)

expr_tail         ::= '=' expression
                    | /* empty, continuing as a plain expression */
                    ;
  (* NOT a flat CFG production in the usual sense - this is the precise
     operational definition, since precedence-climbing parsing isn't
     naturally expressible as one. `postfix_stmt` has already consumed
     one `postfix` as a complete operand (unary/postfix are the only
     levels below `multiplicative`, and both are already spent). The
     empty alternative means: resume the SAME precedence-climbing loop
     `expression` itself uses, re-entered one level up - at
     `multiplicative` and above - with the already-parsed `postfix` as
     the left-hand operand instead of parsing a fresh one. Concretely:
     repeatedly consume a binary operator at `multiplicative` precedence
     or looser, each time parsing its right-hand side through the full
     normal `expression` chain (every binary operator's RHS always
     starts fresh), until no further operator applies. This is exactly
     the loop `multiplicative`/`additive`/`shift`/.../`logic_or` already
     describe for `expression`'s own first operand - `expr_tail` just
     names the case where that first operand arrived pre-parsed. *)

if_stmt           ::= 'if' expression block [ 'else' ( if_stmt | block ) ] ;
while_stmt        ::= 'while' expression block ;
for_stmt          ::= 'for' ( let_stmt | postfix_stmt | expr_stmt ) expression ';' for_step block ;
for_step          ::= postfix expr_tail ;
  (* Same left-factoring as postfix_stmt, reusing the same expr_tail -
     just without the trailing ';' (the block's '{' follows directly).
     e.g.: for let i = 0; i < n; i = i + 1 { ... } - the step clause
     "i = i + 1" is postfix('i') expr_tail('= i + 1'). *)
break_stmt        ::= 'break' ';' ;
continue_stmt     ::= 'continue' ';' ;
return_stmt       ::= 'return' [ expression ] ';' ;
expr_stmt         ::= expression ';' ;
  (* Still exists for statements that can't start with a postfix - e.g.
     a bare unary expression like `!flag;` or `-x;` as a statement
     (unusual, but not excluded). postfix_stmt and expr_stmt together
     cover every expression-shaped statement; they're disjoint by
     construction (postfix_stmt needs a leading postfix; expr_stmt here
     only ever gets reached for the forms that don't have one). *)

expression        ::= logic_or ;
logic_or          ::= logic_and { '||' logic_and } ;
logic_and         ::= relational { '&&' relational } ;
relational        ::= bitor [ rel_op bitor ] ;
rel_op            ::= '<' | '>' | '<=' | '>=' | '==' | '!=' ;
  (* Implemented, #4 step 5 - REPLACES the former two
     levels `equality ::= comparison [ ('=='|'!=') comparison ]` and
     `comparison ::= bitor [ ('<'|'>'|'<='|'>=') bitor ]`. At most ONE
     comparison/equality operator per operand of `&&`/`||` (every
     expression is such an operand, a lone one if no `&&`/`||` is
     present). Non-associative across BOTH former levels, not just
     within each: `a < b < c` and `a == b != c` stay E0101 (same class,
     the existing message); `a < b == c` and `a == b < c` - which the
     two-level grammar accepted as `(a<b)==c` and `a==(b<c)` - are now
     E0108, with a parenthesizing fix-it. Parenthesized operands are
     fine (`(a < b) == c`): '(' re-enters `expression`, so each paren
     group is its own operand. BEHAVIOR CHANGE (done in step 5):
     `tests/parser/err_mixed_cmp_eq_{lhs,rhs}` (`a<b==c`, formerly `comparison_wrapped_by_equality_*`,
     `a==b<c`) used to parse and are now E0108 error goldens, and
     `full_chain_smoke` (its input contained `c==d<e`) had its input
     parenthesized to `c==(d<e)` so it still walks the whole precedence
     chain, with the old input kept as `err_mixed_full_chain`;
     the old `parse_equality` comment calling `a < b == c` "the ordinary,
     unambiguous (a<b) == c" is gone.
     Rationale: that reading is only "unambiguous" to someone who has
     memorized the precedence of two adjacent levels - the same
     C-precedence-trap class MANNA_PRINCIPLES.md Part 3 already counts
     the bitwise rule as fixing (principle 2: what you see is what
     runs). *)
bitor             ::= bitxor { '|' bitxor } ;
bitxor            ::= bitand { '^' bitand } ;
bitand            ::= shift { '&' shift } ;
  (* Bitwise binds tighter than comparison/equality - deliberately the
     opposite of C, which famously binds bitwise looser. *)
shift             ::= additive { ( '<<' | '>>' ) additive } ;
additive          ::= multiplicative { ( '+' | '-' ) multiplicative } ;
multiplicative    ::= unary { ( '*' | '/' | '%' ) unary } ;
unary             ::= ( '!' | '-' | '~' ) unary | postfix ;
  (* Self-recursive - `!!!!x` is a real, deep recursion path, sharing
     the universal depth counter below, not a separate one. *)
postfix           ::= primary { '.' IDENT | '[' index_or_slice ']' } ;
index_or_slice    ::= expression [ '..' expression ] ;
primary           ::= NUMBER | KW_TRUE | KW_FALSE
                    | IDENT [ '(' [ arg_list ] ')' ]
  (* The ONLY call form in v0: a bare identifier immediately followed
     by '('. No indirect calls, no calling an expression's result. *)
                    | PRIM_TYPE '(' expression ')'          (* implemented, #4 step 2: conversion *)
                    | '[' array_body ']'                    (* implemented, #4 step 3: array literal *)
                    | '(' expression ')'
                    ;
arg_list          ::= arg { ',' arg } ;
arg               ::= expression [ ':' expression ] ;       (* implemented, #4 step 4: was just `expression` *)
array_body        ::= expression ( { ',' expression } | ';' NUMBER ) ;
  (* r1-B (all implemented in the #4 step; the semantic checks named below remain IR-stage).
     - `arg`'s `:` form is struct construction: `Point(x: 1, y: 2)`. The
       parser accepts ANY expression on the left of ':'; that it is a
       bare field name, that the callee is a struct (not a function),
       that every field is present exactly once in declaration order, and
       that no call mixes named and positional args, are all IR-stage
       checks (the parser has no field-name or symbol table). Struct
       construction and a function call are the same syntax,
       `IDENT '(' arg_list ')'`; which one it is is resolved by name at
       the IR stage (naming-convention enforcement, §7 item 7, already
       makes them visually distinct: PascalCase vs. snake_case).
     - `[a, b, c]` is an array literal (at least one element: `[]` has
       no element to infer a type from and is a generic E0105,
       "expected expression, found ']'"; no trailing comma, matching
       `arg_list`). `[v; N]` is the repeat literal: `v` is any
       expression, `N` is a NUMBER token (not an expression - v0 has no
       constant evaluation, matching the INTEGER in `'[' INTEGER ']'
       type`). **`[v; 0]` is legal: a zero-length array** - `N == 0` is not an
       error at any stage. It is the only spelling of a zero-length array
       (the list form needs at least one element, so `[]` stays an
       error), which is also why the formatter's `[v; 1]` -> `[v]` rule
       never touches it. `v` is evaluated exactly once whatever `N` is,
       including 0 (one meaning, principle 4), and still determines the
       element type. A wrong element type is an IR check.
     - `PRIM_TYPE '(' expression ')'` is an explicit conversion,
       exactly one operand (not an `arg_list`). Semantics (IR/backend,
       recorded here so the parser step doesn't re-decide them): the
       result is `x` itself when the value fits the target type and a
       runtime TRAP when it doesn't - covering narrowing (`u8(big)` with
       `let big: i32 = 300;`) and a sign change (`u32(neg)` with
       `let neg: i32 = -1;`) uniformly, per "all arithmetic overflow
       traps" (§2). The operand needs a type of its own - a conversion
       supplies none and integer literals have no default (§2) - so the
       former spellings `u8(300)` and `u32(-1)` are compile errors now,
       not traps. Conversions are between integer types only, and
       **bool <-> integer conversion is not allowed in v0, in either
       direction**: `bool(x)` and `u8(b)` (for a `bool` `b`) both parse -
       `bool` is a PRIM_TYPE - and the IR stage rejects both. Write
       `x != 0` for the first; there is deliberately no spelling for the
       second (principle 1: not a second way to say `if b {1} else {0}`). The result of a conversion, an array
       literal, and a call is never an lvalue (lvalue validity -
       an IR-stage check, as `docs/history/R1_TRIAGE.md` #3 decided). *)
```

### Dedicated diagnostics (implementation requirements, not expressible in EBNF)

- **Chained comparison:** immediately after parsing one `comparison`, if the lookahead token is itself a comparison/equality operator, raise this exact diagnostic right there: `"comparisons can't be chained; use a < b && b < c"` — not a generic "expected X, found Y" surfacing later from whatever construct happens to be one level up.
- **Assignment-as-condition:** when `if`/`while` is expecting the condition-terminating `'{'` and finds `'='` instead, raise this exact diagnostic: `"assignment is a statement; did you mean '=='?"` — the single most likely reason someone hits this exact token pair, given `expression` structurally can't contain `'='` anymore.
- **Mixed comparison/equality (E0108) and `-> unit` (E0109)** - implemented (#4 steps 5 and 6); exact wording, trigger and fix-it are under "Reserved words, constructors, and conversions" below and in `docs/error_codes.md`.

### Universal nesting-depth bound

One shared counter, max **256 depth units** — not 256 levels of any particular construct. Each recursive production that can genuinely recurse (not every parse function; see `tools/mannac/parser.c`'s own "INVARIANT" comment for the precise rule: every recursion *cycle* must pass through at least one counted function) costs 1 unit per level. A single construct - nested parens, a `unary` self-recursion chain (`!!!!!!...x`), nested index/slice (`a[a[a[...]]]`), nested calls (`f(f(f(...)))`), or nested array types (`[1][1][1]...T`) - costs exactly 1 unit per level, so **255 levels of any one of them parses successfully; 256 is the first to cleanly fail** with the same parse error ("maximum nesting depth (256 units) exceeded"), never a host-stack overflow **in the parser**. (**Fixed in step 2b of the typed-IR plan.** The bound below counts recursion *cycles*, not tree height: left-associative and postfix chains are built by loops, so a flat 20,000-term input used to make a 20,000-level tree, and anything that walks it, such as the AST dump, could overflow the host stack - measured, `docs/STATUS.md` "Known issues". Tree height is now separately bounded to 512 by E0111; see "Typed IR (stage 3)", Input contract. A depth-valid input that combines deep nesting with long chains can now be rejected by it; every existing `depth_*_ok` golden is still accepted.) The 255-not-256 figure is because the innermost operand itself also costs 1 unit (255 wrapping levels + 1 innermost = 256, the exact cap). Mixed constructs cost more per level - e.g. `!(!(!(...)))` costs 2 units per level (1 from `unary`'s own recursion, 1 from the paren re-entering the full expression grammar), capping at **127 levels**, not 255. Empirically proven for expressions in `tests/parser/depth_*` (parens/unary/index/calls at 255 vs. 256 and 300, mixed nesting at 127 vs. 128 and 300, all under both plain and ASan builds, zero crashes) - not just asserted from reading the code.

**The 256-unit budget is shared with whatever already-counted construct encloses the one being measured - it is not a fresh 256 every time, and effective limits shrink with context, not just with the construct's own per-level cost.** A single construct's 255-level figure above assumes it's the *only* counted thing on the path (true for a bare top-level expression). Once it sits inside another counted construct, that construct's own hook has already spent some of the 256 before the inner one gets to use any of it. Worked example, from Step 6: a nested array type (`[1][1][1]...u32`, `parse_type`'s own 1-unit-per-level self-recursion, same mechanism as `unary`) costs exactly 255 levels **in isolation**, but a `let` statement's type annotation only ever appears inside a `block`, and `parse_block` carries its own hook (spent once, for the block itself, before any statement - let alone that statement's type annotation - is parsed). So the same array-type nesting, measured the way it actually occurs (`{ let x: [1][1]...u32 = 1; }`), caps at **254 levels, not 255** - one less, exactly the one unit `parse_block` already spent. Empirically proven in `tests/parser/depth_type_*` (254 vs. 255 and 300, plain and ASan, zero crashes). Nested blocks got the same treatment once `if`/`while`/`for` (Step 7) actually created block-within-block nesting, confirming the pattern again with two further worked examples, each costing a *different* amount per level depending on exactly which counted functions are open simultaneously at that level - not a single universal "nested block" number: real nesting via a block (`if a { if a { ... } }`, each level opening both `parse_block`'s hook and `parse_if_stmt`'s own) caps at **127 levels**, while an "else if" chain (`if a {} else if b {} else if c {} ...`, a self-recursion on `parse_if_stmt` alone that never touches `parse_block` at all for the chain links themselves) caps at **254 levels** - one less than the pure 255 because the deepest link's own `{ ... }` body still briefly opens one more counted frame before the chain's own depth can unwind. Empirically proven in `tests/parser/depth_nested_if_*` and `depth_elseif_*` (both boundaries, plus 300 for a clear margin, plain and ASan, zero crashes).

**Depth-bound proof summary (Step 9)** — every recursion cycle in `tools/mannac/parser.c`, the function whose hook counts it, and its proven limit. This table *is* the "dedicated depth-bound tests" deliverable: every cycle below already has a golden-test pair proving its boundary under both plain and ASan builds, consolidated here rather than re-derived. See `parser.c`'s own "INVARIANT" comment for why each cycle needs exactly the function listed (the rule - every cycle needs at least one counted function on it - not a blanket "every function needs a hook").

| # | Cycle | Counted by | Construct example | Succeeds | Fails | Golden tests |
|---|---|---|---|---|---|---|
| 1 | `unary -> unary` | `parse_unary` | `!!!...x` | 255 | 256, 300 | `depth_unary_*` |
| 2 | `primary -> expression` | `parse_expression` | `(((...x...)))` | 255 | 256, 300 | `depth_parens_*` |
| 3 | `primary -> arg_list -> expression` | `parse_expression` | `f(f(...f(x)...))` | 255 | 256, 300 | `depth_calls_*` |
| 4 | `postfix -> index_or_slice -> expression` | `parse_expression` | `a[a[...a[x]...]]` | 255 | 256, 300 | `depth_index_*` |
| 5 (standalone) | `type -> type` | `parse_type` | `[1][1]...u32` (bare) | 255 | 256, 300 | `depth_type_*` precede Step 6's `block`-wrapped figures below |
| 5 (in a block) | `type -> type` | `parse_type` (+1 already spent by `parse_block`) | `{ let x: [1][1]...u32 = 1; }` | 254 | 255, 300 | `depth_type_254_ok`/`255_fail`/`300_fail` |
| 6 | `{if,while,for}_stmt -> block -> statement -> {if,while,for}_stmt` | `parse_block` (+ redundantly `parse_if_stmt` for if/if) | `if a { if a { ... } }` | 127 | 128, 300 | `depth_nested_if_*` (bare block and, confirming Step 8 adds no cost, `depth_nested_if_in_function_*`) |
| 7 | `if_stmt -> if_stmt` ("else if") | `parse_if_stmt` | `if a {} else if b {} else if c {} ...` | 254 | 255, 300 | `depth_elseif_*` |
| 7 (via header recovery) | `parse_if_stmt -> handle_header_failure -> recover_else_branch -> parse_if_stmt` - the same cycle reached through the R1 recovery path, not an eighth cycle | `parse_if_stmt` (hooks on entry, before recovery runs) | `if x = 1 {} else if a {} else if b {} ...` (E0102 header, then a valid chain; limit counts the erroring `if` too) | 254 (measured) | 255, 300 (measured) | `depth_elseif_recovered_*` |
| mixed (1+2) | `unary` and `primary -> expression` combined | `parse_unary` + `parse_expression` | `!(!(!(...x...)))` | 127 | 128, 300 | `depth_mixed_*` |
| 10 (#4 step 2) | `primary -> PRIM_TYPE '(' expression ')' -> expression` | `parse_expression` | `u8(u8(u8(...x...)))` | 255 (measured) | 256, 300 (measured) | `depth_conv_{255_ok,256_fail,300_fail}` - same limit as cycle 2's parens, plain + ASan |
| 9 (#4 step 3) | `primary -> '[' array_body ']' -> expression` | `parse_expression` | `[[[...x...]]]` / `[[[x; 1]; 1]; 1]` | 255 (measured, both) | 256, 300 (measured, both) | `depth_array_{255_ok,256_fail,300_fail}`, `depth_repeat_{255_ok,256_fail,300_fail}` - same limit as cycle 2's parens, plain + ASan |
| 3 (extended, #4 step 4) | `primary -> arg_list -> arg -> expression` - named-arg values nest like call args | `parse_expression` | `f(a: f(a: ...x...))` | 255 (measured) | 256, 300 (measured) | `depth_namedarg_{255_ok,256_fail,300_fail}` - identical to cycle 3's `f(f(...))` (also bisected: 255/256), so no new back-edge; INVARIANT stays at nine |
| 8 | *(none - confirmed, not a new cycle)* | — | `function_decl`/`top_level_item`/`translation_unit` add no depth cost; cycle 6's figure (127/128) holds unchanged inside a real function body | — | — | `depth_nested_if_in_function_127_ok`/`128_fail` |

Takeaways that don't fit a table cell: the 256-unit budget is global and shared, not reset per construct, so **effective limits only ever shrink with context** (never grow) as more already-counted constructs enclose the one being measured - cycle 5's 255→254 and cycle 7's 255→254 are both this same effect, not independent coincidences. No cycle has ever produced a crash or host-stack overflow at any tested depth (255/256/300, or 127/128/300 for the costlier ones) - the mechanism works exactly as designed in every case tested.

### AST node types

Every node records a **start and end** source span (`line:col` of its first and last token), not just a start — required by the `manna check --json` error schema (Language design decisions, §7, item 4) to emit precise ranges for tooling like editor squiggles and auto-fixes. That includes `Field`/`Param` below: the shorthand notation omits a span for brevity, but every `Field`/`Param` in the actual implementation carries one, same as every other node — see the golden format below, where `(param@2:8-2:17 a u32)` shows it concretely.

```
Type   = TypeIdent(name) | TypeArray(size, elem: Type) | TypeSlice(elem: Type)

Field  = (span, name, type: Type)   (* also Param - same shape *)

Decl   = StructDecl(name, fields: [Field])
       | FunctionDecl(attributes: [string], name, params: [Param], return_type: Type?, body: Block)

Stmt   = LetStmt(name, type: Type?, init: Expr)
       | AssignStmt(target: Expr, value: Expr)   (* target is always postfix-shaped by construction *)
       | IfStmt(cond: Expr, then: Block, else: (IfStmt|Block)?)
       | WhileStmt(cond: Expr, body: Block)
       | ForStmt(init: (LetStmt|AssignStmt|ExprStmt), cond: Expr, step: (AssignStmt-shaped|Expr), body: Block)
       | BreakStmt | ContinueStmt
       | ReturnStmt(value: Expr?)
       | ExprStmt(expr: Expr)

Expr   = NumberLit(value: u64) | BoolLit(value: bool) | Ident(name)
       | Unary(op, operand: Expr) | Binary(op, lhs: Expr, rhs: Expr)
       | Call(name: string, args: [Expr])
       | Index(base: Expr, index: Expr)
       | Slice(base: Expr, lo: Expr, hi: Expr)
       | FieldAccess(base: Expr, name)
       | Convert(target: PrimType, operand: Expr)          (* #4 step 2: u64(x) *)
       | ArrayLit(elems: [Expr])                           (* #4 step 3: [1, 2, 3] *)
       | ArrayRepeat(value: Expr, count: u64)              (* #4 step 3: [v; N] *)

Arg    = Expr | NamedArg(name_expr: Expr, value: Expr)     (* #4 step 4: x: 1 ; Call.args holds Arg nodes *)
```
`Call` also represents struct construction (`Point(x: 1, y: 2)`) - the parser can't tell the two apart, and the IR stage builds a `STRUCT_NEW` node for a `Call` whose callee resolves to a struct (the AST itself is never mutated; "Typed IR (stage 3)"). `NamedArg.name_expr` is an arbitrary `Expr` at parse time for the same reason (§4 grammar comment): "is a bare field name" is an IR check. `PrimType` is the nine `PRIM_TYPE` keywords; whether `Type` gains a `TypePrim` node or keeps `TypeIdent` holding the keyword text is the #4 step's call, since either renders identically (below).

There is no `Assign` `Expr` variant — assignment lives only in `Stmt`, as `AssignStmt`.

### Golden test format for ASTs

Mirrors the lexer's convention directly: `tests/parser/*.mn` → `*.ast`, run via `make test-parser` (plain + `-asan` variants, same shared-script pattern as `test-lexer`/`test-lexer-asan`). An S-expression dump, each node tagged with its **start-end** span (§7 item 4 requires both, not just a start), e.g.:

```
(function_decl@2:1-5:1 add
  (params
    (param@2:8-2:17 a u32)
    (param@2:19-2:28 b u32))
  (return_type u32)
  (block@2:31-5:1
    (return_stmt@3:5-3:14
      (binary@3:12-3:17 +
        (ident@3:12-3:13 a)
        (ident@3:16-3:17 b)))))
```

This dump format is the **planned** canonical machine interchange form (principle 11: the AST has a stable serialized form, and AI agents may emit it directly) — **status: planned, not built.** Today it is *output-only*: a printer (`ast.c`) whose output the goldens check, and nothing reads it back. It only becomes an interchange form once an S-expression **reader** exists (dump text -> AST, going through the same validation as parser output and never past it, per VISION.md's AI-AST rule) with a round-trip test (`dump(read(dump(ast))) == dump(ast)`). Neither the reader nor that test exists, and `manna` has no ingestion path. It is also **lossy**: comments are dropped before the AST exists (`docs/IDEAS.md`, 2026-10-08 "Comment trivia"), so the dump cannot yet be the formatter's source of truth either. The rendering rules below are spec for the printer now - and a contract the future reader must satisfy - not evidence that interchange works:

- **`Field`/`Param` carry spans**, same as every other node (corrected above - the abstract shorthand omits one, the real type doesn't).
- **An absent `-> type` renders as `(return_type)`** (implemented, #4 step 6), explicit, never omitted - matching "stated, not inferred" (§2). The word `unit` no longer appears because `unit` is reserved and cannot be written in source (E0109), so a dump containing `(return_type unit)` is not a valid interchange form and a reader of it must reject it (principle 11). **This was a dump-format change, done in step 6:** 35 goldens contained `(return_type unit)` by then (14 when Phase B was written), `ast.c` and `ast_test.c` changed, and a script proved that reverting `(return_type)` to `(return_type unit)` in the new output reproduces 34 of the 35 old files byte for byte (the 35th, `err_unit_type_positions`, also gained E0109).
- **Expression nodes added in the #4 step:** `(array@span e1 e2 ...)` (implemented, #4 step 3), `(array_repeat@span N value)` (implemented, #4 step 3 - **the count renders in the header, before the one child, like a unary's operator or a convert's target; Phase B had written `value N`, which doesn't fit the dump's convention of scalar attributes in the header**), `(convert@span u64 operand)` (implemented, #4 step 2; the target renders as bare inline text, like types), and a named call argument (implemented, #4 step 4) renders as `(named_arg@span name_expr value)` inline among the call's other args. Plain positional args keep rendering bare, as now.
- **A typed `let` renders as `name:type`**; an inferred (no explicit type) `let` renders as a bare `name`, with no placeholder standing in for the absent type.
- **Types render inline as compact text** wherever they appear - `u32`, `[4]u32`, `[]u32` - never as their own nested `(type_ident ...)`/`(type_array ...)` S-expression. This is why `u32` appears bare in the example above rather than wrapped.
- **Calls render as `(call@span name arg1 arg2 ...)`**, args inline with no separate `args` wrapper - unlike `params`/`fields`, which do get one, since those group a named, typed thing (`Field`) rather than bare expressions.

For inputs with parse errors, the golden file has two sections — errors first, then whatever tree resulted:

```
ERRORS:
4:9 expected ';' after expression, found '}'

TREE:
(function_decl@2:1-6:1 add ...)
```

Test categories mirror the lexer suite: a basic valid-program case, one golden per grammar construct, a `[kernel_hook]` case, the two dedicated-diagnostic cases above, the four nesting-depth-bound cases, and general error-recovery cases (missing semicolon, unclosed brace, multiple errors in one file to confirm resynchronization continues past the first).

### Error recovery (Step 10b, implemented)

One systematic mechanism - Wirth-style follow-set recovery (Wirth's *Compiler Construction*) - replaces Step 10's per-level ad-hoc functions (including the header-recovery fix that briefly preceded this step). Each syntactic level declares its own resume-token set; `ParserState` tracks which levels are *currently active* via one counter per level, not a stack of distinct sets - every instance of a level shares the identical set regardless of nesting depth, so "is this level active at all right now" is all skipping ever needs to know (see `parser_state.h`'s doc comment on the `sync_*` fields). One shared skip (`sync_skip` in `tools/mannac/parser.c`) finds the first depth-0 token matching ANY currently-active set, tracking brace depth throughout exactly as Step 10 found necessary for `{`/`}` specifically (an inner closer opened during the skip itself must not be mistaken for a boundary), plus a separate `(`/`[` depth that masks **only `';'`** and is reset by any `{`, `}`, `fn`, `struct` or statement keyword (see "Fresh-eyes audit fixes" and "Post-tag correction" below - r1 first shipped these as one combined depth, which was wrong). The level whose call site just detected the failure then checks whether that token is its own: if so, it resumes locally; if not, it does nothing further, trusting whichever level truly owns the token to recognize it via the identical check once control naturally unwinds there.

| Level | Resume tokens | Masks | Active for |
|---|---|---|---|
| Top level | `fn`, `struct`, `'['` (only when `'[' IDENT ']' fn`) | - | the whole file (`parser_parse_file`) |
| Block | `'}'` | - | `parse_block`'s statement loop |
| Statement | `';'`, `let`/`if`/`while`/`for`/`return`/`break`/`continue` | - | one `parse_statement` attempt (same span as block, since block drives it directly) |
| Header (`if`/`while`) | `'{'` | - | condition parsing, until the block starts |
| Header (`for`) | `'{'` | Statement's `';'` | init/cond/`';'`/step, as one span |

**Why `for`'s header masks `';'` and `if`/`while`'s don't:** a `for`-header's own init/cond separator is a bare `';'`, indistinguishable by kind alone from the enclosing statement's own resume point - without the mask, an unresolved `for`-header's search stops at its own internal separator instead of finding `'{'`, reinterpreting the remaining clauses as fresh statements. This is exactly the Step 10 "known limitation" below, now fixed. `if`/`while`'s condition is a bare `expression`, which can never contain a `';'`, so `';'` always safely means "the enclosing statement already ended" for them, exactly as it does outside any header (e.g. `if x return;` still recovers cleanly at the `';'` - masking it too would be wrong, since a stray `'{'` found by skipping past it almost certainly belongs to some unrelated later construct, not this `if`).

**`fn`/`struct` inside a block (post-audit finding #0).** Neither can appear in a block, and neither can an attribute-led function (`'[' IDENT ']' 'fn'`, the same shape the top-level resume rule recognizes - one shared predicate), so when one of them is the next token while a top-level level is active, `parse_block` ends the block there: the statements parsed so far are kept, the enclosing function stays in the tree, and the token is left for the top level. One diagnostic, `expected '}' to close block, found 'fn'`, at the token (reached through a statement's own failed skip or a header failure, the earlier error already stands and nothing is added, and no enclosing block adds it either: every path that ends a block at the token records its position, and a block that finds its own position recorded stays silent; with several enclosing blocks and no earlier error, only the innermost reports). Before this, the block returned NULL and the whole function was dropped. A genuinely nested `fn g() {}` is read the same way, as a missing `}`: `g` becomes a top-level function, and whatever followed it inside the intended body is a second, separate top-level error. Goldens: `err_recovery_stray_{fn,struct}_*`.

**Accepted trade-off:** the mask is coarse, not clause-aware - while a `for`-header is unresolved, *any* content between the failure and the next *unmasked* stop (a statement-start keyword, block's `'}'`, or a top-level start) is silently discarded, even content that would have been an independently-valid statement had it stood alone (`tests/parser/err_recovery_for_header_mask_swallows_filler`). This is strictly better than Step 10's double-diagnostic cascade (silence beats a spurious second error) but is still loss, not recovery - a true fix needs the for-header's own clause-level recovery, deliberately not added (the same reasoning that keeps a malformed struct field from getting its own field-level recovery).

Termination is guaranteed because every resume token is eventually consumed by its true owner as control unwinds (`sync_recover_statement` consumes `';'` itself; the others leave their own token in place for the normal success-path code to consume), plus `parser_parse_file`'s own loop forcing a single-token skip if an iteration makes zero progress - the one case with no inner level left to consume on its behalf (a fresh top-level failure sitting exactly on its own dispatch token, e.g. a bare `'['` that never completes `'[' IDENT ']' fn`).

Two real bugs Step 10 found and fixed by writing and reading golden tests carefully (not by reasoning about the design alone) still have to be - and are - gotten right by this mechanism: brace-nesting-unaware skipping (the shared `sync_skip` tracks brace depth unconditionally, regardless of which level ends up owning the stop) and missing statement-start keywords in the sync set (the statement level's resume set still includes all seven keywords, each safe to leave alone because every `parse_*_stmt` function for them unconditionally consumes its own leading keyword before it can fail).

`parser_parse_block`/`parser_parse_file` still return failure (`NULL`/`false`) if a block's own delimiters are unrecoverably missing (no `{`, or EOF before `}`) or if `version_decl` itself fails - recovering from a missing/mismatched delimiter, or a missing file header, is a different problem than recovering from bad content between well-placed delimiters, and isn't attempted.

**Fresh-eyes audit fixes (r1), two more real bugs found by testing, not by inspection alone:**

1. **A header that recovered past a real body could still get its next sibling statement swallowed.** When a header's search found its own `'{'` and parsed the dropped statement's body (to surface its own errors), the enclosing `parse_block`'s loop had no way to tell that apart from an ordinary failure sitting at the point of breakage - it would attempt its own `sync_skip` on whatever token followed the already-recovered body, treating perfectly valid content as "junk to search past" (`{ if = { q; } y; }` used to silently drop `y;` entirely). Fixed with a one-shot flag, `ParserState.recovered_past_body` (set by `handle_header_failure_impl`, read and cleared by `parse_block`'s loop): when set, the loop just continues without attempting a skip, trusting cur is already wherever the recovered body legitimately left it. Covered for `if`/`while`/`for` and for an else-if chain (`tests/parser/err_recovery_{if,while,for,elseif}_header_recovered_next_stmt_preserved`).
2. **`sync_for_header`'s `';'`-mask was popped too late, leaking into the recovered body it was never meant to affect.** The mask has to stay active for the recovery *search* (which needs it), but a prior version of `handle_for_header_failure` popped it only after also parsing the recovered body - so a broken statement inside that body could have its own legitimate `';'` wrongly masked too, silently swallowing a second, independently-valid statement right along with it. Fixed by popping `sync_for_header` at the same point `sync_header` itself is popped - right after the search, strictly before the recovered body is parsed (`tests/parser/err_recovery_for_mask_not_leaked_into_body`, a two-broken-statements-in-one-body case: 3 diagnostics with the fix, 2 without).

Also: `sync_skip` tracks `'('`/`')'` and `'['`/`']'` depth so that a `';'` inside brackets the skip is passing over is not mistaken for a statement boundary - forward-looking for the planned `[v; N]` array-repeat literal (§6). **The first version of this (shipped in `m2-parser-r1`) was wrong in two ways and is superseded by "Post-tag correction" below:** it used one combined depth for all three bracket kinds, and it let that depth hide *every* resume token, not just `';'`. The claim previously made here - that a `'}'` landing inside an unclosed bracket "is no longer mistaken for an unrelated outer boundary" - was the bug, not a feature.

**Post-tag correction (external audit of `da9f01c`, fixed after tag `m2-parser-r1`, which was not moved or re-tagged).** Reproduced with real builds before changing anything; each was fine at `m2-parser`:
- **R2-R5 (`sync_skip`):** with one combined depth above 0, nothing could stop the skip, so a single unmatched `(` or `[` swallowed every following `}`, `fn` and `struct` - an unclosed `[` in a block dropped the whole function, a top-level `(` emptied the tree, `let a = * g(1;` dropped its function, and `let a = ) ((;` dropped everything. The rule now: `{ }` has its own depth (a failing `{` still counts as an opener); `(`/`[` share a second depth that masks **only `';'`**; `{`, `}`, `fn`, `struct` and every statement keyword reset it to 0, which is sound because the grammar guarantees no expression contains them (`parser.c`: "the grammar guarantees headers never contain '{'"; `is_statement_start_keyword`: "a statement can't legally start with any of them"); the failing token never counts as a `(`/`[` opener. `) [1; 3];` (one diagnostic) and the accepted `g(1 +; 3)` (two) are unchanged. `da9f01c` had also cut `err_recovery_stress_no_infinite_loop` from 7 diagnostics to 1 to match the regression; it is restored to 7.
- **R1 (header recovery):** after `if x = 5 { a; } else { b; }` recovered past its body, the `else` was left for the block loop, which parsed it as a statement - a second diagnostic, and a stray statement for an `else if` chain. Header recovery now consumes the `else` branch too.
- **R6:** only the `sync_*` counters reset at parse entry; `depth` and `error_count` did not, so a `ParserState` reused after a fatal longjmp hit a spurious E0103 on a valid file. All per-parse state now resets.
- **R7, R9 (tests/comments):** the fixes test leaked its arena (0 leaks after, by macOS `leaks`; Linux LSan was not run) and two comments cited a nonexistent `token_span_test.c`.

### Known limitations

- **A `for`-header's error recovery discards, rather than preserves or re-reports, anything between a header failure and the next resume point** (`tests/parser/err_recovery_for_header_mask_swallows_filler`) - see "Error recovery" above for why the `';'`-mask makes this an accepted trade-off, not a bug: it replaces Step 10's for-init double-diagnostic cascade with silent loss instead, which is strictly better but still loses real content that a finer, clause-aware recovery could have preserved.
- **A block cut off by EOF still drops its function, while one cut off by `fn`/`struct` keeps it.** `fn f() { let x = 1;` then end of file gives `expected '}' to close block, found end of file` and no `function_decl` for `f`; the same text followed by `fn g() {}` keeps `f` with its statements (post-audit F0, accepted by Bryden). The asymmetry is deliberate for now: F0 changed only the stray-declaration case. Per level, an EOF-unclosed nest reports one diagnostic per open block.
- **A stray attribute-led function is recognized for ONE `[IDENT]` group only.** Inside a body, `[a] fn g() {}` ends the block at the `[` (post-audit G1), but `[a][b] fn g() {}` does not: the shared predicate (`is_top_level_resume_token`, also used by recovery sets) matches `'[' IDENT ']' 'fn'` and nothing longer, so the first `[a]` is read as an array expression and the `fn` gives `expected ';' after expression, found 'fn'`, with `g` still reaching the top level without its attributes. Same limit as the top-level resume rule; found while writing G1, not yet in a golden.
- **E0108 reports the first offending pair of an expression, and its message does not follow operand order.** Parsing stops at the first error in an expression, so `a<b==c||d==e<f` reports only the first mixed pair (the second appears after that one is fixed, and a later mixed pair inside the same expression after an E0101 is not looked at either: `a<b<c==d` gives E0101 only). The message always spells its example as `use (a < b) == c`, whichever order was written; the machine-applicable fix-it is order-specific - `a == b < c` is offered `a == (b < c)`, `a < b == c` is offered `(a < b) == c` (pinned by `parser_fixes_test.c`). Statements are independent: two statements with a mixed pair each give two diagnostics (`err_mixed_two_statements`). Reproduced on the final tree before being written down.
- **A failure inside an already-opened `(`/`[` can cascade into a second diagnostic when it precedes a `';'` in that bracket** (`g(1 +; 3)` and `[1 +; 3]`, both pinned: `err_recovery_in_call_cascade_accepted`, `err_recovery_repeat_in_bracket_cascade`) - `sync_skip`'s bracket depth only covers brackets the skip itself passes over, not ones the parser had already consumed. Reproduced on today's binary; see "Recovery: `'['` in `primary`'s FIRST set" above for the evidence and the decision. Accepted, not fixed, in r1.

### Reserved words, constructors, and conversions (r1 Phase B)

**Status: implemented (the #4 parser step, steps 1-6).** This subsection began as the Phase B specification and is kept as the design record; every parser-side item below is built, its goldens are named in §8, and the depth figures are measured (see the table). What remains unbuilt is IR-stage work (the semantic checks listed under "Diagnostics added by r1 Phase B"). Where a sentence below still says "specified" or "predicted" about the parser, the proof table and §8 are authoritative.

#### LL(1) re-verification

`FIRST(primary)` becomes `{NUMBER, KW_TRUE, KW_FALSE, IDENT, u8, u16, u32, u64, i8, i16, i32, i64, bool, '(', '['}`. Every decision point the r1-B productions add or change, and why one token of lookahead decides it:

| # | Decision | Alternatives, by current token | Why it's deterministic |
|---|---|---|---|
| 1 | `type` | `PRIM_TYPE` (9 keyword kinds) / `IDENT` / `'['` | Three disjoint token classes. After `'['`: `INTEGER` = sized array, `']'` = slice (the existing left-factored form in `parse_type`). |
| 2 | `primary` | `NUMBER` / `true` / `false` / `IDENT` / `PRIM_TYPE` / `'('` / `'['` | Seven alternatives, pairwise-disjoint FIRST sets (distinct token kinds). **Reservation is what makes the `PRIM_TYPE` row disjoint from `IDENT`:** if `u64` were still an `IDENT`, `u64(x)` would match `IDENT '(' arg_list ')'` too, and only name resolution could say conversion vs. call. |
| 3 | `IDENT [ '(' ... ]` | `'('` or not | Requires `'(' ∉ FOLLOW(primary)`. No production places `'('` after an operand, and the r1-B additions to `FOLLOW(expression)` are only `':'` (from `arg`) and `','`/`';'`/`']'` (from `array_body`) - none is `'('`. |
| 4 | `arg ::= expression [ ':' expression ]` | `':'` or not | `FOLLOW(arg) = {',', ')'}`, which excludes `':'`. The other `':'` sites (`let IDENT ':' type`, `field_decl`, `param`) follow a declared *name*, never an expression, so `':'` joins `FOLLOW(expression)` only via `arg`. |
| 5 | `array_body` after its first `expression` | `','` / `';'` / `']'` | Three disjoint tokens. `N` after `';'` is a `NUMBER` token, not an expression, so the repeat form adds no further decision. |
| 6 | `'['` ∈ both `FIRST(primary)` and the `postfix` loop | operand-start position vs. after-operand position | The `postfix` loop is greedy and only runs after a complete `primary`; `primary` only dispatches at an operand-start position. Ambiguity would need some production that puts an operand-start directly after a complete operand (juxtaposition). Exhaustive check of every site an `expression` appears: `let ... '=' expression ';'`, `return [expression] ';'`, `if`/`while expression block` (next token `'{'`), `for ... expression ';'`, `for_step block`, `index_or_slice` (`']'`/`'..'`), `arg` (`','`/`')'`/`':'`), `array_body` (`','`/`';'`/`']'`), `'(' expression ')'`. Each is followed by a terminal that is not an operand-start, and statements in a block each end in `';'` or `'}'`, so a following statement's leading `'['` is never adjacent to a live operand. No juxtaposition exists, so no conflict. |
| 7 | `return_stmt`'s optional `expression` | `';'` vs. `FIRST(expression)` | `';' ∉ FIRST(expression)` (unchanged property). |
| 8 | `statement` dispatch | `starts_postfix` / unary prefix / statement keywords | `FIRST(postfix_stmt)` grows by the nine `PRIM_TYPE` keywords and `'['`; `FIRST(expr_stmt) = {'!', '-', '~'}` is unchanged; none of the new tokens is a statement keyword. Still pairwise disjoint. |
| 9 | `relational ::= bitor [ rel_op bitor ]` | a `rel_op` token or not | `rel_op ∉ FOLLOW(relational)`. The follow-up "another `rel_op` right after?" is a deterministic error production on the next token (E0101 same class / E0108 mixed class), not a second parse path. |
| 10 | `'->' type` | `KW_UNIT` after `'->'` | `KW_UNIT ∉ FIRST(type)`, so it can never be a valid start; the dedicated E0109 is an error production on that single token. |

**Honest scope of this claim:** it is a hand argument over the productions (the repo has no FIRST/FOLLOW generator), so it is exactly as strong as the table above. The mechanical evidence has to come from the #4 step: one golden per decision row above (including the rows-6 and -8 adjacency cases, e.g. a statement beginning `[1, 2][0] = 3;` right after an `if` block - `tests/parser/array_statement_after_if_block`), plus the fuzz run with the last-resort trap build described under "Recovery" below.

#### Recovery: `'['` in `primary`'s FIRST set

`'['` already means two things to the parser: the index/slice operator (post-operand) and an attribute opener (top level only). It now also starts an expression. The recovery code that mentions `'['` is the top-level resume rule, `sync_skip`'s bracket-depth tracking, and the top-level dispatch.

**1. The `'[' IDENT ']' 'fn'` top-level resume rule - not broken for valid input; one bounded, previously-latent misfire for invalid input.** `parser.c` (`is_top_level_resume_token`):

> `'[' only counts as a top-level resume point when it's '[' IDENT ']' 'fn' - a bare '[' alone is too weak a signal (headers and statements can't otherwise produce one, but requiring the full attribute shape avoids treating some unrelated, coincidental '[' as a fresh top-level start).`

The predicate is a pure four-token lookahead and is unchanged. It cannot fire inside a *valid* program on a new construct: `[ IDENT ]` is now also a one-element array literal, but `fn` ∉ `FOLLOW(expression)`, so `[x] fn` can only occur as an attribute. For *invalid* input, the same text can occur when a statement's skip passes over an array-literal-shaped `[y]` that happens to sit directly before a real `fn`. Run on today's binary (the lexical situation is identical; `[y]` is not yet an expression, which doesn't matter to the skip), scratch input `manna v0;\nfn f() {\n    let a = ) [y]\nfn g() {}\n`:

```
ERRORS:
3:13 expected expression, found ')'
TREE:
(function_decl@3:15-4:10 g
  (attributes y)
  ...
```

One diagnostic, `g` preserved - but `[y]` was claimed as `g`'s attribute. That is the same outcome any stray `[ident]` before `fn` has today, and the alternative (not resuming) would swallow `g`, the failure Step 10b's top-level level exists to prevent. Accepted; no change. **Stale comment (fixed in the #4 step - `is_top_level_resume_token`'s comment no longer says this):** the parenthetical "(headers and statements can't otherwise produce one ...)" becomes false once `'['` starts an expression - the rule is still right, the stated reason is not.

**2. `sync_skip`'s bracket-depth tracking (r1 #18) does what it was built for - when the skip *traverses* the opener.** The `parser.c` comment as shipped in `m2-parser-r1` read *"…so which bracket KIND is open doesn't matter, only whether at least one still is."* - that single-combined-depth design is what caused R2-R5 (see "Post-tag correction") and has been replaced: `(`/`[` now mask only `';'`. The conclusion drawn here still holds; verified on today's binary with `manna v0;\nfn f() {\n    let a = ) [1; 3];\n    let b = 2;\n}\n`: one diagnostic (`3:13 expected expression, found ')'`), and `let b = 2;` survives in the tree - the `';'` inside `[1; 3]` was correctly not a boundary.

**3. What is NOT guaranteed - a real limitation, ACCEPTED as a known limitation (user decision, Phase B review).** The bracket depth only exists inside `sync_skip`, which starts at depth 0 *at the failing token*. If the parser itself already consumed the opener and the error is inside it, the skip starts "inside" brackets it doesn't know about, and the first `';'` it meets is taken as a statement boundary. The repeat literal's own `';'` is exactly such a token: `[1 +; 3]` fails at that `';'`, the statement level resumes there, and `3]` is then parsed as a fresh statement - a second, spurious diagnostic from one mistake. This is not new in kind; today's binary does the same for calls (`manna v0;\nfn f() {\n    let a = g(1 +; 3);\n}\n` gives `3:18 expected expression, found ';'` **and** `3:21 expected ';' after expression, found ')'`). `[v; N]` adds a second construct with the property that its internal `';'` can be hit by an in-bracket error. No stated recovery guarantee is violated (`docs/STATUS.md`'s Step 10 entry claims no-cascade for statement- and top-level-item-granularity failures, not for failures inside an argument list), but the "one mistake -> one error" experience degrades for one more construct. **Decision: option (a), accept and list under "Known limitations"** (done). The rejected alternative, for the record: a bracket-open counter in `ParserState` masking the statement level's `';'` while inside a call/array/index, the same mechanism as `sync_for_header` - it would also have changed existing call behavior and needed its own goldens. Both cases are pinned by goldens: `err_recovery_in_call_cascade_accepted` (`g(1 +; 3)`) and `err_recovery_repeat_in_bracket_cascade` (`[1 +; 3]`), two diagnostics each.

**4. Termination and progress are unaffected.** At the top level `'['` still dispatches to `parse_function_decl`, whose attribute loop consumes `'['` before it can fail (`parse_attribute`: `advance(p); /* consume '[' */`), so a bare `'['` still makes progress and `parser_parse_file`'s last-resort single-token skip stays unreachable by the same argument as before. At statement level `'['` is now consumed by `parse_primary` instead of failing with zero progress. That reachability claim is exactly what `MANNA_FUZZ_TRACE_LAST_RESORT` exists to check; the #4 step must re-run both fuzz targets with it on rather than rely on this paragraph.

**5. The nine `PRIM_TYPE` keywords are NOT added to the statement level's resume set.** That set's rule (`parser.c`, `is_statement_start_keyword`) is that each keyword is "unconditionally consumed as the very first action of its own parse_*_stmt function" and "a statement can't legally start with any of them". Neither holds: `u8(x);` is a legal statement start, and `y = u8(x);` has one mid-statement. Making them resume tokens would make a skip stop inside valid expressions.

#### New recursion cycles and their counted functions

Rule (`parser.c`'s INVARIANT): every cycle must pass through at least one depth-counted function. r1-B adds **two** cycles and **extends one**; none needs a new hook, because each re-enters `parse_expression`, which is already counted. The "Succeeds"/"Fails" columns were predictions when Phase B wrote them and are now **measured** (bisected 1..300 with real builds, plain == ASan; `parser.c`'s INVARIANT comment counts nine back-edges):

| # | Cycle | Counted by | Construct example | Succeeds (measured) | Fails (measured) | Goldens |
|---|---|---|---|---|---|---|
| 9 | `primary -> array_body -> expression` (covers list elements *and* the repeat literal's value; `N` is a `NUMBER` leaf) | `parse_expression` | `[[[...x...]]]` and `[[[x; 1]; 1]; 1]` | **255, measured (#4 step 3), for both forms** | **256 and 300, measured** | `depth_array_*`, `depth_repeat_*` (plain + ASan) |
| 10 | `primary -> PRIM_TYPE '(' expression ')'` | `parse_expression` | `u8(u8(u8(...x...)))` | **255, measured (#4 step 2)** | **256 and 300, measured** | `depth_conv_*` (plain + ASan) |
| 3 (extended) | `primary -> arg_list -> arg -> expression`, on both sides of `':'` (the value side nests; the name side is parsed as an expression too, and nests the same way if someone writes `f(f(x): 1)`) | `parse_expression` | `f(a: f(a: ...x...))` | **255, measured (#4 step 4)** | **256 and 300, measured** | `depth_namedarg_*` (plain + ASan) |

Not cycles: `PRIM_TYPE` inside `type` is a leaf, so cycle 5 (`type -> type`) is unchanged; merging `equality`/`comparison` into `relational` removes one uninstrumented fixed-pipeline level (a constant cost, never a cycle, same character as the other pipeline levels); #6 adds no recursion. `parser.c`'s INVARIANT comment said "exactly seven back-edges" and now says nine (updated in the #4 step; its list numbers back-edges in the order they were added, so the conversion is its item 8 and the array literal its item 9, the reverse of rows 10 and 9 here). Mixed nesting follows the existing rule (e.g. `[(` costs 2 units/level, like cycle "mixed (1+2)"), and the same shared-budget shrinkage applies inside a block.

#### Diagnostics added by r1 Phase B

Registered in `docs/error_codes.md` (both now emitted: E0108 since #4 step 5, E0109 since step 6); the codes are reserved forever:

| Code | Message (exact) | Fires | Fix hint |
|---|---|---|---|
| `E0108` | `comparison and equality can't be mixed without parentheses; use (a < b) == c` | The second `rel_op` of an operand, when it's in the other class (`<,>,<=,>=` vs. `==,!=`) than the first. Same-class repeats stay `E0101`. | **Two zero-width inserts, exact text, no source-text capture** (decided at the start of the #4 step): edit 1 inserts `(` at a zero-width span at the first character of the comparison's left operand; edit 2 inserts `)` at a zero-width span just past the last character of its right operand. "The comparison" is always the `<`, `<=`, `>`, `>=` pair (the inner expression, never the `==`/`!=` pair). The fix inserts exactly one `(` and one `)` and nothing else, so the parser never needs the operands' source text - only their spans. Operands are `bitor`-level, so they may be compound (`x + 1 >= y != z` -> `(x + 1 >= y) != z`). This preserves the meaning the retired two-level grammar gave the expression. `DiagFix` is a single edit today (`diag.h`): #4 step 5 extends it to a short list of edits (two here); the JSON schema in §7 item 4 already carries an `edits` array, so the wire format is unchanged. For the `a == b < c` order the pair's second operand must be parsed before reporting, to know where `)` goes. One violation is reported per operand (at the second operator); if the fixed text still has another, it is reported on the next check - the fix-it does not promise a one-pass cure of a longer chain. |
| `E0109` | `'-> unit' is not written; omit the return type` | `'->'` followed by `KW_UNIT` **and then `{`** (so `-> [3]unit` and `-> unit[3]` stay the generic E0105 "expected type") | Deletions only, nothing inserted. If `->` and `unit` are on one line with only blanks between: ONE edit deleting exactly the text `-> unit` (from the first character of `->` through the last of `unit`). If a newline or a comment sits between: TWO edits, one deleting the `->` token and one deleting the `unit` token, so the comment or line break survives. The function STAYS in the tree with no return type and its body is parsed. |

**Which code each mixed/chained input gets** (E0101 is unchanged in message and in having no fix-it - the rewrite `a < b && b < c` would evaluate `b` twice, so it isn't a mechanical single-span edit, and its registry entry already says so):

| Input | Code | Fix-it inserts | Result after fix |
|---|---|---|---|
| `a < b < c`, `a <= b > c` (both comparison-class) | `E0101` | nothing (no fix-it) | - |
| **`a == b == c`**, `a == b != c` (both equality-class) | **`E0101`** | nothing (no fix-it) | - |
| `a < b == c` | `E0108` | `(` before `a`, `)` after `b` | `(a < b) == c` |
| `a == b < c` | `E0108` | `(` before `b`, `)` after `c` | `a == (b < c)` |
| `x + 1 >= y != z` | `E0108` | `(` before `x`, `)` after `y` | `(x + 1 >= y) != z` |

**Documented exceptions - generic `E0105`, no dedicated code, no machine-applicable fix.** Each of these is a plain "token X where Y was expected" that `diag_expected` already states completely; a dedicated message wouldn't add information, and renaming or choosing a different construct is semantic, not a single-span edit (the same stance the E0105 registry row already takes: "Varies by call site; most have none yet"):

- a reserved word where an identifier is required (`let u8 = 1;`, `fn bool()`, a field named `unit`, `[u8]` as an attribute) - `expected identifier, found 'u8'`;
- `unit` in any type position (`let x: unit`, a param, a field, an array element) - `expected type, found 'unit'` (only the `'->'` site gets the dedicated E0109, because that is the one place someone coming from another language will write it);
- `[]` - `expected expression, found ']'`; `[v; X]` with `X` not a `NUMBER` - `expected array repeat count (integer), found ...`;
- conversion arity and shape: `u8()` - `expected expression, found ')'`; `u8(a, b)` - `expected ')' after conversion operand, found ','`; a bare `u8` in expression position - `expected '(' after 'u8', found ...`.

**IR-stage diagnostics** (struct-construction field checks, array-literal element types, conversion legality, lvalue validity, maximum type size, ...) are specified in "Typed IR (stage 3): decided rules" below. The `E02xx` numbers are assigned in `docs/error_codes.md` in the commit that first emits each; none ship yet, so "no diagnostic ships unregistered" still holds.

### Typed IR (stage 3): decided rules

**Status: specified (decided by the reviewer, 2026-10-11); implemented so far (typed-IR steps 3-10): the IR shape and dump format below, the gate, `E0201`/`E0203`, empty-function building, and the top-level declarations (the one namespace and `E0210`, struct fields `E0211`, type resolution `E0220`, the slice-in-field rule `E0227`, containment cycles `E0221`, layout and the size limit `E0222`; function bodies of `return expr;`, `let`, assignment to a plain name, expression statements and `if`/`else` (block scopes, no shadowing: `E0213`, names `E0230`/`E0231`, the lvalue rule `E0270`, `E0225` for a literal with no expected type), function calls (callee among the top-level names, `E0232`-`E0234`, arguments checked left to right against the parameters; a call that returns nothing is not a value, `E0226`; direct and mutual recursion rejected, `E0235`; struct construction and conversions still stop the build), the return shape (`E0276`) and the structural missing-return analysis (`E0277`, once per function at the closing `}`; any statement of a block that returns makes the block return; loops never count), `while`/`for` (an init `let` is scoped to the loop), `break`/`continue` (`E0275`), every unary and binary operator on names and literals (`E0260`, `E0261` with its fix; a literal takes its typed sibling's type), with integer and bool literals, unary `-` (including the signed-minimum fold), and arithmetic/bitwise/shift groups of literals (`E0223`, `E0224`, `E0260` for unary `-` on an unsigned type); and function signatures: parameters `E0212`, attributes `E0214`/`E0215` with the `kernel_hook` flag, return types, slice placement `E0227`; "Names, scopes, items", "Types", "Layout and limits"). Everything else (names, types, literals, operators, calls, structs, arrays, naming) is not implemented.** This subsection is the owner of the typed IR stage's semantics; `docs/TYPED_IR.md` is its working checklist (steps, proposed error-code numbers, test plan) and links here rather than repeating it. Every rule below is a pending-admission record in §8 (#7-#15) until its goldens exist. Error codes are `E02xx` (`docs/error_codes.md`); each is assigned a number in the commit that first emits it, never earlier. Where a rule says "error" without a code, the number is not fixed yet.

#### What the stage is for, and not for

For: resolving every name and type; deciding what the parser left open (struct construction vs. call, conversion legality, lvalue validity, literal typing, the negative-minimum literal); the checks the parser handed over (naming conventions, unknown attributes, duplicate names, §7 items 7-8); and producing a form the next stages consume without re-checking (loop-counter injection, the verifier, the Wasm backend, `manna check --json`). Target-neutral (principle 13) and deterministic (principle 12).

Not for: bounds proofs (a literal `a[9]` on a `[4]u8` is the **verifier's** to reject, not this stage's), loop-counter injection, runtime trap insertion (the IR records which operations trap; the backends lower them), constant folding or any optimization (typing a literal and folding a directly-negated `2^(N-1)` into `iN::MIN` are representation, not optimization), SSA/CFG construction, machine-cost documentation (§3 backend box), any ingestion path for an externally written AST (which goes through the parser's validation and then this stage, never around either), and the `manna` CLI.

#### Input contract

- **Gate.** The IR runs only on a parse that reported **zero** diagnostics (`ParserState.diags` empty). The tree alone cannot show a parse was clean: an E0109 file yields a tree identical to a clean one, a recovered statement leaves no placeholder (a later use of a name it would have declared would be a bogus "unknown name"), and E0106/E0107/E0103/E0104/E0110 yield no usable tree. The gate is a quality policy, not a safety dependency: the IR must still not crash on any tree the parser can produce, and the fuzz target runs it ungated to prove that (§5).
- **What a clean parse guarantees** (asserted in debug builds, never a user diagnostic): items are `DECL_STRUCT`/`DECL_FUNCTION` in source order; a function body is a block; `if`/`while`/`for` bodies are blocks; a `for` init is a let/assign/expr statement and its step an assign or expr statement; `EXPR_NAMED_ARG` appears only as a direct element of a call's args and its value is never itself a named arg; an array literal has at least one element; `convert.target` is one of the nine primitive keywords; `unit` never appears; `TYPE_IDENT` holds either a primitive keyword's text or a user identifier (the two cannot collide, primitives being reserved); numbers are unsigned magnitudes (negatives are `UNOP_NEG` over a number); every node has a start-and-end span; no expression tree is taller than `MAX_EXPR_HEIGHT` (512, E0111, below).
- **What it does not guarantee**: names are unresolved; duplicates, unknown attributes and any identifier casing (`_x`, `fooBar`, `X`) arrive unchanged; an assignment target is only postfix-shaped, not an lvalue (`1 = 2;`, `f(x) = 3;`, `u8(x) = 1;`, `[1, 2][0] = 3;`, `(a + b) = 1;` all parse); `break`/`continue` outside a loop, `return` shape, missing returns, literal ranges, array sizes, struct recursion and conversion legality are all unchecked.
- **Spans (implemented, step 2a, commit `02b2363`).** Name spans so diagnostics point at the thing itself (principle 6): a new `Attribute { span, name_span, name }` (`span` covers `[ IDENT ]`, `name_span` the identifier) replaces the bare string in `function_decl.attributes`; `name_span` on `function_decl`, `struct_decl`, `Field`/`Param`, `let_stmt`, `call` (the callee) and `field_access` (the field); `target_span` on `convert`. The dump prints none of them; `test_name_spans` in `parser_fixes_test.c` pins each to exact line:col, including a parenthesized call (whose `name_span` is not at `span.start`) and a multi-line attribute, and was mutation-checked (all ten span sites).
- **Tree height is bounded by the parser (implemented, step 2b): E0111.** Every expression tree the parser builds has height at most `MAX_EXPR_HEIGHT` = **512** (`ast.h`). Height counts the nodes on the longest root-to-leaf path (a leaf is 1; parentheses add nothing, there is no Paren node), computed by the AST constructors into `Expr.height` (`uint16_t`, in existing padding - `sizeof(Expr)` stays 72 - saturating at 65535, not printed by the dump) and checked by `guard_height` after every non-leaf construction (17 sites in `parser.c`). A tree that would exceed it records **E0111**, `expression too long: more than 512 chained operations in one expression; split it into several statements`, at the token that follows the node whose construction crossed the limit, and abandons the file (fatal, like E0103; no fix-it, a documented exception). The IR carries no bound of its own: it **asserts** `height <= MAX_EXPR_HEIGHT`.
- **Why 512 (measured 2026-10-11 with `tools/measure_expr_height.sh`, `make measure-height`; approved N = 512).** The walker measured is the real AST printer (`decl_print`), the recursive pass the bound protects, on an 8 MB stack with the bound disabled; each cell is the largest chain length it survived and, in parentheses, the resulting tree height:

  | Shape | Plain `-O2` | ASan+UBSan |
  |---|---|---|
  | binary `1+1+...` | >= 30000 (30001) | 3420 (3421) |
  | mixed precedence `a+b*c+...` | >= 30000 (30002) | 3419 (3421) |
  | field `a.f.f...` | >= 30000 (30001) | 3420 (3421) |
  | index `a[0][0]...` | >= 30000 (30001) | 3420 (3421) |
  | mixed postfix `a.f[0].f[0]...` | >= 30000 (60001) | 1710 (3421) |
  | chain inside 200 nested calls | >= 30000 (30201) | 3220 (3421) |
  | chain inside 120 nested `if`s | >= 30000 (30001) | 3224 (3225) |

  The rule was fixed before measuring: N = the largest power of two <= (the smallest tree height the printer survives under ASan on 8 MB) / 8, with a floor of 512, and **stop and report** if the quotient falls below it. The smallest height is **3,225** (the shape with 120 enclosing `if`s: statement frames use some stack first; every other shape fails at 3,421, about 2,450 bytes of stack per tree level under ASan at `-O0`), so the quotient is 403, below the floor, and the floor applies: **N = 512, an effective margin of 3,225 / 512 = 6.3x rather than 8x.** 512 is also the smallest value that keeps every existing golden valid: the tallest existing input is `depth_namedarg_255_ok`, at height 511 (255 nested `f(a: f(a: ...))`, two nodes per level). The plain build is not the binding constraint.
  **IR step 3 must re-prove the bound with the real IR builder:** build the IR of a maximal (512-high) tree under ASan at 8 MB, plain and ASan. The printer's frame is about 2.45 KB per level; 512 levels fit in 8 MB up to roughly 16 KB per level, about 6.5x the printer's, so a builder with larger frames than that cannot rely on this margin; N cannot go below 511, so such a builder would have to iterate instead.
  **Consequence for mixed constructs:** the bound is on total height, so an input that is valid under the nesting-depth bound can still be rejected when long chains are combined with deep nesting. Every existing `depth_*_ok` golden is still accepted (tallest: 511).

#### IR shape

A new, separate, structured (tree) typed IR with its own arena and hard cap (its own "too large" code, per `error_codes.md`'s E0110 note); not annotations on the AST, not SSA/CFG. **The AST is never mutated**: where the AST note above says the IR "retags" a `Call`, the IR builds a new `STRUCT_NEW` node. `for` stays a `for` (not desugared to `while`), so `continue` keeps meaning "run the step". Names are pointers into the AST arena's strings; the AST arena outlives the IR.

```
IrType   interned & canonical per module (pointer equality == type equality):
         INT{bits 8|16|32|64, signed} | BOOL | UNIT | ARRAY{len:u64, elem} | SLICE{elem} | STRUCT{decl} | ERROR
         UNIT exists internally (a function with no '-> type' returns it) though it is unwritable.
         ERROR is the poison type; it never appears in a successfully built module.
IrStruct { name, fields[{name, type, span}], size, align }
IrFunc   { name, id "fn:name" (§7 item 5), kernel_hook:bool, params[IrLocal], ret:IrType, locals[IrLocal], body }
IrLocal  { slot (declaration order), name, type, span, is_param }
IrExpr   { kind, type:IrType*, span }, kind one of
           INT   BOOL   LOCAL{slot}
           UNARY{NOT|NEG|BITNOT}    BINARY{op}  (arithmetic ops marked trapping; AND/OR marked short-circuit)
           CALL{func, args[]}       STRUCT_NEW{struct, values[] in declaration order}
           FIELD{base, index}       INDEX{base, idx}     SLICE{base, lo, hi}
           CONVERT{target, operand}   ARRAY{elems[]}     REPEAT{value, count}
IrStmt   LET{slot, init} | ASSIGN{place, value} | IF | WHILE | FOR{init, cond, step, body}
         | BREAK | CONTINUE | RETURN{value?} | EXPR | BLOCK
```

**Guarantees to later stages (they assert these, they do not re-check):** every expression has a non-`ERROR` type; every name is a slot / function / struct / field index; no `EXPR_NAMED_ARG`, no call-to-struct; every conversion is an explicit `CONVERT` (the IR never contains an implicit one); every integer constant is in range for its type; an `ASSIGN` place is a `LOCAL`/`FIELD`/`INDEX` chain only; call arity and argument types match; `break`/`continue` are inside loops; every non-unit function definitely returns; every type's size is within the limit; attribute names are known; naming and uniqueness rules hold.

**Dump format (golden contract, implemented in `tools/mannac/ir_dump.c`; fixed by the first golden `tests/ir/empty_function.ir`, with these forms reviewed at step 3).** The AST dump's S-expression convention (`tag@span`, scalar header, children; types as bare inline text); an expression's type is the **last header scalar**; locals print as `name #slot`; no pointer, hash order or time ever appears (determinism); IR nodes keep the span of the AST node they came from.

```
(fn@2:1-4:2 fn:add
  (params (param@2:8-2:14 a #0 u32) (param@2:16-2:22 b #1 u32))
  (returns u32)
  (block@2:31-4:2
    (return@3:5-3:18
      (binary@3:12-3:17 + u32
        (local@3:12-3:13 a #0 u32)
        (local@3:16-3:17 b #1 u32)))))
```

The example's source is `fn add(a: u32, b: u32) -> u32 {` / `    return a + b;` / `}` on lines 2-4, after `manna v0;`. The signature lines are the real `ir_dump` output for it (a span's end column is exclusive, so a `}` in column 1 ends at column 2, and a parameter's span is `a: u32` without its comma); the body lines are the parser's spans, which the IR keeps, until step 7 lets `ir_dump` print a body. `ir_test.c` pins this text byte for byte.

The full vocabulary (implemented and byte-tested in `ir_test.c`; the first goldens cover only the empty module and the empty function, the rest arrive with their steps): `(int@… V T)`, `(bool@… true|false bool)`, `(local@… name #slot T)`, `(unary@… op T operand)`, `(binary@… op T lhs rhs)`, `(call@… fn:name T args…)`, `(struct_new@… S values…)`, `(field@… name #index T base)`, `(index@… T base idx)`, `(slice@… T base lo hi)`, `(convert@… T operand)`, `(array@… T elems…)`, `(repeat@… count T value)`; statements `(let@… name #slot T init)`, `(assign@… place value)`, `(if@… cond then [else])`, `(while@… cond body)`, `(for@… init cond step body)`, `(break@…)`, `(continue@…)`, `(return@… [value])`, `(expr_stmt@… e)`, `(block@… stmts…)`. An integer prints by its *type's* signedness (`-128 i8`, `18446744073709551615 u64`). Whether an operation traps or short-circuits is a function of its operator and type: neither stored nor printed. A function prints `(fn@… fn:name [kernel_hook]`, its parameters on **one** line `(params (param@… a #0 u32) …)`, `(returns [T])`, then the body. A struct prints `(struct@… Name size=N align=A` with one `(field@… name T +offset)` child per field (first golden at step 4). A module prints its structs, then its functions, each form followed by a newline; **an empty module prints nothing** (the golden runner `tools/run_ir_tests.sh` therefore also requires exit status 0, so a silent crash cannot match an empty golden). The test harness `ir_dump` prints one of: the dump; `ERRORS:` and lines `L:C CODE message` (and, for a file the gate refused, a blank line and `IR: skipped (parse errors)`); or, while the skeleton cannot build a construct yet, `IR: unimplemented` with exit status 3, which no golden can record.

A function with no return type prints `(returns)`, matching `(return_type)`. A unit-typed *expression* (a call to such a function) prints the type text `unit`; the IR dump is output-only, not a source interchange form, so principle 11's "unit is unwritable" is not violated. `i64::MIN` prints `(int@3:5-3:25 -9223372036854775808 i64)`. A module that had errors produces **no** dump: an erroring check yields `ERRORS:` only. IR error lines carry the code (`3:5 E0223 type mismatch: ...`), unlike the parser goldens, which print message text only.

#### Names, scopes, items

- **Namespaces.** Structs and functions share one top-level namespace; declaration order is irrelevant (forward references are legal). Locals are a separate, inner namespace. A call's callee is always looked up in the top-level one (§4: "no indirect calls, no calling an expression's result"). A function or struct name used as a value is an error.
- **Scopes; no shadowing.** Parameters live in the function scope; each block opens a scope; a `let`'s binding is visible only *after* its own statement (so `let x = x + 1;` refers to an outer `x` or fails); a `for` init `let` is scoped to the loop. **Redeclaring a name that is already visible anywhere in the function - the same block or an enclosing one, parameters included - is an error** (a name means one thing within a function: principle 3).
- **Storage lives to function end.** Scoping governs which *names* are visible, not storage: see "Arrays, slices, storage" below.
- **No recursion.** Direct and mutual recursion are rejected (a call-graph cycle check). Rationale (this plan's, not a principle's): loop-counter injection (§3) makes every loop observable, and recursion is a loop with no counter; unbounded stack use is also the opposite of principle 10. This is a restriction, not a feature.
- **Struct declarations.** PascalCase name, unique among all top-level names; field names snake_case and unique within the struct; every field type resolves; a struct may not contain itself by value, directly or transitively; zero fields is legal (the grammar allows `{ }`, and `Empty()` constructs it). A field whose type contains a slice at any depth (`[]T`, `[1][]u8`, ...) is an error, `E0227` (see "Arrays, slices, storage"). A cycle is `E0221`, once per strongly connected component, at its lowest-declaration-index member; structs that only hold a cyclic or oversized struct stay silent. Diagnostics of the declaration pass are grouped by check (all `E0210`, then `E0211`/`E0220`/`E0227` struct by struct, then `E0221`, then `E0222`, then every function signature in turn: attributes `E0214`/`E0215`, parameters in order, return type), each group in declaration order.
- **Function declarations.** snake_case name, unique among top-level names; each attribute known and not repeated; parameters snake_case and unique; parameter and return types resolve; an absent `-> type` means `unit` (§2). A return type that contains a slice at any depth (`[]T`, `[2][]u8`, ...) is an error. Zero parameters is legal.
- **Types.** A primitive keyword is its integer or `bool` type; an `IDENT` names a struct, else error (a function's name used as a type too); `[N]T` is an array, `N` any `u64` including 0 (`[0]T` is legal, §4), subject to the size limit; `[]T` is a slice, and **a slice's element type may not contain a slice** (`[][]u8` is an error in every position, parameters included).
- **Attributes.** The only v0 attribute is `kernel_hook`. An unknown attribute is an error, and it supersedes the naming check (one mistake, one error: `[KernelHook]` is reported as unknown, not also as mis-cased). `[kernel_hook]` is a flag the IR carries; see "`[kernel_hook]` in v0".

#### Types and literals

- **Bidirectional checking.** `check(expr, expected)` and `infer(expr)`. An expected type flows into integer literals, unary `-` of a literal, parenthesized expressions, array-literal elements, `[v; N]`'s value, and both operands of arithmetic/bitwise operators. It does **not** flow into a conversion's operand (conversion is the explicit act), nor from a comparison's `bool` result into its operands.
- **Integer literals have no default type.** An **untyped literal expression** is a `NUMBER`; unary `-` or `~` over an untyped literal expression (`~` added at step 8: `let mask: u32 = ~0;` needs it); a parenthesized untyped literal expression; or a binary arithmetic or bitwise operation whose operands are **all** untyped literal expressions. (A comparison is not one: it yields `bool`. A conversion is not one: it is typed by its target.) Such an expression is typed by an expected integer type, which every literal in it must fit, or by its sibling operand: when exactly one operand of a binary operator is an untyped literal expression, the other operand's inferred type is its expectation, and it flows into the **whole group** (`0 == x`, `1 + x`, `10 < n`, left or right alike; `1 + 2 + x` types as `x`'s type, so each of `1` and `2` is checked against it, and so does `x + (1 + 2)`). With no type from anywhere - `let i = 0;`, `0 == 1`, a conversion's bare-literal operand - it is an error ("cannot infer the type of this integer literal; annotate it"). Literal suffixes were considered and are **not** adopted. Consequence: `for let i: u32 = 0; ...` needs the annotation.
- **The negative-minimum literal, at every signed width** (§2): `UNOP_NEG` directly over an unparenthesized `NUMBER` of magnitude exactly `2^(N-1)` in an `iN` context is `iN::MIN`, for N in 8, 16, 32, 64. `-(2^(N-1))` (parenthesized) and any larger magnitude are plain range errors. A literal that does not fit its type is an error.
- **Poisoning, so one mistake gives one error.** A failed expression gets type `ERROR`, which is compatible with everything and never reported again; a `let` whose initializer or annotation failed still declares its name (type `ERROR`); a call to an unknown function still checks its arguments for their own errors but does not compare arity. Errors are capped at 20 per file (the parser's `MAX_ERRORS` precedent), with their own "too many errors" code.
- **`let`.** `let x: T = e;` checks `e` against `T`; `let x = e;` gives `x` the type of `e`. A `unit` value cannot be bound, passed or operated on; it can only be an expression statement. `let` always has an initializer (grammar), so no uninitialized state exists. Every local and parameter is mutable (v0 has no `const`/`mut`; constants are v1, §6).
- **Conversions.** `PRIM_TYPE ( e )`: target and operand must both be **integer** types; `bool` on either side is an error ("write `x != 0`"; there is no spelling for `u8(b)`); a same-type conversion is legal, a no-op, and kept in the IR; the operand is typed on its own, never from the target; it traps at runtime when the value does not fit (§4 conversion comment).

#### Operators and statements

| Production | Rule |
|---|---|
| unary `-` | Operand a signed integer (unsigned is an error, §2). `-iN::MIN` traps at runtime. |
| `!` / `~` | `!` on `bool`; `~` on any integer. |
| `* / % + -` | Both operands the **same** integer type; result that type. Overflow traps (§2); `/` and `%` by zero **trap**. A type mismatch (`E0261`) carries a fix-it wrapping ONE operand in the legal integer conversion, as two zero-width inserts (the E0108 mechanism, no source text needed): the operand that converts without loss if exactly one does, else the right operand into the left's type (`docs/error_codes.md` E0261). An operand type the operator is not defined for (bool, a struct, an array) is `E0260`, once per expression. |
| `& \| ^` | Same integer type -> same type. `bool` operands are **rejected** (`&&`/`\|\|` are the boolean operators; one way to write it). |
| `<< >>` | Count has the **same integer type** as the left operand; a count >= the bit width **traps** (it is not "arithmetic overflow" as §2 words it). |
| `< > <= >=` | Same integer type both sides -> `bool`. The parser already guarantees at most one comparison/equality per `&&`/`\|\|` operand (E0101/E0108). |
| `== !=` | Same type, integer or `bool` only -> `bool`. Struct and array equality are not in v0 (hidden O(n) cost, principle 10). |
| `&& \|\|` | `bool` both -> `bool`; **short-circuit**. A typed non-bool operand is `E0260`; a literal operand, which has no type to name, is `E0223` (`expected bool, found integer literal`); `!` follows the same two rules. |
| evaluation order | **Strictly left to right** for operands and call arguments (principle 4: exactly one meaning). |
| `if` / `while` / `for` | Condition is `bool`; no truthiness (principle 2). |
| `break` / `continue` | Must be lexically inside a `while`/`for` body (the parser accepts them anywhere). |
| `return` | `return e;` requires a non-unit function and `e` of its return type; `return;` only in a unit function. |
| missing return | A non-unit function must **definitely return**, structurally: its body ends in `return`, or in an `if`/`else` whose two branches both definitely return. Loops never count (`while true { return 1; }` still needs a trailing `return`). Unreachable code after `return`/`break`/`continue` is **allowed** (v0 has no warnings, `diag.h`). |
| expression statement | Any well-typed expression; the value may be discarded. |
| assignment | The target must be an lvalue: `IDENT`, `lvalue.field`, or `lvalue[idx]` (base array or slice). Anything else (a call, conversion, array literal, number, slice expression, any parenthesized target, `(x) = 1` included (the lvalue productions have no parentheses)) is an error. The value is `check(v, type(target))`; types must be equal. |

#### Calls and struct construction

- **Function calls** are positional only; arity and each argument's exact type must match. Named arguments to a function are an error.
- **Struct construction** (`IDENT ( ... )` whose callee is a struct) builds a `STRUCT_NEW`. Every argument must be `name: value` - positional arguments, alone or mixed with named ones, are an error ("a constructor names every field", §8 #4 item 1). The left side must be a bare, unparenthesized `EXPR_IDENT` (`(x): 1` is not bare). The name must be a declared field, given exactly once, **in declaration order**, and each value is checked against the field's type. Evaluation order is the written order, which is the declaration order.
- **Field access** needs a struct base and a declared field; a field-less type or an unknown field is an error (the same "no such field" code construction uses). Neither `x.len` nor any other pseudo-field exists in v0.

#### Arrays, slices, storage

- **Arrays are values.** `let b = a;`, passing, returning and assigning copy `N` elements: a visible copy at the use site with a known O(N) cost (principle 10 satisfied by visibility, and documented here). `[N]T` and `[]T` are different types; an array where a slice is expected is a type mismatch (no implicit conversion).
- **Slices may appear only in parameter and local types, and never nest or escape.** A slice type may be the type of a parameter or of a local. **No return type and no struct field may contain a slice at any depth** (so `[]T`, `[2][]u8` and `[1][]u8` are all rejected there), and **a slice's element type may not contain a slice** (so `[][]u8` is rejected everywhere, as a parameter too). Each rule closes a hole the one before leaves: without the first, a slice of a local array returned from its function (directly, or inside an array) would dangle; without the second, a callee given `out: [][]u8` could store `a[0..4]`, a slice of its own local array, into `out[0]`, where the caller outlives it. (§6 item 5 refers to arenas v0 does not have; until v1 regions exist, this is the rule.)
- **Indexing and slicing.** `a[i]` requires an array or slice base and an index of **any integer type**; the result is the element type. `a[lo..hi]` requires an array or slice base and integer bounds (both mandatory in the grammar); the result is `[]T`. A negative or too-large value is a runtime/verifier out-of-bounds trap, not a type error. A literal out-of-range index is likewise the verifier's (§3).
- **Array literals.** At least one element, all one type (the first element, or the expected element type, decides); the result is `[len]T`. `[v; N]` has type `[N]T`; `v` is evaluated exactly once even for `N == 0`.
- **Backend requirement: every local lives until its function returns.** Every local - parameters and `let`s alike, including those declared in inner blocks - has storage that lasts until the function returns. A name's visibility ends at its block's `}`; its storage does not. The Wasm and native backends (and the verifier's model of them) **must not** reuse, pop or overwrite an inner block's slot before the function returns (in Wasm: slots allocated per function frame, not per block). Together with the rules that slices exist only as parameters and locals and that recursion is rejected, a slice of an inner-block array can never outlive the array, even when it is stored in a local declared in an outer block. The IR stage cannot check this and the dump does not show it; it is a requirement on the backends. Its cost: a frame is the sum of all its locals, with no slot reuse across sibling blocks.
- **No length operation yet.** Nothing in v0 produces `len(x)` or `x.len`, so a `[]T` parameter cannot be iterated to its end, and an array becomes a slice only by writing its length literally (`a[0..4]`). Whether and how to add one is a separate Admission Test item (#15, **open**), to be decided before the IR's array step.

#### Layout and limits

The IR defines the layout (fixed layout, §2): fields in declaration order, natural alignment per field, trailing padding to the struct's alignment, no reordering. No single type's size may exceed `2^31 - 1` bytes (Wasm MVP memory32 addresses, signed-offset friendly); exceeding it, including by arithmetic overflow while computing it, is an error. `[0]T` has size 0. **For the size check only, a slice type counts as 16 bytes, align 8** (pointer + length at 64 bits, the larger of the Wasm and native x86_64 targets), so a type that passes the limit fits on every backend (`[268435456][]u8` is `2^32` bytes and an error, `E0222`); the slice's real in-memory layout is the backend's choice, and its element type is size-checked on its own.

#### Naming conventions

The predicates (the lexer admits `_`-led, digit-bearing, mixed-case identifiers, so §7 item 2's names need definitions): `snake_case = [a-z][a-z0-9]*('_'[a-z0-9]+)*` (no leading, trailing or doubled `_`; `_` alone and `_x` are rejected - v0 has no wildcard); `PascalCase = [A-Z][A-Za-z0-9]*` (no `_`; acronym runs such as `HTTPServer` are allowed). Every declaration site is covered (`let`, parameters, fields, structs, functions; attributes via the unknown-attribute rule). A violation carries a fix-it that renames the declaration **and every resolved use**, so the symbol table records each use, and `DiagFix` becomes an arena-allocated edit list (it holds at most 2 edits today, `MAX_FIX_EDITS`, built for E0108). The fix-it is omitted when the converted name would be empty or would collide with an existing name. The test is: apply the fix, re-check, zero errors.

#### `[kernel_hook]` in v0

The v0 grammar and AST have no pointer type, dereference or intrinsic syntax (§2's raw pointers and hardware intrinsics are not expressible), so nothing a `[kernel_hook]` function can say differs from any other function's. In v0 it is a flag the IR carries and whose spelling it validates; the IR imposes no call restriction between hook and non-hook functions (a backend/verifier question, today unanswerable). The Wasm backend still rejects every `[kernel_hook]` function (§3).

#### Flags raised while planning, and where each is settled

| Flag | Settled |
|---|---|
| F1 literal typing never specified | **Resolved**: §2 literal-typing bullet; "Types and literals" above. |
| F2 negative minimum only for `i64`; pseudo-field test | **Resolved**: §2 generalized, test rewritten with the real `Expr` fields. |
| F3 `[kernel_hook]` describes things v0 cannot express | **Restated**: §2 `[kernel_hook]` bullet and "`[kernel_hook]` in v0" above; the §5 verifier-corpus placeholder says so. |
| F4 §5 files "a type mismatch" under the verifier | **Resolved**: §5 verifier bullet corrected (type mismatches are typed-IR rejections). |
| F5 naming fix-it vs. `DiagFix`'s 2-edit cap | **Resolved**: §7 item 7; "Naming conventions" above. |
| F6 layout and maximum array size unspecified | **Resolved**: "Layout and limits". |
| F7 "the IR stage retags a `Call`" mutates the AST | **Resolved**: the AST note after the node types; "IR shape". |
| F8 evaluation order and short-circuit unstated | **Resolved**: "Operators and statements". |
| F9 division by zero and shift counts not covered by "arithmetic overflow traps" | **Resolved**: same table. |
| F10 no scoping, shadowing, recursion or slice-lifetime rules | **Resolved**: "Names, scopes, items" and "Arrays, slices, storage". |
| F11 no way to read a length | **Restated, open**: "No length operation yet"; Admission item #15. |
| F12 named args to a function / positional struct construction | **Resolved**: "Calls and struct construction". |
| F13 naming predicates undefined | **Resolved**: "Naming conventions". |
| F14 "never a host-stack overflow" is true of the parser only | **Restated**: the sentence in "Universal nesting-depth bound" now says so and points at the planned parser bound (E0111). |

## 5. Test strategy

- **Golden tests per stage:** for each stage, a fixed set of `.mn` inputs with checked-in expected output - token stream for the lexer, AST dump for the parser, typed-IR dump for the IR builder, annotated IR for loop-counter injection, `.wasm` binary (or its `wasm2wat` text form, for diffability) for the backend. Any stage's output changing unexpectedly fails the test; a deliberate change updates the golden file in the same commit as the code change, never silently.
- **Verifier rejection tests:** a corpus of programs that *must* fail verification - out-of-bounds pointer arithmetic, a `[kernel_hook]`-only operation attempted outside one (none is expressible in v0, see "Typed IR (stage 3)"; kept for when one is) - each asserting the specific rejection reason, not just "it failed." Also covers: a `[kernel_hook]` function reaching the Wasm backend must produce exactly the "native backend required" diagnostic, every time. This is also where VISION.md's adversarial-AST test lives once the AI-AST ingestion path exists: same corpus, same verifier, AST-shaped inputs instead of source-shaped ones. Type mismatches are **not** verifier rejections: they are typed-IR errors (E02xx) and are tested by the IR goldens below.
- **Wasm execution tests on macOS:** compile a `.mn` program to `.wasm`, run it under `wasmtime`, assert on stdout/return value. Host-side only - unrelated to M3's bare-metal runtime, purely "does the backend emit correct Wasm."
  - **`tools/fetch-wasmtime.sh`:** downloads the official prebuilt `wasmtime-v49.0.2-x86_64-macos.tar.xz` release asset directly from `bytecodealliance/wasmtime`'s GitHub releases (verified this session: Homebrew's formula has zero bottled platforms - it would build from source regardless of OS, and there's no Rust toolchain on this machine either, so that path is avoided entirely). The script verifies the downloaded tarball's checksum against a pinned value before extracting; the extracted `wasmtime` binary is `.gitignore`d, never committed - it's a fetched build tool, not project source.
  - **M3 seed note:** this prebuilt binary unblocks M2's own testing only. M3 needs a Wasm runtime that runs *inside* Deus, which a host CLI binary can't be. WAMR (already named in CLAUDE.md for M3) is the real candidate; a minimal hand-written C interpreter remains a fallback worth prototyping only if WAMR's bare-metal port turns out impractical - not needed to unblock anything in M2.
- **Determinism test (Step 11, implemented):** `make test-determinism` (`tools/run_determinism_test.sh`) runs every existing lexer and parser golden-test input (the only two stages implemented so far - typed IR and `.wasm` get the same treatment once they exist) through the plain binary twice and byte-compares the two runs, then runs the ASan binary once more and byte-compares that against the plain run too - 215 inputs as of the post-tag recovery fixes (14 lexer + 201 parser; 201 at r1 Phase C, 182 at Step 11), zero drift either way. Required by MANNA_PRINCIPLES.md principle 12 (deterministic builds) and load-bearing for diverse double-compiling (§1): if the C-bootstrap and self-hosted compilers' outputs are ever going to be meaningfully diffed against each other, each one individually producing byte-identical output across repeated runs is the precondition that comparison depends on. This is deliberately a separate check from the golden-file tests themselves (`run_lexer_goldens.sh`/`run_parser_tests.sh`), which only confirm output matches a fixed expected text - this one checks for run-to-run and build-to-build drift directly, independent of whether that output is "correct."
- **Fuzzing (Step 12, implemented):** `make fuzz`/`fuzz-lexer`/`fuzz-parser` (`tools/fuzz/run_fuzzers.sh`, harnesses at `tools/mannac/fuzz_lexer.c`/`fuzz_parser.c`) - libFuzzer + ASan + UBSan, seeded directly from the existing golden-test inputs (so the parser fuzzer starts mutating from every `err_recovery_*.mn` instead of from nothing). Apple's Xcode clang has no libFuzzer runtime at all; the script uses brew's keg-only llvm instead. Deliberately not in CI - a fixed wall-clock budget, not a pass/fail check. The parser binary is additionally built with `MANNA_FUZZ_TRACE_LAST_RESORT` (compiled out of every other build), which turns reaching `parser_parse_file`'s last-resort single-token-skip backstop (see "Error recovery" above) into an immediate, saved crash - a direct empirical check of the "unreachable under the current grammar" claim, not just reasoning. First run: lexer 2,328,111 execs/181s, parser 480,689 execs/601s, zero crashes/timeouts/sanitizer reports on either target, and the last-resort backstop was never hit. Every crash or timeout found in any run becomes a golden test (minimized, then copied into `tests/lexer`/`tests/parser` with a name describing what broke) - none did this time.

- **Typed IR tests (specified 2026-10-11, not built):** goldens `tests/ir/*.mn` -> `*.ir` (`make test-ir`/`-asan`, a test-only `ir_dump` harness, whole-file mode), printing the IR dump or `ERRORS:` with a code on every line; one golden per rule row above, at least one per code, a no-cascade golden per poisoning path, and a script that proves every `E02xx` in `error_codes.md` has a golden. The same determinism treatment as the other stages. A `fuzz_ir` target (lexer -> parser -> IR) with a compile-flag-only **ungated** mode that runs the IR on whatever tree the parser produced even when it reported errors - the empirical check that the parse-diagnostics gate is policy, not a safety dependency. Parser and lexer goldens stay byte-identical throughout. A debug assertion that no input tree exceeds the parser's `MAX_EXPR_HEIGHT`, and a test that builds the IR of a maximal (512-high) tree under ASan at 8 MB, so IR step 3 re-proves the parser's bound with the real builder. Plan and step list: `docs/TYPED_IR.md`.

## 6. v1 roadmap

Documentation only — nothing here is v0, nothing here is implemented or scheduled with code yet. Priority order:

1. **`f32`/`f64`.** User-space (Wasm) only until the kernel has XSAVE — the kernel currently builds with `-mgeneral-regs-only` (CLAUDE.md hard constraint) specifically to avoid saving FPU/SSE state in ISRs, so no floating-point value can safely exist in kernel-side (`[kernel_hook]`) code until that changes. Float semantics — NaN handling, rounding mode — are specified in v1, not assumed now.
2. **Strings.** Needed for self-hosting (§2) and for any real diagnostic/identifier handling beyond what v0's fixed-layout approach supports.
3. **Constants and enums.** Not in v0; naming and syntax undecided.
4. **Modules.** Lowest priority of the four — v0's single-translation-unit model is sufficient until the codebase is large enough that namespacing actually hurts without it.

**v1 leanings** (decided at v1; no code now — recorded here per `docs/MANNA_PRINCIPLES.md` Part 4):

5. **Memory model: arenas/regions.** A slice can't outlive its arena.
6. **Optionals and errors.** `?T` is an optional value; `!T` is a value-or-error. Errors propagate with the keyword `try` (principle 9: words over symbols, not a `?`-postfix sigil). Discarding a `!T` is a compile error — matching Go's "ignored error codes" lesson in `MANNA_PRINCIPLES.md` Part 3.
7. **Contracts (`requires`/`ensures`).** Proven at compile time where possible, otherwise a runtime trap. Never disableable — a contract that can be compiled out isn't a contract, it's a comment.
8. **Effects.** Pure by default; a function declares anything else with `uses io, alloc`. `hw` is legal only in `[kernel_hook]` functions. A function's callees can't exceed its own declared effects — an effect-pure function can't call an effect-impure one.
9. **Intent tags.** Optional `[intent: "..."]` annotations, searchable and comparable against actual behavior — `MANNA_PRINCIPLES.md`'s third AI-native candidate feature, grouped here alongside contracts and effects since all three need the same Feature Admission Test treatment before landing in any version.

This section is the standing home for every v1 lean as it gets recorded, not just these nine.

## 7. Language design decisions (v0, locked)

Per `docs/MANNA_PRINCIPLES.md` Part 4 — recorded here so the implementation blueprint states them directly, not just the charter.

1. **File header is a required statement.** The first non-comment line of every file is `manna v0;` — see `version_decl` in the grammar (§4) above. A missing or wrong version is a compile error, not a warning.
2. **Naming is enforced as compile errors, not warnings.** Functions, variables, and fields: `snake_case`. Structs (and future enums): `PascalCase`. Constants (v1): `SCREAMING_SNAKE`. Attributes: `snake_case` — e.g. `[kernel_hook]`.
3. **Source file extension: `.mn`.** Already the extension every test file in `tests/lexer/` uses.
4. **Error JSON schema**, for `manna check --json` (implemented after the parser exists — not yet):
   ```json
   {"schema":1,"file":"a.mn","diagnostics":[{
     "code":"E0102","severity":"error",
     "message":"assignment is a statement; did you mean '=='?",
     "span":{"start":{"line":3,"col":6},"end":{"line":3,"col":7}},
     "expected":"'{'","found":"'='",
     "fixes":[{"title":"use comparison","edits":[
       {"span":{"start":{"line":3,"col":6},"end":{"line":3,"col":7}},"text":"=="}]}]}]}
   ```
   Error codes (`E0xxx`) are stable and documented in a registry. This is why every AST node needs both a start **and end** position, not just a start (the parser grammar's AST node types section, §4, is updated to reflect this — spans are now `start-end`, not just a single point).
5. **AST IDs are name-based, top-level items only, in v0** — e.g. `fn:parse_expr`, `struct:Token`. Inner-node IDs (for addressing a specific statement or expression inside a function body) are deferred to v1.
6. **Tooling: one public binary, `manna`**, with subcommands `build`, `check`, `test`, `fmt`, and `doc`. `mannac` (`tools/mannac/`) is the internal source folder and library name only — never a binary a user runs directly.
7. **Naming-convention enforcement (principle 2 above) lives at the IR stage (E02xx), not the parser (E01xx)** — decided during Step 8, when the question became concrete (struct/function/field/param declarations are the first place names get checked). Consistent with unknown attributes and duplicate names also being IR-stage errors (item 8 below): one semantic-validation pass, not checks scattered across parser and IR, even though naming is purely syntactic and could in principle be checked the moment an IDENT is parsed. Not implemented in Step 8 - the parser accepts any syntactically-valid identifier at a declaration site for now. Requirements for when it is implemented: covers every declaration site (`let`, params, fields, structs, functions, attributes - not just structs/functions), and every violation carries a fix-it hint with the correctly-cased name (the `DiagFix` mechanism E0102 already uses, extended: a rename is one edit at the declaration plus one per resolved use, so `DiagFix` becomes an arena-allocated edit list - decided 2026-10-11; the casing predicates are under "Typed IR (stage 3)").
8. **Unknown attributes and duplicate names are IR-stage errors, not parser errors.** The parser accepts any `[IDENT]` attribute list and any declaration name without checking whether the attribute is recognized or the name collides with another declaration - those require a symbol table, which doesn't exist until the IR stage. Not implemented in Step 8.

## 8. Feature Admission Test records

The five-item test is `docs/MANNA_PRINCIPLES.md`'s "Feature Admission Test". One record per language change that went through it; r1 Phase B (#4, #5, #6) is the first batch. Item 2 requires golden tests, which can't exist for spec-only changes, so each record was written as "pending admission (goldens due in #4 step)". **At the close of the #4 parser step (step 7), all three are ADMITTED:** every golden the record required exists, passes in plain and ASan builds, and is named below (the planned names in the Phase B text were adjusted where the real ones differ). A record is only upgraded to "admitted" with its goldens cited; the status line at the head of each record says so. The typed-IR decisions of 2026-10-11 are the second batch, #7-#15 below (all pending admission; #15 open).

### #4 - struct construction, array literals, conversions, reserved primitive names

**Status: ADMITTED (step 7).**

1. **Principles / locked decisions:** no violation found. Supports principle 2 (conversions are explicit; no zero-initialized or partially-initialized aggregates), 3 (a constructor names every field at the point of use), 5 (LL(1) re-verified, §4), 9 (`u64(x)` is a word-led form, not a cast symbol or `as`), 6 (diagnostics registered). §7 items 1-8 are untouched; item 2 (naming) is what keeps `Point(...)` and `f(...)` visually distinct. One tension recorded under item 5 below.
2. **Spec entry / goldens / errors:** spec = §2, §4 grammar + "Reserved words, constructors, and conversions". Errors = generic E0105 sites documented as exceptions (no dedicated message adds information); IR-stage checks listed without codes, none shipping. **Goldens: all written and passing** (plain + ASan, `make test-parser`): reserved words - `tests/lexer/keywords_types` (whole-token matching), `err_reserved_word_{let_name,fn_name,param_name,field_name,struct_name,attribute}`, `err_unit_type_positions`, `prim_types_every_position`, `reserved_words_whole_token_only`, with all 22 type-using parser goldens byte-identical and `tests/lexer/basic.tokens` changed in exactly the 8 `IDENT`->`KW_*` lines; conversions - `convert_*` (basic, nested, every type, `bool(x)`/`u8(true)` parse, as statement, as assignment target) and `err_convert_*` (`u8()`, `u8(a, b)`, bare `u8`, `u8(x)(y)`, `unit(x)`, unclosed); arrays - `array_*`, `repeat_{basic,zero,one,hex,nested_value,of_conversion}`, `err_array_*`, `err_repeat_*`; construction - `namedarg_*` (struct construction, mixed, non-name left side, nested, array/conversion values), `err_namedarg_*`; the `'['` peek - `peek_*`; recovery regressions - `err_recovery_array_shaped_attribute` (the `[IDENT] fn` misfire), `err_recovery_semi_in_bracket_not_boundary` and `err_recovery_repeat_in_bracket_cascade` (the repeat-literal skip cases), `err_recovery_array_unclosed_then_{let,rbrace,fn}`, `attribute_vs_array_toplevel`; depth, **measured not predicted** - `depth_conv_*`, `depth_array_*`, `depth_repeat_*`, `depth_namedarg_*` (255 ok / 256 and 300 fail each).
3. **Scope:** v0. §2 already puts fixed-size arrays and structs in v0 ("driver and microkernel task formatting need them now") but defined no way to write a value of either, and with no implicit conversions (principle 2) mixed-width integer code cannot be written at all without explicit conversion. These complete v0's type story; no v1 feature is pulled forward.
4. **Machine cost (Wasm MVP, per construct; exact instruction sequences get recorded when the backend lowers each one, §3):** struct construction = one store per field into a stack/linear-memory slot, no allocation (no heap in v0), O(fields). Array literal = one store per element, O(N). `[v; N]` = `N` is a compile-time `NUMBER`, so a statically-known O(N) store loop, **lowered as a counted loop, not `memory.fill`** (bulk-memory is outside the MVP target, §2) and so subject to loop-counter injection like any loop. Conversion (**specified**; the exact sequences are fixed when the backend is written, and assume the backend keeps sub-32-bit values normalized in an `i32`, a backend decision): value-preserving widening is zero or one instruction (`u8`->`u32` and `i8`->`i32` are free; `u32`->`u64` is `i64.extend_i32_u`, `i32`->`i64` is `i64.extend_i32_s`). Anything that can fail is **a compare-and-trap**: the operand is duplicated through one scratch local (`local.tee`), tested, and a forward `if` branches to `unreachable` - roughly 4 instructions for a sign test (`i32`->`u32`, `u32`->`i32`, signed->wider-unsigned) and 5-7 for a range test (`u32`->`u8`; `i32`->`i8` as one biased unsigned compare), plus the extend when widening too; 64-bit forms use the `i64` equivalents at the same count. Per conversion site: O(1), one extra local, one never-taken forward branch, no loop, no call, no memory access; the trap itself is Wasm's `unreachable`, the same mechanism overflow traps use. The verifier may drop the check where it proves the value in range. The native backend's cost (compare plus a conditional jump to a trap stub) is recorded when that backend is written, not here. The v0 maximum array size is an IR-stage limit, not specified here.
5. **Second way to say the same thing?** Struct construction is the *only* way to create a struct value (`let` always requires an initializer, so declare-then-assign-fields doesn't exist). Conversion has exactly one spelling (no `as`, no implicit form; `bool`<->integer is not a conversion in v0, so `bool(x)` is rejected in favor of `x != 0` and `u8(b)` has no spelling at all). **Overlap closed by a formatter rule, not accepted as a tension:** `[0, 0, 0]` and `[0; 3]` mean the same, so the canonical form is fixed by `manna fmt` (§7 item 6; principle 1: "the official formatter defines the layout... the same meaning always produces the same text"). **Formatter requirement (recorded here; `fmt` is unbuilt, so this binds it when it's written):** (a) an array literal of length >= 2 whose elements are all the *same literal token* - same token kind and same lexeme, so `[0, 0, 0]` and `[true, true]` qualify while `[0, 0x0]` and `[x, x]` do not - is rewritten to `[v; N]` (`[0, 0, 0]` -> `[0; 3]`); (b) a repeat literal with `N == 1` is rewritten to the list form (`[v; 1]` -> `[v]`), which the user's rule (a) leaves otherwise as a second spelling of `[v]` - flagged as an addition to confirm; (c) every other array literal is left as written. After (a)-(c) each value has exactly one canonical text. This is a formatter requirement only: both forms stay legal *input* to the parser, and the compiler treats them identically.

### #5 - at most one comparison/equality operator per `&&`/`||` operand

**Status: ADMITTED (step 7).**

1. **Principles / locked decisions:** principles 1 (parentheses are the one way to combine them), 2, 5, 6; extends Part 3's "Chained comparisons" and "C operator-precedence traps" rows. No locked decision touched. **It does reverse currently-accepted, golden-tested behavior** (`a<b==c`, `a==b<c`), which is not a locked decision but is called out in the grammar comment and must be confirmed.
2. **Spec / goldens / errors:** spec = §4 `relational` + comment; E0108 registered with an applicable fix. **Goldens: all written and passing:** the two flipped goldens, now `err_mixed_cmp_eq_{lhs,rhs}` (valid -> E0108; they kept their old `comparison_wrapped_by_equality_*` names until the v0 audit fixes renamed them), plus `err_mixed_*` (`ge_ne`, `three_ops`, `eq_lt_eq`, `in_and_operand`, `statement`, `two_statements`, `assign_rhs`, `if_condition`, `for_step`, `for_cond`, `return`, `call_arg`, `second_operand_unparseable`, `full_chain`), the valid pair `relational_parenthesized_{lhs,rhs}`, `relational_{in_logical_operands,each_operand_one,bitand_equality,unary_not}`, the E0101 side `err_chained_{same_cmp,same_eq,cross_cmp,eq_ne,le_gt}` (unchanged messages), and the E0108 fix-it positions pinned in `parser_fixes_test.c`. One unanticipated consequence: `full_chain_smoke` had its input parenthesized (`c==(d<e)`) so it still walks every precedence level.
3. **Scope:** v0; a parser-level restriction on existing operators.
4. **Machine cost:** none - compile-time only, no runtime construct added or changed.
5. **Second way?** It removes one: `a < b == c` and `(a < b) == c` meant the same thing; now only the second is writable.

### #6 - reserve `unit`; `-> unit` is an error; absent return type dumps as `(return_type)`

**Status: ADMITTED (step 7).**

1. **Principles / locked decisions:** principle 1 (omitting the annotation is the single way to say "returns nothing"), 15 (signatures fully typed - absence is already the stated, not inferred, form per §2), 11 (the interchange form must have one rendering per meaning), 6. No locked decision touched.
2. **Spec / goldens / errors:** spec = §2, §4 `function_decl` comment, golden-format bullet; E0109 registered with an applicable fix. **Goldens: all written and passing:** `err_return_unit{,_newline,_comment,_line_comment,_body_also_has_error,_between_valid_fns}` (E0109, the function stays in the tree), `err_return_unit_{array_element,then_bracket,missing_body}` (stay E0105), `return_type_absent`, `unit` in every other position in `err_unit_type_positions` and `err_reserved_word_*`, the E0109 fix-it forms pinned in `parser_fixes_test.c`, and the dump change proven by script: of the 35 goldens containing `(return_type unit)`, 34 reproduce byte for byte when `(return_type)` is reverted (the 35th, `err_unit_type_positions`, also gained E0109); every other golden byte-identical.
3. **Scope:** v0.
4. **Machine cost:** none - no runtime construct.
5. **Second way?** It removes one: `fn f() {}` and `fn f() -> unit {}` can no longer both be written.

**Second batch: typed IR records (#7-#15).** The second batch, from the typed-IR plan (`docs/TYPED_IR.md`), decided 2026-10-11. Every record is **pending admission**: the spec entry exists ("Typed IR (stage 3): decided rules", §4), the goldens do not - they are due in the IR step named in each record, and a record is upgraded to "admitted" only with its goldens cited. #15 is **open** (not yet decided).

### #7 - integer literals have no default type; the negative-minimum rule at every signed width

**Status: pending admission (goldens due in IR step 6).**

1. **Principles / locked decisions:** supports 2 (no implicit choice of a type) and 4. No locked decision touched; §2's `i64::MIN` paragraph is generalized, not reversed. Cost to readers: `let i = 0;` is an error and `for let i: u32 = 0; ...` needs its annotation; the §4 conversion examples were rewritten.
2. **Spec / goldens / errors:** spec = §2 literal-typing bullet and the generalized negative-minimum paragraph; "Types and literals". Goldens due: the minimum matrix at all four signed widths (`-128: i8` ok, `-(128)` and `-129` rejected, `-9223372036854775808: i64` ok, `-(9223372036854775808)` rejected, in `u64` rejected), range edges per width, literal-on-the-left (`0 == x`, `1 + x`, `10 < n`) and their mirrors, **literal-only groups (`1 + 2 + x` and `x + (1 + 2)`, both typed as `x`)**, `256 + x` with `x: u8`, `0 == 1`. Errors: literal out of range; cannot infer a literal's type (E02xx, numbers at emission).
3. **Scope:** v0; completes the existing "no implicit conversion anywhere". Literal suffixes (a lexer/grammar change) were considered and not adopted.
4. **Machine cost:** none at runtime (compile-time typing only).
5. **Second way?** Removes one: with no default there is a single way to give a literal a type (context).

### #8 - no shadowing; order-independent top level; no recursion

**Status: pending admission (goldens due in IR steps 4, 7, 10).**

1. **Principles / locked decisions:** principles 3 (a name means one thing within a function), 4, 10. No locked decision touched. The recursion rationale (loop-counter injection makes loops observable; recursion is a loop with no counter) is this plan's reasoning, not a principle.
2. **Spec / goldens / errors:** spec = "Names, scopes, items". Goldens due: redeclaration in the same block, in an inner block, of a parameter; forward references; direct and mutual recursion. Errors: duplicate declaration; recursion not allowed.
3. **Scope:** v0; all three are restrictions, no feature is added.
4. **Machine cost:** none at runtime; rejecting recursion makes stack use a static property of the call graph.
5. **Second way?** Removes one (an inner `let x` reusing an outer name).

### #9 - operator semantics and index types

**Status: pending admission (goldens due in IR steps 8 and 12).**

1. **Principles / locked decisions:** principles 2, 4 (exactly one meaning: shift counts, division by zero, evaluation order, short-circuit), 10. §2's "all arithmetic overflow traps" is extended to shift counts and zero divisors, not reversed.
2. **Spec / goldens / errors:** spec = "Operators and statements" and "Arrays, slices, storage" (index types). Goldens due: one positive and one negative per operator row, shifts with mismatched count types, `bool` with `&`/`|`/`^`, `==` on structs/arrays, an index of each integer type. Errors: operator not defined for a type; operand types differ (with its fix-it).
3. **Scope:** v0.
4. **Machine cost (specified; exact sequences recorded when the backend lowers each construct, §3):** a shift adds a count compare and a never-taken forward branch to `unreachable`; `&&`/`||` lower to a forward branch; Wasm's integer division already traps on a zero divisor, so `/` and `%` by zero need no extra instruction on that target; an index is a bounds check or a verifier proof (§3).
5. **Second way?** Removes two: `&`/`|`/`^` on `bool` (the boolean operators are `&&`/`||`) and struct/array equality spelled by hand against a built-in one.

### #10 - arrays by value; slices only in parameter and local types, never nested or escaping; locals live to function end

**Status: pending admission (goldens due in IR steps 4, 5, 7, 12; the storage rule is a backend requirement, verified when the backend lands).**

1. **Principles / locked decisions:** principles 4 (no dangling slice), 10 (an array copy is visible at its use site and costs O(N)), 2. No locked decision touched; §6 item 5's "a slice can't outlive its arena" is made true in v0 by restriction, with no arenas.
2. **Spec / goldens / errors:** spec = "Arrays, slices, storage". Goldens due: array copy by `=`, by call, by return; a slice as parameter and local (ok); a slice as a return type and as a struct field (rejected); **the three escape cases: return type `[2][]u8`, struct field `[1][]u8`, parameter `[][]u8` (all rejected)**; array-vs-slice mismatch. Errors: type mismatch; slice not allowed here (a return type or field containing a slice at any depth, or a slice of slices). The storage rule has no IR golden; it is a requirement on the backends, listed in the §3 backend box.
3. **Scope:** v0; restrictions only.
4. **Machine cost:** an array assignment, argument or return is a copy of `N` elements (O(N), no allocation); a slice is pointer + length, no copy; frames hold every local of the function with no slot reuse across sibling blocks.
5. **Second way?** None added. Removes the way to return a slice.

### #11 - struct layout and the maximum type size

**Status: pending admission (goldens due in IR step 4).**

1. **Principles / locked decisions:** principles 4, 10; fills in §2's "fixed layout". No locked decision touched.
2. **Spec / goldens / errors:** spec = "Layout and limits". Goldens due: sizes and alignments of mixed-width structs (observable through the size limit), a type just under and just over `2^31 - 1` bytes, a size computation that overflows `u64`. Error: type too large.
3. **Scope:** v0.
4. **Machine cost:** none added; padding bytes are real storage, now specified.
5. **Second way?** No.

### #12 - naming-convention predicates and rename fix-its

**Status: pending admission (goldens due in IR step 14).**

1. **Principles / locked decisions:** implements §7 item 2 and item 7 (naming as compile errors, each with a fix-it); principle 1 (one canonical spelling), 6. §7 item 7's "same `DiagFix` mechanism" is extended (an edit list), not reversed.
2. **Spec / goldens / errors:** spec = "Naming conventions". Goldens due: a case-converter table (`fooBar`->`foo_bar`, `HTTPServer`->`http_server`, `X`->`x`, `_x` and `__x`), rename fix-its covering declaration plus every use, collision and empty-result cases omitting the fix. Errors: not snake_case; not PascalCase.
3. **Scope:** v0.
4. **Machine cost:** none (compile-time).
5. **Second way?** Removes the casing choice.

### #13 - call and struct-construction forms

**Status: pending admission (goldens due in IR steps 10 and 11).**

1. **Principles / locked decisions:** principles 1, 2, 3; completes #4 (struct construction), which left two cases unaddressed. No locked decision touched.
2. **Spec / goldens / errors:** spec = "Calls and struct construction". Goldens due: named arguments to a function, positional and mixed struct construction, a non-bare or parenthesized field name, unknown/missing/repeated/out-of-order fields, field access on a field-less type and on an unknown field. Errors: one per case, the unknown-field code shared by construction and access.
3. **Scope:** v0.
4. **Machine cost:** as #4 item 4 (one store per field); arguments evaluate left to right.
5. **Second way?** Removes two: named arguments for functions, and field order in a construction.

### #14 - a tree-height bound in the parser

**Status: ADMITTED (step 2b).**

1. **Principles / locked decisions:** principles 4 and 12 (a bound the compiler enforces, not a host crash). No locked decision touched. Motivation, measured: the nesting-depth bound counts recursion cycles, but loop-built chains make a tree as tall as the input is long; `expr_print` overflowed an 8 MB stack under ASan at 20,000 terms (`docs/STATUS.md`, Known issues, now fixed).
2. **Spec / goldens / errors:** spec = "Input contract" above (final wording, the measurement table and the reasoning for N = 512 are there). Diagnostic **E0111**, registered in `docs/error_codes.md`, no fix-it (documented exception). **Goldens, all passing, plain and ASan:** `height_{binary,mixed_prec,field,index,postfix_mix,in_calls,in_ifs}_fail` (height 513, the bound plus one; each expected position was computed by hand first and matched) and `height_{binary,field,index,postfix_mix}_far_fail` (ten times the bound). The accepted side is unit tests, not goldens (the dump of a height-512 tree is about 1 MB): `test_height_bound_accepted_side` (height 512 accepted in all seven shapes and walked by the real printer under `ulimit -s 8192`), `test_height_bound_fatal_and_reuse`, `test_height_guard_every_site` (all 17 `guard_height` sites trip at the bound at a pinned column and pass one level lower; every site was removed in turn and each removal was caught, and both `>=` and `> N + 1` boundary mutations were caught by the unit tests and by 11 goldens), and `test_expr_height` in `ast_test.c` (heights per node kind, saturation, `sizeof(Expr) == 72`). N was measured (`make measure-height`) and approved before it was written down. **Reviewer's Linux run (gcc 13, ASan+UBSan+LSan, the `v0.1` pack):** the 11 goldens, the unit tests and the 512/513 boundary clean; gcc ASan survived tree height >= 30000 in every shape, so the macOS ASan figure (3,421; 3,225 with 120 enclosing `if`s) remains the binding one. Tagged `m2-parser-v0.1`.
3. **Scope:** v0; a limit like E0103.
4. **Machine cost:** none at runtime; one compare per non-leaf expression node at parse time, and a `uint16_t` in existing padding (`sizeof(Expr)` unchanged at 72).
5. **Second way?** No.

### #15 - a length operation (`len`) - OPEN

**Status: OPEN - not decided, no spec entry. Must be decided before IR step 12 (arrays), which is blocked until then.**

1. **Principles / locked decisions:** principle 18 names `len` as the stdlib convention, but v0 has no stdlib or builtins; the real design question is the first builtin mechanism (function-like `len(x)`, a pseudo-field `x.len`, or none). Not evaluated here.
2. **Spec / goldens / errors:** none yet.
3. **Scope:** a language addition; must be argued as v0 scope (item 3 of the test).
4. **Machine cost:** a slice carries its length (pointer + length, §2), so reading it is O(1); an array's length is a compile-time constant.
5. **Second way?** Without it an array becomes a slice only by writing its length literally (`a[0..4]`); with it, `a[0..len(a)]` would be a second spelling of the same slice, which the record must address.

## Working log

- Lexer (`tools/mannac/lexer.c`/`.h`) and its golden tests (`tests/lexer/`, `make test-lexer`) are the first implementation step out of this blueprint. Diffs and test output are reviewed separately from this document, per the usual process - nothing here implies they're already committed.
