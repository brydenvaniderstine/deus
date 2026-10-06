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
- **Safe pointers:** a single pointer kind, bounds-carrying (pointer + length, not a bare address) for anything outside `[Kernel_Hook]`. Raw/bare pointers don't exist in v0 source — they're an IR-level concept the verifier permits only inside verified `[Kernel_Hook]` regions.
- **`[Kernel_Hook]` attribute:** marks a function as allowed raw pointer arithmetic and hardware intrinsics (port I/O, `cpuid`, MSR access — the exact primitives `kernel/arch/x86/*.c` already uses). Everything else in a `[Kernel_Hook]` function is still typed-IR-checked; it's an escape hatch from the safe-pointer rule only, not from the pipeline.
- **Explicitly excluded from v0:** generics, traits/interfaces, closures, exceptions/unwinding, operator overloading, modules/namespaces, dynamic dispatch, heap allocation, strings.
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
│ verifier            │  proves every non-[Kernel_Hook] pointer access
│                   │  is in-bounds given its carried length, or
│                   │  rejects the whole program. [Kernel_Hook]
│                   │  functions get a narrower check: raw ops are
│                   │  allowed, everything else is still verified.
├─────────────────┤
│ Wasm backend         │  typed, verified IR -> .wasm module.
│                   │  Target: the Wasm MVP subset needed for v0 - no
│                   │  SIMD, no threads, no GC proposal features.
│                   │  Rejects any [Kernel_Hook] function outright,
│                   │  with the diagnostic "native backend required" -
│                   │  port I/O, cpuid, and MSR access have no Wasm
│                   │  sandboxed equivalent, so silently miscompiling
│                   │  or stubbing them out is not an option. A
│                   │  separate native backend (kernel code's actual
│                   │  compile target, per CLAUDE.md's "LBS and the
│                   │  language" dual-target split) is what eventually
│                   │  handles these - not scoped here.
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
translation_unit  ::= { top_level_item } ;
top_level_item    ::= struct_decl | function_decl ;

attribute         ::= '[' IDENT ']' ;

struct_decl       ::= 'struct' IDENT '{' { field_decl } '}' ;
field_decl        ::= IDENT ':' type ',' ;

function_decl     ::= { attribute } 'fn' IDENT '(' [ param_list ] ')' [ '->' type ] block ;
param_list        ::= param { ',' param } ;
param             ::= IDENT ':' type ;

type              ::= IDENT
                    | '[' INTEGER ']' type   (* fixed-size array *)
                    | '[' ']' type           (* safe slice *)
                    ;

block             ::= '{' { statement } '}' ;

statement         ::= let_stmt | assign_stmt | if_stmt | while_stmt | for_stmt
                    | return_stmt | break_stmt | continue_stmt | expr_stmt ;

let_stmt          ::= 'let' IDENT [ ':' type ] '=' expression ';' ;

assign_stmt       ::= postfix '=' expression ';' ;
  (* Assignment is a STATEMENT, not an expression - it does not exist
     inside `expression` at all. Consequences, all structural, none
     needing a bespoke error message except the one noted below:
       - `if x = 5 { ... }`: if_stmt's condition is `expression`, which
         can never produce '='. Parsing the condition stops at `x`,
         then '{' is expected and '=' is found instead - this exact
         spot gets a dedicated diagnostic (below), not a generic one.
       - `a = b = c;`: assign_stmt's RHS is `expression` (no assignment
         inside it), so a second '=' after the first RHS is simply
         unconsumable - parse error at that '='.
       - `1 + 2 = x;`: `1 + 2` cannot reduce to `postfix` (postfix has
         no '+' production), so assign_stmt's left side never matches
         it; falls through to expr_stmt, where '=' is unexpected where
         ';' was expected.
     Dispatch is backtrack-free: parse `postfix` first (cheap,
     unambiguous prefix of any expression), then branch on the next
     token - '=' means assign_stmt; anything else means resume normal
     precedence-climbing expression parsing seeded with the
     already-parsed postfix as its base operand, then expect ';' for
     expr_stmt. *)

if_stmt           ::= 'if' expression block [ 'else' ( if_stmt | block ) ] ;
while_stmt        ::= 'while' expression block ;
for_stmt          ::= 'for' ( let_stmt | assign_stmt | expr_stmt ) expression ';' for_step block ;
for_step          ::= postfix '=' expression | expression ;
  (* e.g.: for let i = 0; i < n; i = i + 1 { ... }
     No parens - matches if/while. Init clause is itself a
     let_stmt/assign_stmt/expr_stmt and supplies its own ';'. Middle
     clause is a bare expression with an explicit ';' after it. Step
     clause has no trailing ';' - the block's '{' follows directly. *)
break_stmt        ::= 'break' ';' ;
continue_stmt     ::= 'continue' ';' ;
return_stmt       ::= 'return' [ expression ] ';' ;
expr_stmt         ::= expression ';' ;

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

One shared counter, incremented/checked/decremented at entry and exit of **every** recursive production — not just the obvious ones (`expression`, `statement`, `block`, `type`), but explicitly also `unary` self-recursion (`!!!!!!...x`) and nested array types (`[1][1][1]...T`). Exceeding 256 on any path is the same clean parse error ("maximum nesting depth (256) exceeded"), never a host-stack overflow. Test cases: nested parens, nested blocks, >256 chained unary operators, and >256 nested `[1]` array-type wrappers.

### AST node types

Every node records a source span (`line:col` of its first token) — same precision discipline as the lexer.

```
Type   = TypeIdent(name) | TypeArray(size, elem: Type) | TypeSlice(elem: Type)

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

Mirrors the lexer's convention directly: `tests/parser/*.mn` → `*.ast`, run via `make test-parser` (plain + `-asan` variants, same shared-script pattern as `test-lexer`/`test-lexer-asan`). An S-expression dump, each node tagged with its span, e.g.:

```
(function_decl@2:1 add
  (params
    (param@2:8 a u32)
    (param@2:16 b u32))
  (return_type u32)
  (block@2:31
    (return_stmt@3:5
      (binary@3:12 +
        (ident@3:12 a)
        (ident@3:16 b)))))
```

For inputs with parse errors, the golden file has two sections — errors first, then whatever tree resulted:

```
ERRORS:
4:9 expected ';' after expression, found '}'

TREE:
(function_decl@2:1 add ...)
```

Test categories mirror the lexer suite: a basic valid-program case, one golden per grammar construct, a `[Kernel_Hook]` case, the two dedicated-diagnostic cases above, the four nesting-depth-bound cases, and general error-recovery cases (missing semicolon, unclosed brace, multiple errors in one file to confirm resynchronization continues past the first).

## 5. Test strategy

- **Golden tests per stage:** for each stage, a fixed set of `.manna` (or, for the lexer specifically this session, `.mn`) inputs with checked-in expected output - token stream for the lexer, AST dump for the parser, typed-IR dump for the IR builder, annotated IR for loop-counter injection, `.wasm` binary (or its `wasm2wat` text form, for diffability) for the backend. Any stage's output changing unexpectedly fails the test; a deliberate change updates the golden file in the same commit as the code change, never silently.
- **Verifier rejection tests:** a corpus of programs that *must* fail verification - out-of-bounds pointer arithmetic, a `[Kernel_Hook]`-only operation attempted outside one, a type mismatch - each asserting the specific rejection reason, not just "it failed." Also covers: a `[Kernel_Hook]` function reaching the Wasm backend must produce exactly the "native backend required" diagnostic, every time. This is also where VISION.md's adversarial-AST test lives once the AI-AST ingestion path exists: same corpus, same verifier, AST-shaped inputs instead of source-shaped ones.
- **Wasm execution tests on macOS:** compile a `.manna` program to `.wasm`, run it under `wasmtime`, assert on stdout/return value. Host-side only - unrelated to M3's bare-metal runtime, purely "does the backend emit correct Wasm."
  - **`tools/fetch-wasmtime.sh`:** downloads the official prebuilt `wasmtime-v49.0.2-x86_64-macos.tar.xz` release asset directly from `bytecodealliance/wasmtime`'s GitHub releases (verified this session: Homebrew's formula has zero bottled platforms - it would build from source regardless of OS, and there's no Rust toolchain on this machine either, so that path is avoided entirely). The script verifies the downloaded tarball's checksum against a pinned value before extracting; the extracted `wasmtime` binary is `.gitignore`d, never committed - it's a fetched build tool, not project source.
  - **M3 seed note:** this prebuilt binary unblocks M2's own testing only. M3 needs a Wasm runtime that runs *inside* Deus, which a host CLI binary can't be. WAMR (already named in CLAUDE.md for M3) is the real candidate; a minimal hand-written C interpreter remains a fallback worth prototyping only if WAMR's bare-metal port turns out impractical - not needed to unblock anything in M2.

## 6. v1 roadmap

Documentation only — nothing here is v0, nothing here is implemented or scheduled with code yet. Priority order:

1. **`f32`/`f64`.** User-space (Wasm) only until the kernel has XSAVE — the kernel currently builds with `-mgeneral-regs-only` (CLAUDE.md hard constraint) specifically to avoid saving FPU/SSE state in ISRs, so no floating-point value can safely exist in kernel-side (`[Kernel_Hook]`) code until that changes. Float semantics — NaN handling, rounding mode — are specified in v1, not assumed now.
2. **Strings.** Needed for self-hosting (§2) and for any real diagnostic/identifier handling beyond what v0's fixed-layout approach supports.
3. **Constants and enums.** Not in v0; naming and syntax undecided.
4. **Modules.** Lowest priority of the four — v0's single-translation-unit model is sufficient until the codebase is large enough that namespacing actually hurts without it.

This list grows as later steps record more v1 leanings (memory model, error handling, contracts, effects) — this section is the standing home for all of them, not just these four.

## Working log

- Lexer (`tools/mannac/lexer.c`/`.h`) and its golden tests (`tests/lexer/`, `make test-lexer`) are the first implementation step out of this blueprint. Diffs and test output are reviewed separately from this document, per the usual process - nothing here implies they're already committed.
