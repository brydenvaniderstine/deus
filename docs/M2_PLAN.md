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

**Self-hosting is out of scope for v0 and requires v1.** Writing `mannac` itself in Manna needs capabilities v0 deliberately doesn't have: a heap allocator (for AST nodes and other dynamic structures), strings (for identifiers, source text, diagnostics), and richer structs than v0's fixed layout (tagged unions/variants, realistically, to represent an AST node type at all). Until v1 is designed and specified, self-hosting isn't attempted — v0's C bootstrap compiler is the only `mannac` that exists, by design.

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

## 4. Test strategy

- **Golden tests per stage:** for each stage, a fixed set of `.manna` (or, for the lexer specifically this session, `.mn`) inputs with checked-in expected output - token stream for the lexer, AST dump for the parser, typed-IR dump for the IR builder, annotated IR for loop-counter injection, `.wasm` binary (or its `wasm2wat` text form, for diffability) for the backend. Any stage's output changing unexpectedly fails the test; a deliberate change updates the golden file in the same commit as the code change, never silently.
- **Verifier rejection tests:** a corpus of programs that *must* fail verification - out-of-bounds pointer arithmetic, a `[Kernel_Hook]`-only operation attempted outside one, a type mismatch - each asserting the specific rejection reason, not just "it failed." Also covers: a `[Kernel_Hook]` function reaching the Wasm backend must produce exactly the "native backend required" diagnostic, every time. This is also where VISION.md's adversarial-AST test lives once the AI-AST ingestion path exists: same corpus, same verifier, AST-shaped inputs instead of source-shaped ones.
- **Wasm execution tests on macOS:** compile a `.manna` program to `.wasm`, run it under `wasmtime`, assert on stdout/return value. Host-side only - unrelated to M3's bare-metal runtime, purely "does the backend emit correct Wasm."
  - **`tools/fetch-wasmtime.sh`:** downloads the official prebuilt `wasmtime-v49.0.2-x86_64-macos.tar.xz` release asset directly from `bytecodealliance/wasmtime`'s GitHub releases (verified this session: Homebrew's formula has zero bottled platforms - it would build from source regardless of OS, and there's no Rust toolchain on this machine either, so that path is avoided entirely). The script verifies the downloaded tarball's checksum against a pinned value before extracting; the extracted `wasmtime` binary is `.gitignore`d, never committed - it's a fetched build tool, not project source.
  - **M3 seed note:** this prebuilt binary unblocks M2's own testing only. M3 needs a Wasm runtime that runs *inside* Deus, which a host CLI binary can't be. WAMR (already named in CLAUDE.md for M3) is the real candidate; a minimal hand-written C interpreter remains a fallback worth prototyping only if WAMR's bare-metal port turns out impractical - not needed to unblock anything in M2.

## Working log

- Lexer (`tools/mannac/lexer.c`/`.h`) and its golden tests (`tests/lexer/`, `make test-lexer`) are the first implementation step out of this blueprint. Diffs and test output are reviewed separately from this document, per the usual process - nothing here implies they're already committed.
