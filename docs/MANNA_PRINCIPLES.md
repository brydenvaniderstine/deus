# Manna Design Principles

Manna is a language for humans and machines alike. Humans get readability; AIs get certainty. It's the same code for both audiences.

All Manna language changes must pass the **Feature Admission Test** at the end of this document.

---

## Part 1: Core principles

1. **One way to write it.**
   - There is a single canonical syntax, and the official formatter defines the layout.
   - The same meaning always produces the same text.
2. **Explicit over implicit.**
   - No implicit type conversions, hidden allocation, exceptions, operator overloading, or text macros.
   - What you see is what runs.
3. **Local reasoning.** Code's meaning is visible where it is written. There is no action-at-a-distance (global magic, monkey-patching).
4. **No undefined behaviour.** Every program has exactly one meaning, fails to compile, or traps at runtime.
5. **One-token-lookahead grammar (LL(1)).** The grammar is parseable with a single token of lookahead and has no context-sensitive tricks.
6. **Errors teach.**
   - Every error gives line:col, what was expected, what was found, and a fix hint.
   - Fix hints are machine-applicable edits, e.g. "replace 3:5–3:6 with `==`".
7. **Small core.**
   - The full spec plus the standard library fits in about 20 pages, small enough for an AI context window.
   - Every feature must justify its cost.
8. **Safe by default, unsafe by declaration.** `[kernel_hook]` is the only escape hatch. It is visible, greppable, and audited.
9. **Words over symbols.** Keywords (`fn`, `let`, `while`, `break`) instead of symbol soup. Symbols are used only where they are universal (`+`, `==`).
10. **Predictable cost.** Every construct maps to a known, bounded machine cost, with no hidden copies or allocations.
11. **Canonical machine interchange.**
    - The AST has a stable serialized form.
    - AI agents may emit it directly, and it passes the same verifier as source code.
12. **Deterministic builds.** The same input always produces byte-identical output. This is required for diverse double-compiling.
13. **Future-proof by versioning.**
    - There is a versioned spec with editions, and the stable core never breaks.
    - The IR stays target-neutral: Wasm now, native next, SPIR-V as a candidate.

---

## Part 2: AI-native principles

14. **Version declared in the file.** Every file starts with `manna v0`. No version guessing.
15. **Types at every boundary.** Function signatures are fully typed. Inference is allowed only inside function bodies.
16. **Fast self-check.**
    - `manna check --json` returns structured errors and fix-it edits in milliseconds.
    - It enables a write → check → fix loop before code reaches a human.
17. **Addressable code structure.** Functions and AST nodes have stable IDs, so tools and AIs can make targeted edits ("change the body of `parse_expr`") without rewriting whole files.
18. **Consistent naming.**
    - One naming convention.
    - Predictable standard-library names: `len`, never a mix of `length`, `size`, and `count`.
19. **Tests live next to code.** Examples double as specification and as training material.
20. **Official example corpus.**
    - A few hundred small, correct, idiomatic programs, maintained alongside the spec.
    - This bootstraps AI fluency in a language with no training data.

### Candidate AI-native features (v1+, each must pass the admission test)
- **Contracts:** `requires` / `ensures` clauses that are checked at compile time where possible and trap at runtime otherwise.
- **Declared effects:** functions are marked pure, or as touching I/O, memory, or hardware.
- **Intent tags:** optional `[intent: "..."]` annotations that are searchable and can be compared against behaviour.

---

## Part 3: Lessons learned from other languages

✅ = decided in the v0 spec (not necessarily built - see VISION.md's "Claims we can already demonstrate" for what exists and is tested today) · ▶ = planned · ✗ = permanently excluded

### Memory and safety (C/C++)
| Problem | Manna answer |
|---|---|
| Buffer overflows | ✅ Bounds-checked slices (`arr[a..b]`) |
| Null pointers | ▶ No null; an `Option` type in v1 |
| Use-after-free, dangling pointers | ✅ No heap in v0. ▶ v1 heap uses an ownership or region model simpler than Rust's |
| Undefined behaviour | ✅ None, by design |
| Silent integer overflow | ✅ All overflow traps |
| `if (x = 5)` | ✅ Assignment is a statement, with a "did you mean `==`?" hint |
| Header files, slow builds | ▶ Modules in v1. ✗ Never headers |
| Text macros (`#define`) | ✗ Never |

### Types (JavaScript, Python, PHP)
| Problem | Manna answer |
|---|---|
| Implicit coercion | ✅ No implicit conversions |
| Type errors only at runtime | ✅ Static types |
| `==` vs `===` confusion | ✅ One equality operator |
| Float surprises (`0.1 + 0.2`) | ▶ Float semantics fully specced in v1 |

### Errors and control flow (Java, C++, Go)
| Problem | Manna answer |
|---|---|
| Exceptions (invisible control flow) | ✗ Never. Errors are values |
| Go's `if err != nil` repetition | ▶ A `Result` type plus a short propagation operator |
| Ignored error codes | ▶ Discarding an error is a compile error |
| `goto` | ✗ Never |
| Chained comparisons (`a < b < c`) | ✅ Parse error with a dedicated message |
| C operator-precedence traps | ✅ Bitwise binds tighter than comparison |

### Complexity (C++, Scala, Rust)
| Problem | Manna answer |
|---|---|
| Feature bloat | ✅ Small core plus the admission test |
| Huge template errors | ✅ One-screen errors with hints |
| Steep learning curve | ▶ A simpler memory model than Rust's (arenas/regions) |
| Slow compiles | ✅ LL(1) grammar, no templates |
| Style wars | ▶ One official formatter |

### Concurrency
| Problem | Manna answer |
|---|---|
| Data races, deadlocks | ▶ Message passing over shared locks, matching the Deus IPC design |
| Async "function colouring" | ▶ Decide on a single model before adding concurrency |

### Tooling
| Problem | Manna answer |
|---|---|
| Dependency hell | ▶ One built-in package manager with lockfiles |
| "Works on my machine" | ✅ Deterministic builds |
| Fragmented build systems | ▶ `manna build`, `test`, `fmt`, `doc`, `check` as one tool |
| No editor support | ▶ A language server (LSP) from early on |
| Opaque compiler output | ▶ Inspectable IR and Wasm per function |
| Slow feedback | ▶ A REPL / instant JIT shell (TempleOS heritage) |

---

## Part 4: Language design decisions

### v0 (locked)
1. **The file header is a required statement.**
   - The first non-comment line of every file is `manna v0;`.
   - Grammar: `translation_unit ::= version_decl { top_level_item }` and `version_decl ::= 'manna' IDENT ';'`.
   - A missing or wrong version is a compile error.
2. **Naming is enforced as compile errors, not warnings.**
   - Functions, variables, and fields: `snake_case`.
   - Structs (and future enums): `PascalCase`.
   - Constants (v1): `SCREAMING_SNAKE`.
   - Attributes: `snake_case`, e.g. `[kernel_hook]`.
3. **Source file extension:** `.mn`.
4. **Error JSON schema** for `manna check --json`:
   ```json
   {"schema":1,"file":"a.mn","diagnostics":[{
     "code":"E0102","severity":"error",
     "message":"assignment is a statement; did you mean '=='?",
     "span":{"start":{"line":3,"col":6},"end":{"line":3,"col":7}},
     "expected":"'{'","found":"'='",
     "fixes":[{"title":"use comparison","edits":[
       {"span":{"start":{"line":3,"col":6},"end":{"line":3,"col":7}},"text":"=="}]}]}]}
   ```
   - Error codes (`E0xxx`) are stable and documented in a registry.
   - Every AST node records both its start and end positions.
5. **AST IDs** are name-based, for top-level items only in v0 (`fn:parse_expr`, `struct:Token`). Inner-node IDs are deferred to v1.
6. **Tooling:**
   - One public binary, `manna`, with the subcommands `build`, `check`, `test`, `fmt`, and `doc`.
   - `mannac` is the internal source folder and library name only.

### v1 leanings (decided at v1; no code now)
7. **Memory model:** arenas/regions. A slice can't outlive its arena.
8. **Optionals and errors:**
   - `?T` is an optional value; `!T` is a value or an error.
   - Errors propagate with the keyword `try` (principle 9: words over symbols).
   - Discarding a `!T` is a compile error.
9. **Contracts (`requires` / `ensures`):** proven at compile time where possible, otherwise a runtime trap. They can never be disabled.
10. **Effects:**
    - Functions are pure by default and declare anything else with `uses io, alloc`.
    - `hw` is legal only in `[kernel_hook]` functions.
    - A function's callees can't exceed its declared effects.

---

## Part 5: Known tensions
- **Human readability vs machine convenience:** resolved by one canonical form that serves both. Readability is a security property, because humans must be able to audit.
- **No training data on day one:** AIs will write Manna worse than C at first. This is mitigated by the small spec (principle 7), the example corpus (principle 20), and the fast self-check (principle 16).
- **Safety vs speed:** traps and bounds checks cost a little runtime. Safety wins, and the remaining cost must stay predictable (principle 10).

---

## Feature Admission Test
A feature enters Manna only if **all** of the following hold:
1. It violates none of principles 1–20 and none of the locked decisions in Part 4.
2. It has a spec entry, golden tests, and defined error messages with fix hints.
3. It fits the current version's scope. No v1 features in v0.
4. Its machine cost is documented (principle 10).
5. It does not create a second way to express something that already exists (principle 1).
