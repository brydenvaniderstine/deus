# Milestone 2 Blueprint — The Manna Compiler (host-side, macOS)

Text blueprint only — no implementation here beyond the lexer named in the working log below. See `CLAUDE.md` for the hard constraints this must not violate, and `VISION.md` for longer-range context this does *not* assume.

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

- **Types:** `u8`/`u16`/`u32`/`u64`, `i8..i64`, `bool`, fixed-size arrays, structs (fixed layout, no tagged unions/variants yet). No generics, no traits, no interfaces.
- **Functions:** named functions, fixed arity, no overloading, no closures, no varargs.
- **Loops:** `for` (C-style three-clause) and `while` only. No `foreach`/iterators — those need a trait/protocol system v0 excludes.
- **Safe pointers:** a single pointer kind, bounds-carrying (pointer + length, not a bare address) for anything outside `[kernel_hook]`. Raw/bare pointers don't exist in v0 source — they're an IR-level concept the verifier permits only inside verified `[kernel_hook]` regions.
- **`[kernel_hook]` attribute:** marks a function as allowed raw pointer arithmetic and hardware intrinsics (port I/O, `cpuid`, MSR access — the exact primitives `kernel/arch/x86/*.c` already uses). Everything else in a `[kernel_hook]` function is still typed-IR-checked; it's an escape hatch from the safe-pointer rule only, not from the pipeline.
- **Explicitly excluded from v0:** generics, traits/interfaces, closures, exceptions/unwinding, operator overloading, modules/namespaces, dynamic dispatch, heap allocation, strings.
- **Wasm backend target (v0): Wasm MVP only** — no threads, no SIMD, no GC proposal features. (Already noted inline in the pipeline-stages diagram below; restated here as an explicit scope decision, not just an implementation detail of one pipeline box.)
- **Signed integers, overflow, and the negative-min literal:** `i8`/`i16`/`i32`/`i64` are first-class alongside `u8`-`u64` (already listed above). Unary `-` applied to an unsigned-typed operand is a type error, caught in the typed-IR stage — the parser has no type information yet, so it parses identically to the signed case; rejection happens one stage later. All arithmetic overflow, signed and unsigned, **traps at runtime** — no silent wraparound, no undefined behavior, in either direction. The most-negative `i64` literal (`-9223372036854775808`) needs special handling in the typed-IR stage specifically: the lexer only ever produces unsigned-magnitude `NUMBER` tokens, so `9223372036854775808` (2^63) lexes fine as an in-range `u64` value with no lexer-level overflow (confirmed: well under `2^64-1`); the IR stage must recognize negation applied directly to a literal of magnitude exactly `2^63` in an `i64` context as meaning `i64::MIN` — not validate the unsigned magnitude as a standalone `i64` first (which would wrongly fail, since positive `i64` only reaches `2^63-1`) and negate second.

**Self-hosting requires v1 features (strings, heap). Structs moved to v0.** Structs are in v0 (see Types, above) — driver and microkernel task formatting need them now, not at some later self-hosting milestone. What v0 still doesn't have, and what self-hosting `mannac` itself in Manna still needs, is a heap allocator (for AST nodes and other dynamic structures) and strings (for identifiers, source text, diagnostics). Until v1 adds those, self-hosting isn't attempted — v0's C bootstrap compiler is the only `mannac` that exists, by design.

## 3. Pipeline stages

```
source (.manna)
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

## 4. Parser grammar (v0, approved)

Text plan only — no parser code exists yet. Recursive-descent, built on the lexer above.

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
     those bodies. *)
param_list        ::= param { ',' param } ;
param             ::= IDENT ':' type ;

type              ::= IDENT
                    | '[' INTEGER ']' type   (* fixed-size array *)
                    | '[' ']' type           (* safe slice *)
                    ;

block             ::= '{' { statement } '}' ;

statement         ::= let_stmt | if_stmt | while_stmt | for_stmt
                    | return_stmt | break_stmt | continue_stmt
                    | postfix_stmt | expr_stmt ;
  (* postfix_stmt and expr_stmt have disjoint FIRST sets - postfix_stmt
     starts with whatever primary/postfix starts with (NUMBER, KW_TRUE,
     KW_FALSE, IDENT, '('); expr_stmt, reached only when none of those
     applied, starts with a unary prefix ('!', '-', '~'). Genuinely
     LL(1) at this level, unlike the assign_stmt/expr_stmt split it
     replaces. *)

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
logic_and         ::= equality { '&&' equality } ;
equality          ::= comparison [ ( '==' | '!=' ) comparison ] ;
comparison        ::= bitor [ ( '<' | '>' | '<=' | '>=' ) bitor ] ;
  (* Non-associative: at most one comparison/equality operator per
     expression. `a < b < c` parses "a < b" here, leaves the second
     '<' unconsumed, and fails one level up. *)
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
                    | '(' expression ')'
                    ;
arg_list          ::= expression { ',' expression } ;
```

### Dedicated diagnostics (implementation requirements, not expressible in EBNF)

- **Chained comparison:** immediately after parsing one `comparison`, if the lookahead token is itself a comparison/equality operator, raise this exact diagnostic right there: `"comparisons can't be chained; use a < b && b < c"` — not a generic "expected X, found Y" surfacing later from whatever construct happens to be one level up.
- **Assignment-as-condition:** when `if`/`while` is expecting the condition-terminating `'{'` and finds `'='` instead, raise this exact diagnostic: `"assignment is a statement; did you mean '=='?"` — the single most likely reason someone hits this exact token pair, given `expression` structurally can't contain `'='` anymore.

### Universal nesting-depth bound

One shared counter, max **256 depth units** — not 256 levels of any particular construct. Each recursive production that can genuinely recurse (not every parse function; see `tools/mannac/parser.c`'s own "INVARIANT" comment for the precise rule: every recursion *cycle* must pass through at least one counted function) costs 1 unit per level. A single construct - nested parens, a `unary` self-recursion chain (`!!!!!!...x`), nested index/slice (`a[a[a[...]]]`), nested calls (`f(f(f(...)))`), or (once implemented) nested array types (`[1][1][1]...T`) - costs exactly 1 unit per level, so **255 levels of any one of them parses successfully; 256 is the first to cleanly fail** with the same parse error ("maximum nesting depth (256 units) exceeded"), never a host-stack overflow. The 255-not-256 figure is because the innermost operand itself also costs 1 unit (255 wrapping levels + 1 innermost = 256, the exact cap). Mixed constructs cost more per level - e.g. `!(!(!(...)))` costs 2 units per level (1 from `unary`'s own recursion, 1 from the paren re-entering the full expression grammar), capping at **127 levels**, not 255. Empirically proven for expressions in `tests/parser/depth_*` (parens/unary/index/calls at 255 vs. 256 and 300, mixed nesting at 127 vs. 128 and 300, all under both plain and ASan builds, zero crashes) - not just asserted from reading the code. Nested blocks and array-type wrappers get the same treatment once those productions exist (M2_PLAN.md's parser steps 6-8).

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
```

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

This dump format **is** the canonical machine interchange form (principle 11: the AST has a stable serialized form, and AI agents may emit it directly) — not just a test-golden convenience, so the rendering rules are spec, not implementation detail:

- **`Field`/`Param` carry spans**, same as every other node (corrected above - the abstract shorthand omits one, the real type doesn't).
- **An absent `-> type` renders as `(return_type unit)`**, explicit, never omitted - matching "stated, not inferred" (§2).
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

## 5. Test strategy

- **Golden tests per stage:** for each stage, a fixed set of `.manna` (or, for the lexer specifically this session, `.mn`) inputs with checked-in expected output - token stream for the lexer, AST dump for the parser, typed-IR dump for the IR builder, annotated IR for loop-counter injection, `.wasm` binary (or its `wasm2wat` text form, for diffability) for the backend. Any stage's output changing unexpectedly fails the test; a deliberate change updates the golden file in the same commit as the code change, never silently.
- **Verifier rejection tests:** a corpus of programs that *must* fail verification - out-of-bounds pointer arithmetic, a `[kernel_hook]`-only operation attempted outside one, a type mismatch - each asserting the specific rejection reason, not just "it failed." Also covers: a `[kernel_hook]` function reaching the Wasm backend must produce exactly the "native backend required" diagnostic, every time. This is also where VISION.md's adversarial-AST test lives once the AI-AST ingestion path exists: same corpus, same verifier, AST-shaped inputs instead of source-shaped ones.
- **Wasm execution tests on macOS:** compile a `.manna` program to `.wasm`, run it under `wasmtime`, assert on stdout/return value. Host-side only - unrelated to M3's bare-metal runtime, purely "does the backend emit correct Wasm."
  - **`tools/fetch-wasmtime.sh`:** downloads the official prebuilt `wasmtime-v49.0.2-x86_64-macos.tar.xz` release asset directly from `bytecodealliance/wasmtime`'s GitHub releases (verified this session: Homebrew's formula has zero bottled platforms - it would build from source regardless of OS, and there's no Rust toolchain on this machine either, so that path is avoided entirely). The script verifies the downloaded tarball's checksum against a pinned value before extracting; the extracted `wasmtime` binary is `.gitignore`d, never committed - it's a fetched build tool, not project source.
  - **M3 seed note:** this prebuilt binary unblocks M2's own testing only. M3 needs a Wasm runtime that runs *inside* Deus, which a host CLI binary can't be. WAMR (already named in CLAUDE.md for M3) is the real candidate; a minimal hand-written C interpreter remains a fallback worth prototyping only if WAMR's bare-metal port turns out impractical - not needed to unblock anything in M2.
- **Determinism test:** compile the same input twice (same binary, same flags, two separate invocations) and byte-compare the output at every stage - token stream, AST dump, typed IR, `.wasm` module. Required by MANNA_PRINCIPLES.md principle 12 (deterministic builds) and load-bearing for diverse double-compiling (§1): if the C-bootstrap and self-hosted compilers' outputs are ever going to be meaningfully diffed against each other, each one individually producing byte-identical output across repeated runs is the precondition that comparison depends on.

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

## Working log

- Lexer (`tools/mannac/lexer.c`/`.h`) and its golden tests (`tests/lexer/`, `make test-lexer`) are the first implementation step out of this blueprint. Diffs and test output are reviewed separately from this document, per the usual process - nothing here implies they're already committed.
