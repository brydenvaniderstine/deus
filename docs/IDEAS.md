# Ideas (parking lot)

Dated entries. Nothing here enters a milestone except at a milestone boundary — this file is where a new idea waits, not a side channel for scope creep into whatever's currently in progress.

Format: `- YYYY-MM-DD: idea, one or two sentences, and where it might eventually belong (M2/M3/M4, v1 roadmap, VISION.md candidate, etc.) if that's already obvious.`

## 2026-10-07: Visible, navigable Deus

- M1.5 "screen + keyboard" (candidate, small): use Limine's framebuffer to draw a text console in the QEMU window (bitmap font); PS/2 keyboard driver; echo typed keys. Validation: boot in QEMU, type, and see characters on screen. Candidate slot: after M2.
- M3+ "Deus shell": TempleOS-style prompt that compiles and runs Manna instantly (e.g. `> 2 + 3` prints `5`). Requires the M2 compiler + M3 Wasm runtime.
- Real hardware: boot from USB on a cheap x86_64 UEFI mini PC. Requires a USB keyboard driver. MacBooks are poor targets (custom keyboard/hardware).
- Later: mouse + windowed GUI. Out of scope until the shell exists.

## 2026-10-07: Collaboration readiness (before inviting contributors)

- Branch protection on main + PR workflow; CI must run on PRs (currently push-only).
- CONTRIBUTING.md; CODEOWNERS for paging, verifier, CLAUDE.md.
- Decide licence/IP model (proprietary + CLA vs open source); get legal advice.
- Decide platform policy for contributors (Mac-only vs portable C on any OS).

## 2026-10-07: Indentation-guided "missing `}`" location hints

- When `parse_block`/`parser_parse_file` hit EOF (or any far-away resume point) looking for a `'}'` that was never there, the diagnostic currently just points at where the search gave up, not at the likely real culprit. A formatter (M2+, once one exists) could track the indentation level at each `'{'` and suggest "this `'{'` on line N (at indent level K) looks like the one missing its close" by comparing where indentation drops back below K without a matching `'}'` - a much more actionable hint than "expected '}', found end of file". Needs the formatter's indentation model to exist first; parked until then. Parser slot: a diagnostic enhancement, not a recovery-mechanism change.

## 2026-10-08: Comment trivia for the formatter

- The lexer skips comments, so no AST node ever sees one, and the S-expression dump (M2_PLAN.md "Golden test format for ASTs") can't carry them. `manna fmt` (principle 1: the formatter defines the layout) must never delete a comment, so it needs trivia retained somewhere: either the lexer keeps comments as tokens/side-table entries with spans that the formatter re-attaches to the nearest node, or the formatter works from the token stream rather than the AST. Also decides whether the planned dump reader (M2_PLAN.md, canonical interchange "planned, not built") is lossless. Not parser-stage work and not v0; parked until `manna fmt` is scoped. Related formatter requirement already recorded: M2_PLAN.md §8 #4 item 5 (array-literal canonicalization).

## 2026-10-09: Candidate principle P21 - token-efficient

- **Status: candidate, not adopted.** Proposed text, verbatim:

  > 21. **Token-efficient.** Among designs that satisfy principles 1–20 and the
  > locked decisions, prefer the one that uses fewer tokens per meaning: common
  > English keywords, no boilerplate, one canonical form. Dense, never cryptic.
  > No efficiency claim without numbers from the token benchmark.

- **Tiebreaker only; never overrides principles 1–20.** Deliberate "extra" tokens stay: `manna v0;` (P14), full signatures (P15), explicit conversions (P2), trailing field commas, contracts.
- **Overlap:** P1, P7, P9 already push the same way. The new part is measurement.
- **Promotion:** into `docs/MANNA_PRINCIPLES.md` Part 2 at the first milestone tag after the token benchmark (next entry) has baseline numbers. That commit also changes Feature Admission Test criterion 1 from "principles 1–20" to "1–21" and fixes every other "20 principles" reference. Found by grep on 2026-10-09: `docs/MANNA_PRINCIPLES.md:176` (criterion 1) and `docs/README.md:10` ("the 20 core/AI-native principles"); re-grep at promotion time.

## 2026-10-09: Token benchmark suite

- The same small programs in Manna, C, Rust and Python, reported as token counts and ratios.
- **Scope limit, stated in every result:** only features Manna v0 has (ints, structs, arrays, loops, functions). No strings, heap or I/O.
- **Correctness gates:** every Manna sample parses with zero diagnostics; every C/Rust/Python version compiles/runs and gives the same result.
- **Fairness:** idiomatic, formatter-style code in every language. No golfing. Comments handled the same way for all languages.
- **Determinism:** one offline tokenizer, pinned by name and version (e.g. tiktoken `o200k_base`), so counts are reproducible. Toolchains installed as prebuilt, checksum-verified binaries, not brew (CLAUDE.md lesson).
- **Earliest start:** after the post-r1 parser step (struct construction, arrays, conversions), since the samples need those constructs to parse. No token claims in VISION/README/STATUS until numbers exist.

## 2026-10-09: Type guardrails = contracts, not dependent types

- **Lean:** the guardrails path is lightweight contracts / refinement checks (`requires` / `ensures`), not full dependent types.
- Already a v1 candidate: `docs/MANNA_PRINCIPLES.md` Part 4 item 9 and the contracts item in `docs/M2_PLAN.md` §6. Not restated here.
- **Why not dependent types:** large core (P7), hard checking, hard-to-teach errors (P6).
- v1 only; must pass the Feature Admission Test. **Open tension with P21:** contracts add tokens.

## 2026-10-09: Capability grants (post-M3)

- A task can do a sensitive action only with an explicit, unforgeable, user-granted capability. Builds on effects (`docs/M2_PLAN.md` §6 effects item) and intent tags.
- The user or a written policy grants rights, never an AI. No self-replicating agents.

## 2026-10-17: Capability-scoped storage (post-M3, needs a storage driver)

- No global path tree for user code: a Wasm task sees only the directory/file handles it is granted through host imports (Fuchsia component namespaces / WASI preopens model). Extends the Capability grants entry above; the user or a written policy grants handles, never an AI.
- Layout candidate: immutable, hash-named system store (kernel, Manna stdlib), with hashes checked at boot; separate user-owned data tree; few, readable top-level names (TempleOS simplicity).
- Limits: protects on-disk files only, not the running kernel (see CLAUDE.md known risks). Prerequisites: M3 Wasm runtime and a storage driver (virtio-blk first). Must not add filesystem code to the kernel TCB beyond what the 120k LOC cap allows.

## 2026-10-09: Per-task execution budget (M3)

- Extends M2's loop-counter injection to a runtime budget per Wasm task; on exhaustion the task traps cleanly.
- Cost documented (P10). No model-token budgets: Deus runs no models.

## 2026-10-11: Exhaustive `match` on enums

- **Status: parked (2026-10-13, `m2-parser-v0.1` review).** Reason: none belongs in the IR skeleton. Not promoted; still must pass the Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) at its milestone boundary; nothing enters the build from here directly.
- **Where:** v1, together with enums (`docs/M2_PLAN.md` §6 item 3).
- **Shape:** no catch-all arm. Adding a variant makes every non-exhaustive `match` a compile error, with a fix-it listing the missing variants. `match` works on enums only: an integer `match` is rejected, because it is a second way to write an `if`-chain (P1).
- **Cost (P10):** a tag compare plus a jump.

## 2026-10-11: Required argument labels on multi-parameter calls

- **Status: parked (2026-10-13, `m2-parser-v0.1` review).** Reason: none belongs in the IR skeleton. Not promoted; still must pass the Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) at its milestone boundary; nothing enters the build from here directly.
- **Where:** v1, at an edition boundary (a breaking change to call syntax).
- **Shape:** calls to functions with 2 or more parameters must label every argument, reusing the struct-construction syntax (`name: value`). Today named arguments to a function are an error (the typed-IR rules, `docs/M2_PLAN.md` "Calls and struct construction"; proposed code E0234), so this would reverse that for the 2+-parameter case. It prevents swapped same-type arguments. One form per function (P1).
- **Open tension with P21:** labels add tokens. Decide with token-benchmark data (the "Token benchmark suite" entry above), not by argument.

## 2026-10-11: Wrapping arithmetic builtins

- **Status: parked (2026-10-13, `m2-parser-v0.1` review).** Reason: none belongs in the IR skeleton. Not promoted; still must pass the Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) at its milestone boundary; nothing enters the build from here directly.
- **Where:** v1 stdlib, before any kernel code is written in Manna (hash, checksum and PRNG code needs modular arithmetic).
- **Shape:** `wrapping_add`, `wrapping_sub`, `wrapping_mul`. The operators `+ - *` still trap on overflow (`docs/M2_PLAN.md` §2: no silent wraparound); wrapping is explicit and greppable, never a mode.
- **Cost (P10):** one instruction each.

## 2026-10-11: Formatting as a check

- **Status: parked (2026-10-13, `m2-parser-v0.1` review).** Reason: none belongs in the IR skeleton. Not promoted; still must pass the Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) at its milestone boundary; nothing enters the build from here directly.
- **Where:** after `manna fmt` exists. **Depends on comment trivia** (the 2026-10-08 entry above): a formatter that cannot keep comments cannot be a check.
- **Shape:** non-canonical layout is a diagnostic, and its fix-it is the formatter's own output. Enforces P1 (the same meaning always produces the same text) instead of leaving it to habit.

## 2026-10-11: Offline self-describing toolchain

- **Status: parked (2026-10-13, `m2-parser-v0.1` review).** Reason: none belongs in the IR skeleton. Not promoted; still must pass the Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) at its milestone boundary; nothing enters the build from here directly.
- **Where:** tooling, after the typed-IR stage (it needs the `E02xx` registry to be real).
- **Shape:** `manna explain <code>` prints that code's entry from `docs/error_codes.md`; `manna spec` prints a short spec card. **Every example on the card is compiled in CI**, so the card cannot drift from the compiler. Helps AI and humans with no network (P16 fast self-check; P20 official example corpus).

## 2026-10-11: Change-impact report

- **Status: parked (2026-10-13, `m2-parser-v0.1` review).** Reason: none belongs in the IR skeleton. Not promoted; still must pass the Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) at its milestone boundary; nothing enters the build from here directly.
- **Where:** tooling, after effects and contracts exist (`docs/M2_PLAN.md` §6 items 7-8); before that there is nothing beyond signatures to report.
- **Shape:** `manna check --json --since <rev>` lists the function signatures, effects and contracts that changed since `<rev>`, for reviewing AI-written edits at the level of what the code is allowed to do, not line by line.

## 2026-10-14: Decide the generics story before the v1 stdlib

Title: Decide the generics story before the v1 stdlib
Date: 2026-10-14
Source: lesson (Go shipped without generics in 2009 and added them in 2022 at high cost)
Problem it solves: v0 excludes generics (M2_PLAN §2); nothing says how v1 containers (lists, maps) get written without them.
Helps humans how / helps AI how: one settled answer before code depends on it; no late retrofit that changes how existing code reads.
Options to weigh at v1: (a) none, concrete types only; (b) built-in generic types only (?T, !T, slices, arrays) (arrays and slices exist today; ?T and !T are specified for v1); (c) minimal generic functions, no templates, no traits.
Earliest milestone boundary: v1, before the stdlib.
Admission Test status: not run (watch P1 one way, P7 small core, P10 predictable cost, LL(1) grammar).
Status: candidate

## 2026-10-14: Small, in-tree v1 standard library, scoped to Deus

Title: Small, in-tree v1 standard library, scoped to Deus
Date: 2026-10-14
Source: lesson (languages win through ecosystem fit for their niche)
Problem it solves: nothing yet lists what the v1 stdlib contains, so it can grow by accident.
Helps humans how / helps AI how: a short, fixed list of modules with P18 names (len, never size/count) that an AI can hold in context and a human can audit.
Shape: a written scope list decided together with the generics entry (2026-10-14); ships in the repo, versioned with the spec edition; counts against P7's ~20-page budget; no third-party packages until the package manager exists.
Earliest milestone boundary: v1, after the generics entry (2026-10-14) is decided.
Admission Test status: not run.
Status: candidate

## 2026-10-15: Capabilities instead of ambient authority

Title: Capabilities instead of ambient authority
Date: 2026-10-15
Source: lesson (Microsoft Midori: https://joeduffyblog.com/2015/11/10/objects-as-secure-capabilities/)
Problem it solves: code can reach anything global; effects alone say what kind of thing a function touches, not which thing.
Helps humans how / helps AI how: a reviewer sees every power a program has in main's inputs; an AI agent can only use what it was handed.
Shape: no mutable globals and no global clock; access to I/O, time and hardware is an unforgeable value passed in; an app manifest decides what main receives; declared effects (M2_PLAN §6) are checked against the capabilities held.
Relation: extends the 2026-10-09 "Capability grants (post-M3)" entry; does not replace it.
Open questions for the Admission Test: (a) the revocation model; (b) narrowing (attenuation) and delegation rules; (c) how Wasm imports map to capabilities; (d) initial types to consider: file, device, net, memory, task.
Earliest milestone boundary: v1, decided together with effects.
Admission Test status: not run (watch P21 tokens; supports P3 local reasoning).
Status: candidate

## 2026-10-15: Bugs trap the sandbox; errors are values

Title: Bugs trap the sandbox; errors are values
Date: 2026-10-15
Source: lesson (Midori error model: https://joeduffyblog.com/2016/02/07/the-error-model/)
Problem it solves: nothing yet says what a trap (overflow, out-of-bounds, failed contract) ends: the task, the app, or the system.
Helps humans how / helps AI how: one rule; a bug never needs handling code, an expected failure always does.
Shape: a bug traps and ends only its own Wasm instance, never the kernel or other apps; expected failures are `!T` values (MANNA_PRINCIPLES Part 4 item 8). A trap inside [kernel_hook] code is a kernel panic; say so in the spec.
Earliest milestone boundary: M3 (Wasm runtime), with the spec line at v1.
Admission Test status: not run.
Status: candidate

## 2026-10-15: AI fluency scorecard

Title: AI fluency scorecard
Date: 2026-10-15
Source: lesson (AI agents fail to compile in unfamiliar languages and fall back to Python: https://www.khoury.northeastern.edu/~arjunguha/main/papers/wu-tokenmaxxing.html)
Problem it solves: "AI-friendly" is a claim with no number behind it.
Helps humans how / helps AI how: shows whether the spec card, corpus and fix-its actually help an AI write Manna.
Shape: a fixed set of small tasks; a pinned model name and version; N runs per task; report the first-try `manna check` pass rate, fix-loop rounds to zero diagnostics, and tokens used. A row is added to REFINEMENT.md section 4 at each tag once it exists. Separate from the "Token benchmark suite" entry (that measures code size; this measures AI success). No AI-friendliness claim in VISION/README/STATUS without a run log.
Earliest milestone boundary: after the typed IR stage (first-try pass needs type errors).
Admission Test status: not applicable (tooling, not a language feature).
Status: candidate

## 2026-10-15: Deterministic record and replay of apps

Title: Deterministic record and replay of apps
Date: 2026-10-15
Source: Claude
Problem it solves: a bug or a bad AI action that happened once can't be reproduced exactly.
Helps humans how / helps AI how: any run can be replayed step by step; an AI debugging loop gets an exact reproduction instead of a guess.
Shape: all nondeterminism (clock, input, randomness, IPC messages) reaches a Wasm app only through capabilities ("Capabilities instead of ambient authority" entry), so the runtime logs those inputs per instance and replays them identically. Known gaps to spec: Wasm NaN bit patterns, resource exhaustion, and any future threads. Added 2026-10-18: `memory.grow` outcomes, preemption points (Deus is preemptive, so the runtime must log where each task was preempted), and relaxed SIMD.
Prior art: Determinator (https://dedis.cs.yale.edu/2010/det/papers/osdi10-abs), Hermit (https://developers.facebook.com/blog/post/2022/11/22/hermit-deterministic-linux-testing/), Midori (no global clock).
Cost (P10): log size per captured input; measure before any claim.
Depends on: the capabilities entry.
Updated 2026-10-18: (a) M3 workshop tooling, no Deus code: QEMU record/replay with GDB reverse-stepping. Limits to know first: TCG only (no HVF, so slow on a Mac), limited device support, block I/O goes through blkreplay, and reverse-stepping needs a snapshot, so the boot image needs a writable qcow2 overlay. Verify it with the Limine boot setup before relying on it. Earliest milestone for (a): M3 kickoff. (b) is the runtime logging described under Shape, after the Wasm runtime exists.
Earliest milestone boundary: M3 (Wasm runtime design).
Admission Test status: not run.
Status: candidate

## 2026-10-15: Undoable agent actions

Title: Undoable agent actions
Date: 2026-10-15
Source: Claude
Problem it solves: an AI agent's mistaken write can't be taken back.
Helps humans how / helps AI how: every agent task can be rolled back with one command, so the system never has to trust the AI to be right.
Shape: an agent task's writes land on a copy-on-write snapshot; the user commits or rolls back. External effects (network sends) can't be undone, so they need human approval (extends the VISION agent-service rule that model output can never trigger an action by itself).
Prior art: AgentRewind (https://arxiv.org/pdf/2608.14380), DeltaBox (https://arxiv.org/pdf/2605.22781); both sit on top of Linux and can't undo network calls.
Depends on: storage/filesystem and the VISION "Agent service on Deus" candidate.
Earliest milestone boundary: post-M3, with the agent service.
Admission Test status: not applicable (OS feature, not language).
Status: candidate

## 2026-10-15: Contracts as the human review surface

Title: Contracts as the human review surface
Date: 2026-10-15
Source: Claude
Problem it solves: humans can't review hundreds of AI-written lines line by line.
Helps humans how / helps AI how: a human reviews a few `requires`/`ensures` lines and the effects list; the AI must meet them and the compiler proves them or traps.
Shape: reviewing AI-written code starts from the contracts and effects diff (the "Change-impact report" entry), not the body. Extends the 2026-10-09 "Type guardrails = contracts, not dependent types" entry; adds no new syntax.
Earliest milestone boundary: v1, with contracts.
Admission Test status: not run (inherits the contracts entry's P21 tension).
Status: candidate

## 2026-10-15: Deterministic simulation testing of kernel logic

Title: Deterministic simulation testing of kernel logic
Date: 2026-10-15
Source: Claude
Problem it solves: rare scheduler and IPC bugs show up once in a million runs and can't be reproduced.
Helps humans how / helps AI how: millions of seeded random runs on the Mac; any failure replays exactly from its seed.
Shape: build scheduler/IPC/allocator logic as host-side test code against a fake clock, fake interrupts and a seeded scheduler. It is host tooling like the fuzzers; nothing from it enters the Deus image.
Prior art: TigerBeetle VOPR (https://docs.tigerbeetle.com/about/vopr), FoundationDB.
Earliest milestone boundary: M3 (when IPC exists).
Admission Test status: not applicable (tooling).
Status: candidate

## 2026-10-15: Hardware backstop for Spectre

Title: Hardware backstop for Spectre
Date: 2026-10-15
Source: lesson (Chrome: "assume any active code can read any data in the same address space", https://chromium.googlesource.com/chromium/src/+/master/docs/security/side-channel-threat-model.md; Cloudflare Workers, 2026: a cross-tenant Spectre leak inside one process, fixed partly with memory protection keys, https://thehackernews.com/2026/08/cloudflare-workers-spectre-attack-leaks.html)
Problem it solves: LBS alone can't stop Spectre in the single address space (CLAUDE.md known risk).
Helps humans how / helps AI how: secrets and network-facing apps get a hardware wall as well as the language wall.
Options: (a) protection keys (supervisor keys, PKS, since Deus code runs in ring 0): no CR3 reload, keeps the single address space; CPU support must be checked (not all x86_64 CPUs have PKS). (b) separate page tables for secret-holding or network-facing tasks: breaks the "one CR3 load" hard constraint.
HARD CONSTRAINT AREA: any option is Bryden's decision; nothing here changes CLAUDE.md.
Earliest milestone boundary: the Spectre mitigation plan CLAUDE.md requires before networking.
Admission Test status: not applicable (OS design).
Status: candidate

## 2026-10-18: Small trusted checker, untrusted compiler

Title: Small trusted checker, untrusted compiler
Date: 2026-10-18
Source: Claude
Problem it solves: CLAUDE.md lists the compiler, verifier and Wasm runtime as the TCB (open risk "TCB"), with no size budget; a bug in any of them bypasses LBS.
Helps humans how / helps AI how: a reviewer audits one small checker instead of a compiler; an AI-written program is accepted only on the checker's say-so, however it was produced.
Shape: only the verifier (and the Wasm runtime's validator) is trusted. mannac's output is re-checked before it runs, so a compiler bug cannot bypass LBS. The trusted checker gets its own line budget, separate from the kernel's 120k LOC; long term it is a candidate for formal proof.
Prior art: the Wasm validator, seL4, proof-carrying code.
Alternatives rejected: trust the whole compiler (TCB too large); fully verified compiler, CompCert-style (cost too high for now).
Earliest milestone boundary: before the verifier is designed (M2).
Admission Test status: not applicable (security architecture, not a language feature).
Status: candidate

## 2026-10-19: Supervised restart of trapped tasks

Title: Supervised restart of trapped tasks
Date: 2026-10-19
Source: outside AI (brainstorm reviewed 2026-10-19)
Problem it solves: the "Bugs trap the sandbox; errors are values" entry says a bug traps its task but not what happens next.
Helps humans how / helps AI how: one stated policy for a crashed task, so a service survives its own bugs without hiding them; an AI-written task that traps is restarted or reported by rule, not by hand.
Shape: a supervisor policy per task: restart, give up after N restarts in a time window, or escalate to the parent. State is not carried over; a restarted task starts clean.
Prior art: Erlang/OTP supervision trees.
Open questions: who owns the restart policy; what capabilities a restarted task gets; host-call side effects completed before the trap (kept, rolled back, or logged); how restarts interact with record and replay ("Deterministic record and replay of apps").
Alternatives rejected: panic the system (one bug kills everything); silent auto-restart with no limit (hides crash loops).
Earliest milestone boundary: M3 (Wasm runtime + IPC).
Admission Test status: not applicable (runtime, not language).
Status: candidate

## 2026-10-19: Live component replacement

Title: Live component replacement
Date: 2026-10-19
Source: outside AI (brainstorm reviewed 2026-10-19)
Problem it solves: updating a component means rebooting the system, so every update is downtime.
Helps humans how / helps AI how: a fixed service can be swapped in while the system runs; a new module is accepted only on the trusted checker's say-so, however it was produced.
Shape: swap a Wasm module without rebooting; the new module must pass the trusted checker before it is swapped in.
Prior art: Theseus, Erlang hot code loading.
Open questions: state handoff between the old and new versions; what happens to in-flight IPC; rollback if the new module traps.
Alternatives rejected: reboot-to-update (downtime); swapping unchecked code (bypasses LBS).
Depends on: "Small trusted checker, untrusted compiler".
Earliest milestone boundary: post-M3.
Admission Test status: not applicable (runtime, not language).
Status: candidate

## 2026-10-19: Provenance on every module

Title: Provenance on every module
Date: 2026-10-19
Source: outside AI (brainstorm reviewed 2026-10-19)
Problem it solves: nothing records what built a loaded module, so the loader cannot tell a module from the trusted toolchain from any other.
Helps humans how / helps AI how: every module can be traced to its source and builder; the loader can refuse unknown provenance, which matters most for AI-written modules.
Shape: each loaded module records its source hash, compiler hash and builder; the loader can refuse unknown provenance.
Prior art: reproducible-builds attestations, SLSA.
Pairs with: "Deterministic record and replay of apps".
Open questions: signing keys and who holds them; verify by independent rebuild?; extending it to stored objects once storage exists.
Alternatives rejected: trust by filename or path (spoofable).
Depends on: deterministic builds (principle 12).
Earliest milestone boundary: M3 (Wasm loader).
Admission Test status: not applicable (runtime, not language).
Status: candidate

## 2026-10-09: Considered and rejected ("Omni/AetherOS" brainstorm)

One line of why each, so they aren't re-proposed:

- **Strict/fluid functions:** nondeterministic, unbounded cost (P4, P10, P12).
- **Neural runtime type checks:** nondeterministic; contracts cover this.
- **Kernel vector filesystem:** no storage yet; TCB and the 120k LOC cap.
- **Token hypervisor / neural compute scheduling:** no accelerators; scope guard.
- **Agent embedding bus:** models' embeddings are incompatible; unauditable.
- **"Up to 90%" token saving:** unsourced claim.

Added 2026-10-11:

- **Distinct integer types (newtypes):** a one-field struct already does it (Admission Test criterion 5: a second way to express something that exists).
- **Compile-time evaluation:** large core, hidden cost (P7, P10).
- **Catch-all `match` arm:** defeats exhaustive match; a new variant would silently take the wrong arm.
- **Expression-bodied functions / implicit return:** a second way to return (P1).
- **String interpolation:** hidden allocation (P2); revisit with strings and arenas.

(Add new entries above this line as they come up.)
