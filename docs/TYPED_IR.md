# Typed IR stage (M2 stage 3): plan

Working document for the typed IR builder / type checker - not a permanent owner file (`docs/README.md`'s table doesn't list it). Same role `PARSER_STEP4.md` had for the #4 parser step: a checklist and a record, **not the spec**. Step 1 below promotes the settled rules into `docs/M2_PLAN.md` (the owner of language and pipeline design); this file then links to it instead of duplicating it. Archived to `docs/history/` at close-out.

**Status: STEP 0 - PLAN ONLY. No code written. Reviewed; the section 8 questions are DECIDED 2026-10-11 (recorded there). Awaiting approval to commit.** Written at HEAD `c6a4cf0`; `m2-parser-v0` is on `12f1d3d` (checked: `git rev-parse`). Everything marked **[Qn]** refers to a question in section 8 (all decided; the marker now points at the decision); everything marked **[Fn]** is an ambiguity or contradiction in the existing docs, section 9.

## 1. What the IR is for, and what it is not

**For.** `docs/M2_PLAN.md` §3 gives this stage one job: "AST -> typed IR; resolves names, checks types, rejects anything outside the v0 spec". Concretely it is the one place where:

- every name is resolved (locals, parameters, functions, structs, fields, attributes) and every type is known;
- everything the parser deliberately left open is decided: struct construction vs. a call (§4 "`Call` also represents struct construction"), `Point(x: 1)` field checks, conversion legality (`bool` <-> integer), lvalue validity (R1_TRIAGE #3), the `i64::MIN` literal (§2), unary `-` on unsigned (§2);
- the checks the parser handed over by decision run: naming conventions, unknown attributes, duplicate names (§7 items 7-8);
- the output is a form the next stages can consume **without re-checking**: loop-counter injection (§3), the pointer verifier (E03xx), the Wasm backend (E04xx), and `manna check --json` for E02xx diagnostics.

It is target-neutral (principle 13: "The IR stays target-neutral: Wasm now, native next") and deterministic (principle 12; M2_PLAN §5 already says the determinism test extends to "typed IR and `.wasm` ... once they exist").

**Not for** (each belongs to a later stage or is out of v0):

- bounds proofs, `arr[i]` range reasoning, "provably out-of-bounds" rejection - the verifier (§3, E03xx). A literal `a[9]` on a `[4]u8` is **not** an IR error;
- loop-counter injection (§3) - it runs on this IR, after it;
- inserting runtime overflow/bounds traps - the IR records which operations trap; the backends lower them (§8 #4 item 4 fixes the Wasm cost);
- constant folding or any optimization. The two literal rewrites (typing a literal; folding a directly-negated `2^63` into `i64::MIN`) are *representation*, not optimization;
- machine-cost documentation (§3 Wasm box), `[kernel_hook]` semantics beyond carrying the flag **[Q16, F3]**, SSA/CFG construction;
- any ingestion path for an externally-written AST (planned canonical interchange, M2_PLAN §4 "planned, not built"). When one exists it goes **through the parser's validation and then this stage**, never around either (VISION.md AI-AST rule, §3 box);
- the `manna` CLI. The real driver does not exist; this stage gets a test-only dump harness (`ir_dump`), same role as `parser_dump`.

## 2. Input contract (from the parser)

### 2.1 The gate: IR runs only if the parser reported zero diagnostics **[Q10]**

The tree alone cannot tell the IR whether the parse was clean. Verified on today's binary (not reasoned):

| Input | What the tree looks like |
|---|---|
| `fn f() -> unit { }` (E0109) | A normal `function_decl` with `(return_type)`, byte-identical to `fn f() { }`. |
| `fn g() { let x = 1; let = 2; return x; }` (E0105) | The malformed `let` is **gone**: no placeholder node (Step 10 design). A later use of a name it would have declared would be a bogus "unknown name". |
| `manna v1;` (E0106) / missing header (E0107) | `parser_parse_file` returns false, no items. |
| arena cap (E0110), depth (E0103), error cap (E0104) | No usable tree (`longjmp` to the fatal target). |
| stray `fn` in a body (post-audit F0/G1) | The enclosing function stays, truncated at the stray token. |

So the contract is on `ParserState.diags`, not on tree shape: **`ir_build` takes the parse `DiagList` and refuses to run (returns an explicit "skipped: N parse error(s)" status, adds no diagnostics) if it is non-empty.** The refusal is a quality policy (no cascades from a tree with holes), **not a safety dependency**: the IR must still not crash on any tree the parser can produce, gated or not. The fuzz target (section 6) runs the IR ungated in a build-flag-only mode to prove exactly that.

### 2.2 What a clean parse guarantees (derived from `ast.h` and `parser.c`; each is asserted in the IR builder in debug builds, never turned into a user diagnostic)

- Items are `DECL_STRUCT` / `DECL_FUNCTION` only, in source order (`parser_parse_file` returns `NodeList` of `Decl*`; there is no translation-unit node). `manna v0;` was already checked.
- A function body is a `STMT_BLOCK`; `if`'s then-branch, `while`/`for` bodies are blocks; an `else` is NULL, a `STMT_IF` or a `STMT_BLOCK`. A `for` init is a let/assign/expr statement; its step is an assign or expr statement, never a `let`.
- `EXPR_NAMED_ARG` occurs only as a direct element of a call's `args`; its `value` is never itself a named arg; its `name` is **any** expression (the "bare field name" check is ours). Positional and named args may be mixed.
- An array literal has at least one element. `array_repeat.count` is a `NUMBER`'s `u64` value (0 and enormous values both reach us). `convert.target` is the text of one of the nine primitive keywords (`bool` included). `unit` never appears: it is reserved and unwritable (E0109 / E0105).
- `TYPE_IDENT.name` is either a primitive keyword's text or a user identifier. The two cannot collide: primitives are reserved words, so a struct can't be named `u8`. `TYPE_ARRAY.size` is any `u64`.
- Numbers are unsigned magnitudes only; negatives are `UNOP_NEG` over a number. `Expr.parenthesized` is set (and the span widened to include the parens) on a node that was the immediate operand of `(...)` - the §2 `i64::MIN` mechanism.
- Every node has a start-and-end span. Identifiers are any lexer IDENT: ASCII letters, digits and `_`, may start with `_`, any case (`lexer.c: is_ident_start/cont`). So **no naming convention is guaranteed** and `_`, `__x`, `fooBar` and `X` all arrive.

### 2.3 What it does *not* guarantee (all ours to check)

- Names: nothing is resolved; duplicates, unknown attributes and bad casing pass. The call "callee" is a bare `const char *name` with no span of its own.
- Assignment targets are only "postfix-shaped" - which is **not** "lvalue". Verified: `1 = 2;`, `f(x) = 3;`, `u8(x) = 1;`, `[1, 2][0] = 3;`, `(a + b) = 1;` and `Point(x: 1) = p;` all parse to an `assign_stmt`.
- `break`/`continue` outside a loop, `return` shape vs. the function's type, missing returns, unreachable code, array sizes, struct recursion, literal ranges, conversion legality - all unchecked.
- **Today, tree height is not bounded by the 256-unit depth bound.** See 2.4. **[Q14, decided: option C]** The parser gets a tree-height bound in step 2 (new code E0111); after that lands, this bullet moves to 2.2 as a guarantee and the IR *asserts* the bound instead of enforcing one.
- **Coarse spans.** Only expressions, statements, types, decls and fields/params carry spans. The following do not: a call's callee name, a field-access's field name, a conversion's target keyword, a `let`'s name, a function's/struct's/field's/param's name, each attribute. A decl's span starts at its first attribute. So "unknown function `f`" can only point at the whole call; "unknown attribute" only at the whole declaration **[Q11]**.

### 2.4 Finding: AST height is unbounded by the parser's depth bound (measured)

The depth bound counts recursion *cycles* (M2_PLAN §4 "Universal nesting-depth bound"). Left-associative binary chains (`1+1+1+...`), postfix chains (`a.f.f.f...`, `a[0][0]...`) and `else if` links are built by loops, so a flat 20,000-term expression produces a tree 20,000 levels tall at no depth cost. Run on `main` (`c6a4cf0`), `parser_dump` built from the Makefile's own sources:

| Input (`let x = <expr>;` in one function) | Plain `-O2`, 8 MB stack | ASan+UBSan, 8 MB stack |
|---|---|---|
| `1` + 1,000 x `+1`; `a` + 1,000 x `.f`; `a` + 1,000 x `[0]` | ok | ok |
| 20,000 / 60,000 / 100,000 terms, all three shapes | ok (no crash) | **stack overflow in `expr_print`** at 20,000 (all three shapes) |
| same, ASan binary with a 64 MB stack | - | ok |
| 200,000 terms | E0110 (arena cap), no tree | same |

Conclusions: (1) the **parser** is fine - it builds these trees iteratively (the ASan failure is in the printer's recursion, shown by the ASan trace, whose frames are all `expr_print`, and by the same input passing with a 64 MB stack); (2) the **only** bound on tree height is the 16 MiB arena (somewhere between 100,000 terms, which parsed, and 200,000, which gave E0110), far beyond what a recursive walker survives under ASan; (3) any recursive IR builder, IR dump or later pass inherits this. M2_PLAN's "never a host-stack overflow" sentence is true of the parser and false of anything that walks the tree. (4) Separately, the existing AST dump's output grows quadratically with chain length (indentation per level): the 100,000-term plain run wrote ~20 GB before I killed it and deleted the output. Not a bug for realistic inputs; relevant only for the IR dump's golden sizes. **Decided (Q14, option C, 2026-10-11): the bound goes in the parser**, as a new E01xx code (proposed E0111), merged with the name-span update into one parser update (step 2). That covers `parser_dump`, a future `manna fmt` and the IR with one mechanism, and fixes this known issue at its source (recorded in `docs/STATUS.md`). The IR carries no height bound or error code of its own; it asserts the parser's.

## 3. IR data shape **[Q13]**

**Proposal: a new, separate, structured (tree) typed IR - not annotations on the AST, not SSA/CFG.**

- *Separate from the AST* because (a) §4 says the IR stage "retags a `Call` whose callee resolves to a struct", i.e. mutates the tree, which conflicts with the AST staying the pristine, validated artifact that an external-AST path must re-enter **[F7]**; (b) the resolved form (slots instead of names, struct-construction nodes instead of calls, typed constants) is a different shape, and having the types *in* the nodes lets later stages rely on them without a side table.
- *Tree, structured control flow* because the Wasm backend's target is structured (`block`/`loop`/`br`), loop-counter injection (§3) is "more typed IR" and wants to find `for`/`while` nodes, and the verifier walks it. A native backend can build a CFG from the tree later (principle 13 keeps the IR target-neutral); building one now would be the "second hard problem" (principle 7). `for` stays a `for` (not desugared to `while`) so `continue` keeps meaning "run the step".
- *Allocation*: its own `Arena` with its own hard cap (so IR size is bounded independently of the parser's and gets its own "too large" code, per the `error_codes.md` E0110 note: "any later stage that adds its own cap gets its own code"). Names are pointers into the AST arena's strings; the AST arena outlives the IR. Documented, not copied.

```
IrType   interned & canonical per module (pointer equality == type equality):
         INT{bits 8|16|32|64, signed} | BOOL | UNIT | ARRAY{len:u64, elem} | SLICE{elem} | STRUCT{decl} | ERROR
         UNIT exists internally (a function with no '-> type' returns it) though it is unwritable.
         ERROR is the poison type (section 4.1); it never appears in a successfully built module.
IrStruct { name, fields[{name, type, span}], size, align }        (layout: [Q15, F6])
IrFunc   { name, id "fn:name" (§7 item 5), kernel_hook:bool, params[IrLocal], ret:IrType, locals[IrLocal], body }
IrLocal  { slot (declaration order), name, type, span, is_param }
IrExpr   { kind, type:IrType*, span }, kind one of
           INT{bits, signed repr}   BOOL   LOCAL{slot}
           UNARY{NOT|NEG|BITNOT}    BINARY{op}  (arith ops marked trapping; AND/OR marked short-circuit [Q6])
           CALL{func, args[]}       STRUCT_NEW{struct, values[] in declaration order}
           FIELD{base, index}       INDEX{base, idx}     SLICE{base, lo, hi}
           CONVERT{target, operand}   ARRAY{elems[]}     REPEAT{value, count}
IrStmt   LET{slot, init} | ASSIGN{place, value} | IF | WHILE | FOR{init, cond, step, body}
         | BREAK | CONTINUE | RETURN{value?} | EXPR | BLOCK
```

**Guarantees the IR gives later stages (they assert these, they do not re-check):** every expression has a non-`ERROR` type; every name is a slot / function / struct / field index; no `EXPR_NAMED_ARG`, no call-to-struct; every conversion is an explicit `CONVERT` (the IR never contains an implicit one); every integer constant is in range for its type; an `ASSIGN` place is `LOCAL`/`FIELD`/`INDEX` chains only; call arity and argument types match; `break`/`continue` are inside loops; every non-unit function definitely returns **[Q5]**; every type's size is within the limit **[Q15]**; attribute names are known; naming and uniqueness rules hold.

**Backend requirement (Q8, decided): every local lives until its function returns.** Every local - parameters and `let`s alike, including those declared in inner blocks - has storage that lasts until the function returns. A name's *visibility* ends at its block's `}`; its *storage* does not. The Wasm and native backends must not reuse, pop or overwrite an inner block's slot before the function returns (in Wasm: slots allocated per function frame, not per block). Together with the decided rules that slices exist only as parameters and locals (never returned, never a struct field) and that recursion is rejected, a slice of an inner-block array can never outlive the array - even when it is stored in a local declared in an outer block. This is a requirement **on the backends** (and the verifier), not something the IR stage can check or the dump shows; step 1 records it in M2_PLAN §3's backend box.

**Dump format (golden contract).** Same S-expression convention as the AST dump (`tag@span`, scalar header, children; types as bare inline text); the expression's type is the **last header scalar**; locals print as `name #slot`; no pointer, hash order or time ever appears (determinism). IR nodes keep the AST span they came from. Proposed, to be fixed by the first golden in step 3:

```
(fn@2:1-5:1 fn:add
  (params (param@2:8-2:17 a #0 u32) (param@2:19-2:28 b #1 u32))
  (returns u32)
  (block@2:31-5:1
    (return@3:5-3:14
      (binary@3:12-3:17 + u32
        (local@3:12-3:13 a #0 u32)
        (local@3:16-3:17 b #1 u32)))))
```

A function with no return type prints `(returns)`, matching `(return_type)`. A unit-typed *expression* (a call to such a function) prints the type text `unit`; the IR dump is output-only and is not a source interchange form, so principle 11's "unit is unwritable" rule is not violated by it. `i64::MIN` prints `(int@3:5-3:25 -9223372036854775808 i64)`. A buildable-with-errors module produces **no** dump: an erroring check yields `ERRORS:` only (a poisoned tree is not a contract worth pinning).

## 4. Type rules per v0 production

Cites are to `docs/M2_PLAN.md` unless noted. "Err" is a code from section 5. A rule marked **[Qn]** is a proposal that waits for the answer; nothing below is a decision.

### 4.1 Mechanics that apply everywhere

- **Bidirectional checking.** `check(expr, expected)` and `infer(expr)`. An expected type flows into: integer literals, unary `-` of a literal, parenthesized expressions, array-literal elements, `[v; N]`'s value, and both operands of arithmetic/bitwise operators. It does **not** flow into a conversion's operand (conversion is the explicit act) or from a comparison's `bool` result into its operands; a comparison types its operands against each other. When exactly one operand of a binary operator is an untyped integer literal expression, the other operand's inferred type is its expectation (`0 == x`, `1 + x`, `10 < n`, literal on the left or the right alike); if both are literal expressions and nothing outside supplies a type, it is E0225 (Q1a, no default). Which constructs push an expectation is the riskiest design surface in the stage (it is hand-rolled unification-lite); step 6 is STOP for that reason.
- **Poisoning, so one mistake gives one error** (the parser's own cascade discipline, applied here). A failed expression gets type `ERROR`, which is compatible with everything and is never reported again; a `let` whose initializer or annotation failed still declares its name (type `ERROR`); a call to an unknown function still checks its arguments for their *own* errors but does not compare arity. Goldens pin each "one error, not two" case.
- **Error cap**: 20 per file (`MAX_ERRORS` precedent), own code (E0203).
- **Storage vs. visibility [Q8]**: scoping (below) governs only which *names* are visible; storage for every local lasts until the function returns - a backend requirement, see section 3.
- **Scopes [Q3]**: function scope holds the parameters; each block opens a scope; a `let`'s binding is visible only *after* its own statement (so `let x = x + 1;` refers to an outer `x` or fails); a `for` init `let` is scoped to the loop.
- **Namespaces [Q4]**: structs and functions share one top-level namespace, order-independent (forward references fine). Locals are a separate, inner namespace; a call's callee is always looked up in the top-level one (§4: "no indirect calls, no calling an expression's result").

### 4.2 Items

| Production | Rule | Cite | Err |
|---|---|---|---|
| `struct_decl` / `field_decl` | Name PascalCase; unique among all top-level names; field names snake_case and unique within the struct; each field type resolves; a struct may not contain itself by value, directly or transitively; zero fields is legal (grammar allows `{ }`). Slice-typed fields: **[Q8]**. | §4 `struct_decl`, §7 items 2/7/8 | E0210 E0211 E0220 E0221 E0280 E0281 |
| `function_decl` | Name snake_case, unique top-level; each attribute known (only `kernel_hook` in v0) and not repeated; params snake_case and unique; param and return types resolve; absent `-> type` = `unit` (§2 "stated explicitly, never inferred"). `[kernel_hook]` only sets the flag **[Q16, F3]**. | §4 `function_decl`, §2, §7 items 2/7/8 | E0210 E0212 E0214 E0215 E0220 E0280 |
| `type` | `PRIM_TYPE` -> the integer / `bool` type. `IDENT` -> a struct, else E0220 (a function's name used as a type is also E0220). `[N]T` -> array; `N` is any `u64` including 0 (`[0]T` is a legal zero-length array, §4 `[v; 0]` note) but the type's total size must be within the limit **[Q15]**. `[]T` -> slice. | §4 `type`, §2 | E0220 E0222 |

### 4.3 Statements

| Production | Rule | Cite | Err |
|---|---|---|---|
| `let` | `let x: T = e;` -> `check(e, T)`. `let x = e;` -> `x : infer(e)`; an integer literal with no context follows **[Q1]**; `unit` may not be bound (**D1**). Name snake_case. `let` always has an initializer (grammar), so no uninitialized state exists. Every local and parameter is mutable (v0 has no `const`/`mut`, constants are v1 §6). | §4 `let_stmt`, principle 15 | E0223 E0213 E0226 E0280 |
| assignment (`postfix_stmt`) | Target must be an lvalue: `IDENT`, or `lvalue.field`, or `lvalue[idx]` (base array or slice). Anything else (a call, conversion, array literal, number, slice expression, parenthesized non-lvalue) is E0270. Value is `check(v, type(target))`; types must be equal. Array/struct assignment copies **[Q8]**. | §4 comment on lvalue, R1_TRIAGE #3 | E0270 E0223 |
| `if` / `while` | Condition `check(c, bool)`; no truthiness (principle 2). `else` branch is a block or an `if`. | §4 | E0223 |
| `for` | init scoped to the loop; condition `bool`; step is assignment or expression; body block. | §4 | E0223 |
| `break` / `continue` | Must be lexically inside a `while`/`for` body (the parser accepts them anywhere). | §4 | E0275 |
| `return` | `return e;` -> `check(e, ret)` and `ret != unit`; `return;` iff `ret == unit`. | §2, §4 | E0276 E0223 |
| function end | A non-unit function must definitely return **[Q5]**. | principle 4 | E0277 |
| `expr_stmt` | Any well-typed expression; the value is discarded (no warnings exist, `diag.h`) (**D2**). | §4 | - |
| `block` | New scope. Unreachable code after `return`/`break` is allowed (**D3**, **[Q5]**). | §4 | - |

### 4.4 Expressions (precedence chain, `unary`, `postfix`, `primary`)

| Production | Rule | Cite | Err |
|---|---|---|---|
| `NUMBER` | Typed by the expected integer type, which it must fit (E0224); with no expectation: E0225 (Q1a: no default). The lexer already rejected `> 2^64-1`. | §2 signed-integer item | E0224 E0225 |
| `-` literal | `UNOP_NEG` directly over an unparenthesized `NUMBER` of magnitude exactly `2^(N-1)` in an `iN` context is `iN::MIN` - the test is `operand.kind == EXPR_NUMBER && !operand.parenthesized && value == 2^(N-1)`. `-(2^63)` (parenthesized) is a plain range error. The spec states this **for `i64` only** **[Q2, F2]**. | §2 and the paragraph after it | E0224 |
| `-` other | Operand must be a signed integer; unsigned is a type error (named in §2). Overflow (`-iN::MIN`) traps at runtime. | §2 | E0260 |
| `!` / `~` | `!` on `bool`; `~` on any integer. | principle 2 (derived, not stated) | E0260 |
| `* / % + -` | Both operands the **same** integer type; result the same type. No implicit conversion (principle 2). Overflow and `/`,`%` by zero trap **[Q6, F9]**. Mismatch offers a fix-it wrapping the operand in the conversion when one is legal (**D4**). | §2 | E0261 E0260 |
| `& \| ^` | Same integer type -> same type. `bool` operands: **[Q6]**. | §4 | E0261 E0260 |
| `<< >>` | Count type and out-of-range count semantics: **[Q6, F9]**. | §4 `shift` | E0260 |
| `< > <= >=` | Same integer type both sides -> `bool`. The parser guarantees at most one comparison/equality per `&&`/`\|\|` operand (E0101/E0108), so no chain reaches us. | §4 `relational` | E0261 E0260 |
| `== !=` | Same type, integer or `bool` -> `bool`. Struct/array equality is **not** in v0 (hidden O(n) cost, principle 10) **[Q6]**. | §4 | E0261 E0260 |
| `&& \|\|` | `bool` both -> `bool`. Short-circuit and evaluation order are unspecified **[Q6, F8]**. | §4 | E0260 |
| `( e )` | Transparent; keeps `parenthesized` for the `i64::MIN` test. | §2 | - |
| `IDENT` | Resolves to a local/parameter. A function or struct name used as a value is E0231 (no function values, §4). | §4 `primary` | E0230 E0231 |
| `IDENT ( args )`, function | Callee in the top-level namespace; arity must match; each arg `check(a, param_type)`; positional only (named args to a function: E0234, **[F12]** not stated anywhere). Result type is the return type (may be `unit`; **D1**). Recursion **[Q4]**. | §4 `primary`, "ONLY call form" | E0232 E0233 E0234 E0223 E0235 |
| `IDENT ( name: v, ... )`, struct | Retagged to `STRUCT_NEW`. Every arg must be `name: value` (E0240, which also covers positional arguments mixed in with named ones), the left side a bare unparenthesized `EXPR_IDENT` (E0241 - `(x): 1` is not bare), a declared field (E0242, the same code field access uses), none repeated (E0244), none missing (E0243), **in declaration order** (E0245, stated twice: §4 arg comment and the §4 to-do list), each value `check(v, field_type)`. A constructor with positional-only args is E0240 (the admission record's "a constructor names every field", §8 #4 item 1). | §4 arg comment, "Diagnostics added by r1 Phase B", §8 #4 | E0240-E0245 E0223 |
| `.field` | Base is a struct; field exists; type is the field's. (No `.len`: **[Q9]**.) | §4 `postfix` | E0242 E0251 |
| `[i]` | Base array or slice; index an integer type **[Q7]**; result the element type. Constant out-of-range is the **verifier's**, not ours (§3). | §4 `index_or_slice`, §3 | E0252 E0253 |
| `[lo..hi]` | Base array or slice; both bounds integer; result `[]T`. Both bounds are mandatory in the grammar (`a[..3]` doesn't exist). | §4 | E0252 E0253 |
| `PRIM_TYPE ( e )` | Target and operand both **integer** types (`bool` either side is E0265 - "write `x != 0`"; there is no spelling for `u8(b)`); same-type conversion is legal and a no-op (**D5**); the operand is typed on its own, not from the target **[Q1]**. Runtime trap when the value doesn't fit (§4 conversion comment). | §4, §8 #4 | E0265 |
| `[a, b, c]` | At least one element; all elements one type (the first, or the expected element type, decides); result `[len]T`. | §4 `array_body` | E0223 |
| `[v; N]` | Result `[N]T`; `v` is evaluated once, even for `N == 0`; total size within the limit **[Q15]**. | §4 `array_body` | E0223 E0222 |
| array / slice values | `[N]T` and `[]T` are different types; passing an array where a slice is expected is a mismatch (no implicit conversion) **[Q8, Q9]**. | principle 2 | E0223 |

## 5. New error codes (proposed numbers, `E02xx` = typed IR per `docs/error_codes.md`)

**Rule: a code is registered in `docs/error_codes.md` in the same commit that first emits it, and not before** ("codes are never reused or renumbered", so no speculative reservation, and "no diagnostic ships unregistered" keeps holding). The numbers below are the proposal; the step column says when each lands. 39 codes (E0202, E0246 and E0250 are deliberately unassigned: E0202 was the IR-side height bound, dropped by Q14; E0246 and E0250 merged into E0240 and E0242. Gaps stay, numbers are never reused). One more code lives in the **parser** range: proposed **E0111** `expression nested too deeply (limit N levels)`, registered in `docs/error_codes.md` with step 2, no fix-it (documented exception: the input must be restructured). Messages follow "errors teach" (principle 6): what was expected, what was found, and a fix hint where a mechanical one exists.

| Code | Message template | Step | Fix-it |
|---|---|---|---|
| `E0201` | `input too large: the type checker's memory limit was exceeded` | 3 | none (documented exception, like E0110) |
| `E0203` | `too many errors, stopping` | 3 | none |
| `E0210` | `'X' is already declared at L:C` (structs and functions, one namespace) | 4 | none |
| `E0211` | `duplicate field 'x' in struct 'S'` | 4 | none |
| `E0212` | `duplicate parameter 'x'` | 5 | none |
| `E0213` | `'x' is already declared in this function` **[Q3]** | 7 | none |
| `E0214` | `unknown attribute 'x'; the only v0 attribute is 'kernel_hook'` (supersedes the case check) | 5 | none |
| `E0215` | `duplicate attribute 'x'` | 5 | delete it (one edit) |
| `E0220` | `unknown type 'X'` | 4 | none |
| `E0221` | `struct 'S' contains itself` | 4 | none |
| `E0222` | `type is too large (size exceeds the limit)` **[Q15]** | 4 | none |
| `E0223` | `type mismatch: expected T, found U` (the workhorse, like E0105) | 6 | none |
| `E0224` | `integer literal N does not fit in T` | 6 | none |
| `E0225` | `cannot infer the type of this integer literal; annotate it` **[Q1, only if option (a)]** | 6 | none |
| `E0226` | `a 'unit' value can't be used here` | 7 | none |
| `E0230` | `unknown name 'x'` | 7 | none |
| `E0231` | `'f' is a function/struct, not a value` | 7 | none |
| `E0232` | `unknown function 'f'` | 10 | none |
| `E0233` | `'f' takes N arguments, found M` | 10 | none |
| `E0234` | `'f' is a function; named arguments are for struct construction` | 10 | none |
| `E0235` | `recursion is not allowed in v0` **[Q4]** | 10 | none |
| `E0240` | `struct construction names every field: use 'field: value'` (also fires when named and positional arguments are mixed) | 11 | none |
| `E0241` | `left of ':' must be a field name` | 11 | none |
| `E0242` | `struct 'S' has no field 'x'` (in a construction and in a field access) | 11 | none |
| `E0243` | `missing field 'x' in construction of 'S'` | 11 | none |
| `E0244` | `field 'x' given twice` | 11 | none |
| `E0245` | `fields must be given in declaration order: 'x' before 'y'` | 11 | none (a reorder needs source text) |
| `E0251` | `type T has no fields` | 11 | none |
| `E0252` | `can't index/slice type T` | 12 | none |
| `E0253` | `index/bound must be an integer, found T` | 12 | none |
| `E0260` | `operator 'op' is not defined for T` (covers `-` on unsigned, `!` on an integer, `+` on `bool`, ...) | 8 | none |
| `E0261` | `operands have different types: T and U; convert one explicitly` | 8 | wrap the operand in the legal conversion: two zero-width inserts, the E0108 mechanism (**D4**, proposed) |
| `E0265` | `can't convert T to U; conversions are between integer types only` | 13 | none (`x != 0` needs source text) |
| `E0270` | `can't assign to this expression` | 7 | none |
| `E0275` | `'break'/'continue' outside a loop` | 8 | none |
| `E0276` | `missing/unexpected return value` | 9 | none |
| `E0277` | `function 'f' may end without returning a value` **[Q5]** | 9 | none |
| `E0280` | `'x' is not snake_case` | 14 | rename (declaration + every resolved use) **[Q12, F5]** |
| `E0281` | `'X' is not PascalCase` | 14 | same |

Naming predicates (§7 item 2 gives only the names of the conventions, not their definition **[F13]**; the lexer admits leading `_`, digits and any case): *proposed* `snake_case = [a-z][a-z0-9]*('_'[a-z0-9]+)*` (no leading, trailing or doubled `_`; `_` alone and `_x` rejected - v0 has no wildcard); `PascalCase = [A-Z][A-Za-z0-9]*` (no `_`; acronym runs like `HTTPServer` allowed). The fix-it's case converter gets a table test (`fooBar`->`foo_bar`, `HTTPServer`->`http_server`, `X`->`x`, `__x` -> no fix when the result would be empty or collide).

## 6. Test strategy

**Invariant for every step:** the 365 parser goldens and 15 lexer goldens stay **byte-identical** (a golden that loses diagnostics or nodes is STOP-tier with every removed line listed, per CLAUDE.md "Lessons"). Counts re-checked against `STATUS.md`'s snapshot: `tests/parser/*.mn` = 365, `tests/lexer/*.mn` = 15.

- **Goldens** `tests/ir/*.mn` -> `*.ir`, `make test-ir` / `test-ir-asan`, harness `tools/mannac/ir_main.c` -> `ir_dump` (whole-file mode only; expression/block modes need a symbol context). Output is either the IR dump or `ERRORS:`. **Error lines carry the code** (`3:5 E0223 type mismatch: ...`) - unlike the parser goldens, which print message text only - because 39 codes with generic-looking messages are too easy to mix up silently. One golden per rule row in section 4 (a positive and a negative), at least one per code, plus a no-cascade golden for each poisoning path.
- **Per-code coverage by script**, not by memory (the method the `m2-parser-v0` tag used for productions): every `E02xx` in `error_codes.md` appears in at least one `.ir` golden, and every code string in the source is registered. The script's output goes in the step report.
- **Unit tests** `ir_test.c` (plain + ASan, wired into CI): type interning/equality; the snake/Pascal predicates and case converter on a table; literal-range edges at every width (`u8` 255/256, `i8` -128/-129/127/128); the `i64::MIN` matrix (`-9223372036854775808` ok; `-(9223372036854775808)`, `-9223372036854775809`, `9223372036854775808` in `i64`, and `-9223372036854775808` in `u64` all rejected; `-0x8000000000000000` per **[Q2]**) and its generalization to other widths if approved; fix-it edit positions pinned like `parser_fixes_test.c`; the gate refusing a non-empty `DiagList`, including the E0109 file; reuse of an `IrState` after a fatal longjmp (the parser's R6 lesson: all per-run state resets).
- **Determinism**: extend `make test-determinism` / `tools/run_determinism_test.sh` to the IR goldens (twice plain, once ASan, byte-compared) - what M2_PLAN §5 already promises. Output must never depend on a pointer, hash iteration order or the arena base. Required for diverse double-compiling (§1).
- **Height bound (the 2.4 finding; parser side, step 2 [Q14])**: measured, not predicted (Lessons: proof over reasoning). For each of binary, postfix-field, postfix-index and mixed chains: bisect the largest accepted height and the smallest rejected under plain **and** ASan at the default 8 MB stack, with `expr_print` run on the maximal accepted tree (that run is the proof the known issue is fixed); goldens at N-ok / N+1-fail / 300-fail; zero crashes. N is chosen from the measurement with margin, then approved. On the IR side: a debug assertion that no input tree exceeds the parser's bound, exercised by an `ir_test` that builds the IR for a maximal-height tree.
- **Corpus sweep**: every whole-file input in `tests/parser/` through `ir_dump` gated (expect: either clean IR or E02xx, never a crash) and ASan'd. Not goldens (those files were written for parsing, not typing), just a crash and determinism net.
- **Fuzz**: new libFuzzer target `fuzz_ir` (lexer -> parser -> IR), seeded from `tests/parser` and `tests/ir`, ASan+UBSan, run for a fixed budget like the other two (not in CI). It has an **ungated build mode** (compile flag only, `MANNA_FUZZ_IR_UNGATED`) that runs the IR on whatever tree the parser produced even when it reported errors: that is the empirical check of "the gate is policy, not a safety dependency". Gated alone, mutated inputs rarely survive the parser. Every crash becomes a golden.
- **CI**: every new `make test-*` target goes into `.github/workflows/ci.yml` in the same commit, and the run is watched green. **Note:** CI has `paths-ignore: docs/**, **/*.md`, so a docs-only commit (steps 0, 1, and the docs in 16) triggers **no** run; do not claim "CI green" for those.
- **Coverage at the tag**: list uncovered lines for the new `ir*.c` files with justifications, as CLAUDE.md requires for `lexer.c`/`parser.c` (**D6**).

## 7. Steps (one commit each; STOP marked)

ROUTINE steps follow CLAUDE.md's rule: tests pass, no design choice, no spec deviation; at most 3 accumulate before review; nothing is pushed until the batch is approved. Any step that turns out to need a decision becomes STOP on the spot. Every report carries the plain-English summary, risk level and which tests prove it.

| # | Step | Tier | Tests / proof |
|---|---|---|---|
| 0 | This plan, committed | **STOP** (plan approval) | docs only; no CI run |
| 1 | Promote the settled rules (sections 3, 4, 5 as decided) into `docs/M2_PLAN.md` as the owner; add §8 Admission Test records for each new language-level rule (literal typing, scoping, no recursion if approved, operator semantics, array/slice rules), marked "pending admission (goldens due)"; resolve or restate each **[F]** flag; add the new IR section to `docs/README.md`'s consistency check; record the Q8 backend requirement in M2_PLAN §3's backend box; **add D6 to CLAUDE.md** (the at-every-tag uncovered-lines rule covers `ir*.c` too); **raise `len` (Q9) as its own Admission Test item**, which must be decided before step 12 | **STOP** (spec additions and any deviation) | docs only; quote-check each claim against the repo (Lessons) |
| 2 | **One small parser update**: (a) name spans (callee, field-access name, conversion target, `let`/decl/field/param names, each attribute) **[Q11]**; (b) the tree-height bound **[Q14, option C]** - new E0111, N measured under ASan at 8 MB with margin; registers E0111 in `error_codes.md`, flips the STATUS known issue to fixed; **new parser tag after it** (name proposed: `m2-parser-v0.1`) | **STOP** (touches the tagged parser, `ast.h`, a new diagnostic) | `ast_test`/`parser_fixes_test` pin each span; height goldens N-ok / N+1-fail / 300-fail per chain shape, plain + ASan, and `expr_print` on the maximal tree passes at 8 MB; all 365 + 15 existing goldens byte-identical (a golden taller than N would be a STOP finding); determinism; parser fuzz 10 min with the last-resort trap |
| 3 | IR skeleton: `ir.h`/`ir.c` (types, interning, `IrState` with arena cap and error cap, `ERROR` type; a debug assertion of the parser's height bound), `ir_build` with the parse-diagnostics gate, `ir_dump` harness, `make test-ir`/`-asan`, CI wiring, determinism wiring; E0201, E0203 | **STOP** (data shape and dump format first appear) | `ir_test`: interning, gate refuses E0109 file, state reuse; first goldens (empty module, one empty function) |
| 4 | Top-level declarations: symbol table, struct/function names, E0210/E0211, type resolution, E0220-E0222, struct-recursion check, size/limit **[Q15]** | ROUTINE | goldens per rule + negative; recursive-struct matrix (direct, mutual, via array) |
| 5 | Function signatures: params, return type, attributes (E0212, E0214, E0215), `kernel_hook` flag | ROUTINE | goldens; `[Kernel_hook]` shows E0214 only (no double report) |
| 6 | Literals and expected-type checking: integer typing, range (E0223-E0225), unary `-`, the `i64::MIN` rule at its approved widths, `ERROR` poisoning semantics | **STOP** (literal typing and the MIN rule are design; hard to reverse) | the `i64::MIN` matrix from section 6 (all four signed widths); **literal-on-the-left goldens: `0 == x`, `1 + x`, `10 < n`** (the other operand supplies the type), their right-hand mirrors, an out-of-range one (`256 + x` with `x: u8` -> E0224) and a both-literal one (`0 == 1` -> E0225); no-cascade goldens |
| 7 | Locals and basic statements: `let`, scopes, identifiers, assignment + lvalue rule, expression statements, blocks, the `unit` rule (E0213, E0226, E0230, E0231, E0270) | ROUTINE | the six lvalue-shape inputs from 2.3 as goldens; shadowing matrix **[Q3]** |
| 8 | Operators and control flow: unary/binary/logical/shift/compare typing, conditions, `if`/`while`/`for`, `break`/`continue` (E0260, E0261, E0275), the E0261 fix-it | ROUTINE (STOP if **[Q6]** is answered with anything unusual) | per-operator type matrix; fix-it pinned in `ir_test` |
| 9 | `return` shape and the missing-return analysis (E0276, E0277) | **STOP** (design choice **[Q5]**) | every control-flow shape: if/else both return, one branch, loops, nested |
| 10 | Calls: function resolution, arity, argument types, named-arg rejection, recursion check (E0232-E0235) | ROUTINE | direct and mutual recursion goldens **[Q4]** |
| 11 | Struct construction + field access (E0240-E0245, E0242 for access, E0251), `STRUCT_NEW` retag | ROUTINE | one golden per code; parenthesized `(x): 1`; out-of-order; mixed (E0240); unknown field in a construction and in an access (both E0242) |
| 12 | **Blocked until the `len` Admission Test item (step 1) is decided.** Arrays: literal, `[v; N]` (incl. `N == 0`), indexing, slicing (E0252, E0253), size limit | ROUTINE | `[0]T`, nested arrays, array-vs-slice mismatch |
| 13 | Conversions (E0265): int<->int legal, `bool` either side rejected, same-type no-op | ROUTINE | `bool(x)`, `u8(b)`, every int pair |
| 14 | Naming conventions + fix-its: predicates, case converter, use tracking, `DiagFix` edit-list growth **[Q12, F5]** | **STOP** (changes `diag.h`'s edit capacity, cross-cutting rename) | converter table; rename fix-it applied to a file produces a file with zero E02xx |
| 15 | Proof step: no-cascade audit across all poisoning paths, per-code coverage script | **STOP** (every claim stated as fact) | script output; the parser height bound was already proven in step 2 |
| 16 | Close-out: `fuzz_ir` (gated + ungated) run and corpus seeded, determinism and coverage re-run with uncovered lines justified, Admission Test records -> admitted with goldens cited, STATUS entry, archive this file, tag **`m2-ir-v0`** (name proposed) | **STOP** (tag) | fuzz execs and zero crashes stated from the run; CI run watched green on the final code commit |

The `expr_print` stack overflow found while writing this plan (2.4) is a known issue of `m2-parser-v0` in `docs/STATUS.md` (separate docs commit, shown first) and is fixed by step 2's height bound.

## 8. Open questions for Bryden

Ordered by how much of the plan hangs on them. **All sixteen questions were decided by the reviewer on 2026-10-11; each paragraph keeps the original options and recommendation and ends with the decision.** Where a decision differs from the recommendation (Q9, Q14) or adds to it (Q8), the decision text says so.

**Q1. How is an integer literal typed when nothing around it says?** The spec never says **[F1]** and its own examples presuppose an answer: §4 says `u8(300)` and `u32(-1)` trap at runtime, so `300` and `-1` must have types with no context at all, yet §2 says `-` on an unsigned type is an error. Options: (a) **no default**: a literal needs an expected type (annotation, parameter, field, other operand) or it is E0225 - so `let i = 0;` is an error and `u8(300)` is an error; (b) a fixed default (`i64`, say) used wherever there is no context - then `let i = 0;` works, `i` is `i64`, and mixing it with a `u32` is a mismatch error; (c) (a) plus literal suffixes (`0u32`) - a lexer/grammar change, so a full Admission Test. *Recommendation: (a), with (c) held in reserve* - a default is exactly the kind of invisible choice principle 2 exists to prevent, and (a) keeps the checker to bidirectional checking with no unification. Cost: the §4 conversion examples must be rewritten (`let big: i32 = 300; u8(big)`), and `for let i: u32 = 0; ...` needs the annotation. Your call, because it changes how every program reads. **DECIDED 2026-10-11: (a) no default. A literal needs an expected type or it is E0225; suffixes (c) are not adopted. The §4 conversion examples (`u8(300)`, `u32(-1)`) get rewritten in step 1.**

**Q2. Does the `i64::MIN` rule generalize?** §2 specifies it for `i64` only. `let x: i8 = -128;` has the identical problem (`128` doesn't fit `i8`). *Recommendation: generalize - directly-negated, unparenthesized literal of magnitude exactly `2^(N-1)` in an `iN` context is `iN::MIN`, for N in 8/16/32/64*; the alternative makes `-128` unwritable as an `i8`. **DECIDED 2026-10-11: generalize to all signed widths (8/16/32/64).**

**Q3. Scoping and shadowing.** Nothing in the docs. *Recommendation: block scopes; parameters live in the function scope; redeclaring a name already visible anywhere in the function - same block or an outer one - is E0213 (no shadowing at all).* Principle 3 (local reasoning): a name means one thing within a function. Shadowing is the permissive alternative. **DECIDED 2026-10-11: no shadowing: redeclaring a name visible anywhere in the function is E0213.**

**Q4. Top-level order and recursion.** Order-independent declarations (forward references legal) - assumed, nothing says otherwise. Recursion: *Recommendation: reject direct and mutual recursion in v0 (call-graph cycle check, E0235).* My reasoning, not in the docs: loop-counter injection (§3) exists so every loop writes to the telemetry ring; recursion is a loop with no counter, and unbounded stack use is the opposite of principle 10 (predictable cost). If you'd rather allow it, the verifier/backends need a stack-depth story first. It's a restriction, so it passes the Admission Test easily. **DECIDED 2026-10-11: reject direct and mutual recursion (E0235); forward references stay legal.**

**Q5. Missing-return and unreachable code.** Principle 4 forbids a non-unit function falling off its end. *Recommendation: structural "definitely returns" - the body ends in `return`, or an `if`/`else` where both branches definitely return; loops never count (so `while true { return 1; }` still needs a trailing `return`); unreachable code after `return`/`break`/`continue` is allowed* (v0 has no warnings, `diag.h`, and an error for dead code would be a new rule nobody asked for). **DECIDED 2026-10-11: as recommended (structural definitely-returns, loops never count, unreachable code allowed).**

**Q6. Operator semantics the spec never states [F8, F9].** Bundled because the answers are one-liners and the Wasm backend will need all of them: (i) shift count: *same integer type as the left operand*, and a count >= the bit width **traps** (it is not "arithmetic overflow" as §2 words it); (ii) `&`, `|`, `^` on `bool`: *rejected* (`&&`/`||` are the boolean operators; one way to write it); (iii) `==`/`!=`: *integers and `bool` only*; (iv) `&&`/`||`: *short-circuit*; (v) operand and argument evaluation order: *strictly left to right* (principle 4 wants exactly one meaning); (vi) `/` and `%` by zero: *trap*; `iN::MIN / -1` is overflow, already covered. Confirm or amend. **DECIDED 2026-10-11: all of (i)-(vi) as recommended.**

**Q7. Index and slice-bound types.** There is no `usize`, and no implicit conversion. *Recommendation: any integer type; a negative or too-large value is a runtime/verifier out-of-bounds trap, not a type error.* The alternative (`u64` only) forces a `u64(i)` on every `u32` loop counter. **DECIDED 2026-10-11: any integer type.**

**Q8. Arrays by value, and slice lifetimes [F10].** (i) Arrays as values: `let b = a;`, passing, returning and assigning copy `N` elements - a visible copy at the use site, with a known cost (§8 #4 item 4 style), so arguably within principle 10; *recommendation: allow, document the O(N) cost.* (ii) **Slices have no lifetime story in v0**: §6 item 5 says "a slice can't outlive its arena", but v0 has no arenas, and a slice of a local array returned from a function dangles. *Recommendation: `[]T` is allowed as a parameter type and as a local's type only - not as a return type, not as a struct field - until v1 regions exist.* Without something like this the "no undefined behaviour" claim has a hole. **DECIDED 2026-10-11: arrays copy by value, with the O(N) cost documented; `[]T` only as a parameter or local type. **Added requirement:** every local lives until its function returns, including locals declared in inner blocks, so a slice of an inner-block array cannot dangle - recorded in section 3 as a backend requirement.**

**Q9. Reading a length [F11].** Nothing in v0 can produce `len(x)` or `x.len` (principle 18 names `len` as a stdlib convention, but v0 has no stdlib or builtins). A `[]T` parameter therefore can't be iterated to its end, and an array can't be turned into a slice except by writing its length literally (`a[0..4]`). That is a language addition and needs its own Admission Test; *this plan does not include it.* Do you want it raised as a separate item before the IR step 12? **DECIDED 2026-10-11: yes. `len` is raised as its own Admission Test item (step 1) and must be decided before step 12. It is not part of this plan's scope.**

**Q10. The parse-error gate (2.1).** *Recommendation: IR never runs on a file with any parse diagnostic.* Alternative: run it on the partial tree and report IR errors too - more diagnostics per run, but every dropped statement can produce a bogus "unknown name" later. Also confirm that any E02xx stops the pipeline before loop-counter injection. **DECIDED 2026-10-11: as recommended: the IR never runs on a file with any parse diagnostic; any E02xx stops the pipeline before loop-counter injection.**

**Q11. Name spans in the AST (step 2).** Today diagnostics could only point at a whole call or a whole declaration for: unknown function, unknown attribute, naming violations on declarations, unknown field in an access. Principle 6 asks for line:col of the thing. *Recommendation: add the spans in a small tested parser step first (no golden output changes, because the dump doesn't print them)* - but it touches the tagged parser, hence STOP, and probably a new tag. Alternative: ship the IR with coarse spans and add them later, re-baselining nothing but every IR golden's columns. **DECIDED 2026-10-11: yes, add the spans - merged with the height bound into one parser update (step 2).**

**Q12. Naming fix-its are multi-edit [F5, F13].** §7 item 7 requires a correctly-cased fix-it on every naming violation, "the same `DiagFix` mechanism E0102 uses". `DiagFix` holds at most 2 edits (`MAX_FIX_EDITS`, built for E0108), and a rename is one edit at the declaration plus one per use. Options: (a) grow `DiagFix` to an arena-allocated edit list and have the symbol table record every resolved use (my recommendation; it makes the fix actually machine-applicable, and the test is "apply the fix, re-check, zero errors"); (b) fix-it edits the declaration only (applying it breaks every use - not a fix); (c) no fix-it, which contradicts §7 item 7's text. Also: your approval of the proposed predicates in section 5. **DECIDED 2026-10-11: option (a): `DiagFix` grows to an arena-allocated edit list and the symbol table records every resolved use. The predicates in section 5 are approved with it.**

**Q13. IR shape (section 3).** Separate structured tree IR with its own arena; `for` kept as `for`. Alternatives: annotate the AST in place (smaller, but mutates the validated AST) or a CFG/SSA (premature). **DECIDED 2026-10-11: as proposed.**

**Q14. The tree-height bound (2.4, F14).** Options: (A) an IR-stage bound on tree height with its own error (E0202), the number chosen by measurement under ASan at 8 MB - simple and uniform, but it caps legitimate long chains (a 600-term `a | b | ...` table expression); (B) walk left spines iteratively in the IR builder, dump and later passes - no limit for chains, more code in every pass; (C) bound it in the **parser** instead (a change to the tagged v0 grammar's behaviour, new diagnostic in E01xx). *Recommendation: (A), with the number set in step 3 provisionally (candidate 1024) and fixed by the step 15 measurement.* (B) is the right answer if you expect generated code with very long chains. **DECIDED 2026-10-11: **option (C), not the recommendation (A).** The bound goes in the parser as a new E01xx code (proposed E0111), merged with step 2 into one small parser update (STOP); N is measured under ASan at 8 MB with margin; a new parser tag follows. E0202 and any IR-side bound are dropped; the IR asserts the parser's bound.**

**Q15. Struct layout, sizes and the maximum array size [F6].** §2 says structs have "fixed layout" and §4/§8 list "maximum array size" with no value and no layout rule. The max-size check needs `size_of`, which needs layout; the verifier and Wasm backend need it too. *Recommendation: the IR stage defines it - fields in declaration order, natural alignment per field, trailing padding to the struct's alignment, no reordering; no single type's size may exceed 2^31 - 1 bytes* (Wasm MVP memory32 addresses; signed-offset friendly) - E0222. Confirm the layout and the limit. **DECIDED 2026-10-11: as proposed (declaration order, natural alignment, trailing padding, no reordering; no type larger than 2^31 - 1 bytes; E0222).**

**Q16. `[kernel_hook]` in v0 [F3].** §2 describes raw pointer arithmetic and hardware intrinsics inside `[kernel_hook]`, but the v0 grammar and AST contain no pointer type, no dereference and no intrinsic syntax, so nothing a `[kernel_hook]` function can do differs from any other function. *Recommendation: the IR treats it as a flag it carries and validates the spelling of (E0214 for any other attribute), and imposes no call restriction* (whether a non-hook function may call a hook function is a backend/verifier question, today unanswerable). Confirm that is the intended v0 reading. **DECIDED 2026-10-11: as proposed: `[kernel_hook]` is a carried flag with its spelling validated and no call restriction.**

**Defaults D1-D8: all accepted, DECIDED 2026-10-11.**
- **D1.** A `unit` value can only be an expression statement or `return`-less call; binding it (`let x = f();`), passing it, or operating on it is E0226.
- **D2.** An expression statement may discard any value; no "unused result" rule (no warnings exist in v0).
- **D3.** Unreachable code is allowed (see Q5).
- **D4.** E0261 (`u32 + u64`) offers the wrap-in-conversion fix-it when an integer conversion between the two exists, as two zero-width inserts - the E0108 mechanism, no source text needed.
- **D5.** A same-type conversion (`u32(x)` with `x: u32`) is legal and a no-op, kept in the IR.
- **D6.** The "list uncovered lines at every tag" rule is applied to the new `ir*.c` files too; CLAUDE.md only names `lexer.c`/`parser.c`; **decided: step 1 adds D6 to CLAUDE.md.**
- **D7.** IR goldens print error codes (section 6); parser goldens are untouched.
- **D8.** Zero-field structs and zero-parameter functions are legal (the grammar allows both); `Empty()` constructs.

## 9. Spec ambiguities and contradictions found (flagged, not resolved)

Each cites where; none was silently settled in the sections above - the ones I had to take a position on to write section 4 are the **[Qn]** items.

- **F1. Literal typing is never specified.** No rule for the type of an integer literal anywhere (§2, §4, MANNA_PRINCIPLES). §2's `i64::MIN` paragraph speaks of "an `i64` context" and §4 gives `u8(300)` and `u32(-1)` as trapping examples, both presupposing a typing rule that isn't there. -> Q1.
- **F2. The negative-minimum rule covers `i64` only** (§2, last item). `i8`/`i16`/`i32` have the same problem and are unmentioned. Also the "exact test" in §2 is written with pseudo-fields (`expr.operand.kind`); the real AST is `expr.as.unary.operand` - harmless, but a literal transcription won't compile. -> Q2.
- **F3. `[kernel_hook]` describes things v0 cannot express.** §2: "raw pointer arithmetic and hardware intrinsics (port I/O, `cpuid`, MSR access)" and "Raw/bare pointers ... an IR-level concept the verifier permits only inside verified `[kernel_hook]` regions"; the §4 grammar and AST have no pointer type, dereference or intrinsic. §5's "a `[kernel_hook]`-only operation attempted outside one" has no v0 referent. -> Q16.
- **F4. §5 files "a type mismatch" under *verifier* rejection tests**, but type checking is this stage (E02xx); the verifier is E03xx (`error_codes.md`). Probably a wording slip; the type-mismatch tests belong to the IR goldens here.
- **F5. §7 item 7 vs. `DiagFix`.** The naming fix-it is required "via the same `DiagFix` mechanism E0102 already uses" (one edit); `diag.h` caps a fix at `MAX_FIX_EDITS = 2` and says "E0108 is the only two-edit fix", while a rename needs one edit per use. -> Q12.
- **F6. "Fixed layout" structs and the "maximum array size"** (§2; §4 to-do list; §8 #4 item 4: "an IR-stage limit, not specified here") have no layout algorithm and no limit value. -> Q15.
- **F7. "The IR stage retags a `Call`"** (§4, after the AST node types) implies mutating the AST in place, against §3's picture of the AST as the validated artifact an external AST re-enters through. -> Q13 (this plan builds new nodes).
- **F8. Evaluation order and short-circuiting are unstated** (`&&`/`||`, operands, call arguments) although principle 4 requires exactly one meaning. -> Q6.
- **F9. "All arithmetic overflow ... traps" (§2) doesn't cover division/remainder by zero or out-of-range shift counts**, and it's ambiguous whether bits shifted out of `x << 1` count as "overflow". -> Q6.
- **F10. No scoping, shadowing, forward-reference or recursion rules; no slice-lifetime rule in v0** (§6 item 5 refers to arenas v0 doesn't have). -> Q3, Q4, Q8.
- **F11. No way to read a length**, although principle 18 names `len`; no builtin or intrinsic mechanism exists. -> Q9.
- **F12. Named arguments to a *function* call are not addressed.** §4 says "no call mixes named and positional args" and lists struct-construction checks, but never says `f(a: 1)` for a function is an error; likewise it never says a positional struct construction is an error (the admission record's "a constructor names every field" implies it). This plan makes both errors (E0234, E0240) as the consistent reading.
- **F13. Naming-convention predicates are undefined** (§7 item 2: names only) while the lexer admits `_`-led, digit-bearing, mixed-case identifiers. Section 5 proposes definitions. -> Q12.
- **F14. M2_PLAN §4's nesting-depth section says "never a host-stack overflow"** - true of the parser, false of any walker over its output (2.4). Nothing in the repo contradicts it, but it will mislead whoever writes the next pass. -> Q14.

## 10. Proof obligations (nothing here may be stated as fact until built and run)

- The parser height bound (Q14, step 2): bisected under plain and ASan at 8 MB, per chain shape, with `expr_print` run on the maximal accepted tree; zero crashes. Until then "N levels" is a candidate, nothing more, and the STATUS known issue stays open.
- The gate claim (2.1): the ungated fuzz mode runs a fixed budget with zero crashes - the policy/safety distinction is *tested*, not argued.
- "One mistake, one error": a golden for every poisoning path.
- Determinism over every IR golden, plain x2 and ASan x1, byte-compared.
- Per-code coverage by script; uncovered lines of `ir*.c` listed with justifications.
- Parser (365) and lexer (15) goldens byte-identical at every step; `make test-parser`, `-asan`, `test-determinism` re-run whenever step 2 or any shared file (`diag.h`, `ast.h`) changes.
- Docs consistency check (`docs/README.md`) before the tag; quote-check every number in the STATUS entry against the repo, not against this file.
