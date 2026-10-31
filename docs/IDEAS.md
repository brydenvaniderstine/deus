# Ideas (parking lot)

Dated entries. Nothing here enters a milestone except at a milestone boundary — this file is where a new idea waits, not a side channel for scope creep into whatever's currently in progress.

Format: `- YYYY-MM-DD: idea, one or two sentences, and where it might eventually belong (M2/M3/M4, v1 roadmap, VISION.md candidate, etc.) if that's already obvious.`

## 2026-10-07: Visible, navigable Deus

- M1.5 "screen + keyboard" (candidate, small): use Limine's framebuffer to draw a text console in the QEMU window (bitmap font); PS/2 keyboard driver; echo typed keys. Validation: boot in QEMU, type, and see characters on screen. Candidate slot: after M2.
- M3+ "Deus shell": TempleOS-style prompt that compiles and runs Manna instantly (e.g. `> 2 + 3` prints `5`). Requires the M2 compiler + M3 Wasm runtime.
  - Self-describing commands (from Omarchy review 2026-10-26): each shell command declares its summary, args and examples in its own source header. The shell builds help and listings from those headers only, with no separate registry, and a check mode reports two commands claiming the same name. Precondition: the shell exists.
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
Status: promoted 2026-10-31 (the threat model only; see the dated line below)

**Update 2026-10-30 (28 Oct architecture audit, ranked item 2; foundation plan, Phase 1): a Spectre boundary for mutually distrusting tenants.** The audit's verdict is that speculative attacks "defeat all language-enforced confidentiality" in one address space (V8 team; Chrome moved to process isolation), so language isolation alone cannot keep a secret from another sandbox. The proposal is a threat model that **names which tenants may share the address space** and requires a second boundary for tenants that distrust each other, options (a) and (b) above being the candidates for that boundary (the audit also lists the compiler-emitted fences and coarsened timers CLAUDE.md names, as unvalidated). It also moves the trigger: from "before networking" (register A5's soft trigger) to "before M3 runs untrusted code", and adds the test "a Spectre-v1 Wasm test cannot read another sandbox's secret". Guard-region memory (below) removes the bounds-check gadget on linear memory but not the indirect-call-table and `br_table` gadgets. Still **HARD CONSTRAINT AREA**: the threat model is a document; choosing option (b), or any change to the one-CR3 rule, is Bryden's decision. Nothing in `CLAUDE.md` or the register changes by this update.

**Promoted 2026-10-31 (Bryden, `docs/REFINEMENT.md` "Review 2026-10-30"): the threat model only.** Decided: write the threat model (which tenants may share the address space; a second boundary for tenants that distrust each other), and move register A5's trigger from "before networking" to "before M3 runs untrusted code". Not decided: the choice between (a) protection keys and (b) separate page tables, which stays open for the M3 design. The one-CR3 rule is unchanged and `CLAUDE.md` is not edited.

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
Status: promoted 2026-10-31 (a decision for the verifier stage; see the dated line below)

**Update 2026-10-30 (28 Oct architecture audit, ranked items 3 and 4; foundation plan, Phase 1): which component is the trusted base, and the resident compiler as an unprivileged Wasm guest.** The audit's correction: for user apps the trusted base is the **Wasm runtime** (its validator, code generator and host calls; WAMR is C), not mannac; mannac is trusted only for `[kernel_hook]` native code. The order in which to shrink it: (1) mannac is freestanding C, so compile it to wasm32 and run the resident compiler as a **guest** under the app runtime, unprivileged (a TempleOS-style resident compiler must be always available, not privileged; a parser bug should crash a sandbox, not the kernel); (2) trust checkers, not producers: a Wasm code generator whose output is checked or proven (vWasm and rWasm, Bosamiya et al., USENIX Security 2022); (3) the verifier checks the emitted native code of `[kernel_hook]` code, or a typed form of it, and stays small and specified; (4) treat WAMR as trusted C: fuzz it like mannac and differentially test its interpreter against its AOT output (register A3). Test the plan proposes: a deliberately crashing mannac build leaves the kernel and other guests running. The audit advises **against** rewriting mannac in Manna as a security measure (self-hosting does not shrink the trusted base). This repo's own evidence for "trust the checker": four external audit rounds of the typed IR, and the fix self-check (`ir_fix_selfcheck`) is already a checker that does not trust the producer of a fix. Nothing in `CLAUDE.md` or the register changes by this update.

**Promoted 2026-10-31 (Bryden, `docs/REFINEMENT.md` "Review 2026-10-30"): a decision for the verifier stage; closes register A2.** Decided: the trusted checker is a separate, small component with its own line budget of **5,000 lines of C, excluding tests and generated tables, revisited when the verifier-stage plan is written**. Not decided: how the checker is specified or proved, and which components it checks. Nothing is built now, and there is no Wasm-guest build of the compiler (that waits for M3).

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
Crash record for diagnosis (Omarchy review 2026-10-26): on a trap the kernel writes a fixed record (task, module, trap kind, time). A future agent service may read it and explain the cause, but may not restart, patch or change anything. Read-only by rule.
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

## 2026-10-25: Model-chosen resource policy (AI policy agent)

```
Title:                  Model-chosen resource policy: a model sets the kernel's policy dials; the kernel keeps every mechanism and enforces the bounds
Date:                   2026-10-25
Source:                 Bryden
Problem it solves:      Fixed scheduler, memory and power policies are tuned for an average load; a model reading live telemetry can tune them to the actual load.
Helps humans how / helps AI how:
                        Humans: the machine adapts without hand-tuning, and every change carries a written reason, so behaviour is explainable after the fact.
                        AI: telemetry and policy are a structured, bounded interface (read counters, propose settings, get an accept/reject with the reason), so an agent can operate the machine safely.
Alternatives rejected:  A model in the kernel's fast path: inference takes ms to s, a timer tick is 1 ms; it would make scheduling slow and non-deterministic.
                        A model inside the trusted core (ring 0 TCB): a model cannot be verified, and CLAUDE.md's TCB rule says one bug compromises everything.
                        A model that writes kernel code or patches live: same TCB objection; out of scope.
Earliest milestone boundary:
                        Decision at M3 start (it resolves register entry R4: IDEAS "Deus runs no models" vs VISION's model behind an agent service). Build after M3, with the agent service.
Admission Test status:  not applicable (not a language change)
Status:                 candidate
```

**Shape (the hard lines).**
- **Mechanism stays in the kernel.** Scheduling, allocation, I/O and power control run as today: fixed, verified code, every tick. The model never runs on those paths.
- **Policy is a small set of named dials** (examples: task priorities, CPU shares, memory limits per sandbox, power and thermal limits), each with hard bounds the kernel defines and enforces.
- **The loop is slow:** seconds to minutes. The model reads the telemetry rings (the region CLAUDE.md reserves at 0x4000000), proposes settings, and the kernel validates each one.
- **An invalid or out-of-bounds proposal is rejected loudly and logged.** It is never clamped or applied silently (CLAUDE.md "No degradation": no silent fallbacks).
- **Every applied change is logged with the model's stated reason** (observe, reason, choose, explain), so any behaviour can be traced to a decision.
- **The machine runs correctly with no model at all.** The kernel's own default policy is complete. The model only tunes inside the bounds, so losing it changes performance, never correctness or safety.
- **Where the model runs is open:** off-box over the network (simplest, nothing new in the TCB) or a local model as an isolated task (needs hardware and its own sandboxing story). This is the R4 decision.

**Open questions for review.**
1. Which dials, and which bounds for each? Each needs a test that an out-of-bounds value is rejected.
2. Who may run the agent? The policy interface is a capability, connected to the IDEAS entry "Capabilities instead of ambient authority" (2026-10-15).
3. The telemetry format: it becomes an interface others depend on, so it needs a version from day one.
4. How is a policy change undone? (Connected to "Undoable agent actions", 2026-10-15.)
5. The Spectre angle: timing telemetry exposed to the agent must not become a side channel (CLAUDE.md "Open risks").

## 2026-10-25: Single-service mode for cloud guests

```
Title:                  Single-service mode: Deus boots as a cloud VM running one service, keeping its language-based isolation inside the VM
Date:                   2026-10-25
Source:                 Bryden
Problem it solves:      In the cloud, a general-purpose OS per VM wastes memory, boot time and attack surface on one service; unikernels fix that but drop all isolation inside the VM.
Helps humans how / helps AI how:
                        Humans: one OS and one app format for laptop, edge and cloud; a service deploys as a small, fast-booting VM without being rebuilt for it.
                        AI: one target to learn and reason about; the same telemetry, policy and fix-it interfaces in every mode.
Alternatives rejected:  A separate cloud-only build of Deus (a second OS to maintain; the point is one OS, both modes).
                        A pure unikernel (one fully trusted app, rebuilt per app): drops LBS inside the VM, the reason Deus exists.
                        Running Deus apps on Linux micro-VMs instead: keeps Linux in the stack, which Deus replaces.
Earliest milestone boundary:
                        Decide at M3 (cloud work: virtio drivers, the Wasm runtime); build after the M3 runtime and virtio exist.
Admission Test status:  not applicable (not a language change)
Status:                 candidate
```

**Prior art (from the reviewer's knowledge, to be checked at review):** MirageOS (OCaml, about 2013), IncludeOS (C++), OSv, Unikraft, Hermit (Rust), Nanos; Microsoft Hyperlight runs Wasm functions in micro-VMs. Single-service VMs are not new. Their known weaknesses: hard to debug and monitor, existing apps often need changes, and no isolation inside the VM (one bug owns it).

**What Deus would add (the reasons to do it):**
1. **One OS, both modes.** The same Wasm app runs unchanged among many apps on a laptop or edge box, or alone as a cloud VM. The mode is a boot configuration, not a different kernel.
2. **Isolation inside the VM.** LBS and the Wasm sandboxes stay on in single-service mode, so a bug in one library or plugin of the service cannot take over the VM.
3. **Observable by design.** The telemetry rings (and, if adopted, the model-chosen resource policy, IDEAS 2026-10-25) cover the debugging and monitoring weakness of unikernels.

**Constraints (hard lines, from CLAUDE.md):**
- Same hard constraints in both modes: x86_64, paging on, one CR3, LAPIC preemption, the calibration cascade.
- Nothing is removed for single-service mode (no "fast mode" without the verifier or sandboxes): CLAUDE.md "No degradation".
- Under a hypervisor, nested paging adds two-dimensional page walks (CLAUDE.md Scope, cloud note); this needs measuring, not assuming.

**Open questions for review:**
1. What "single-service" means exactly: one Wasm app plus the kernel, or a small fixed set (a service and its sidecars)?
2. The device set for the first cloud target: virtio-net, virtio-blk, a serial console. Which hypervisors first (QEMU/KVM, Firecracker)?
3. Image and boot: Limine on UEFI as today, or a direct-boot path some micro-VM monitors need?
4. How a service is configured and updated without a shell (connects to "Provenance on every module" and "Live component replacement", IDEAS 2026-10-19).
   - Prior art: Omarchy runs updates through one locked command with timestamp-ordered, idempotent one-time migrations that leave completion markers.
5. Benchmarks that would show it is better, not just smaller: boot time, memory, request latency, against a Linux micro-VM running the same Wasm app.

## 2026-10-30: Captured from the 28 Oct architecture audit and foundation plan (refinement loop at m2-ir-v0)

Source documents: the Deus & Manna architecture audit and the Deus foundation plan, both dated 28 Oct 2026 (PDFs; Bryden has them), and the reviews of them. Every entry below is a **candidate**: none is a commitment, none changes `CLAUDE.md`, and the Admission Test has not been run on any of them except where the review in `docs/REFINEMENT.md` ("Review 2026-10-30") says so. The audit's own repository facts (line numbers in `paging.c`, `gdt.c`, `idt.c`) were measured at its head `604213f`, not at this tree; each is to be re-read before work starts. Two existing entries were updated instead of duplicated: "Hardware backstop for Spectre" and "Small trusted checker, untrusted compiler" (2026-10-30 update blocks inside them).

```
Title:                  Kernel write protection: read-only aliases, read-only tables, page 0 unmapped
Date:                   2026-10-30
Source:                 outside AI (28 Oct architecture audit, ranked item 1; foundation plan, Phase 1)
Problem it solves:      One kernel write bug becomes code execution: the identity map and the HHDM keep the frames of .text, .rodata and the page tables writable, the page tables sit in writable .bss, the IDT is writable, and virtual page 0 is mapped read-write (register K3 covers only the first of these).
Helps humans how / helps AI how:
                        Humans: a stray write in the runtime or a [kernel_hook] faults at the write instead of corrupting code. AI: each protection has one faulting test case, so a change that loses it fails a test.
Alternatives rejected:  Leave it to W^X on the higher-half alias alone (the other aliases stay writable). Protection keys (PKS) as the first step (a write path is only needed once M3 changes mappings at run time; PKS patches were unmerged proposals in the audit's sources).
Earliest milestone boundary:
                        Before any untrusted code runs (register K3, trigger-id `untrusted-code`).
Admission Test status:  not applicable (kernel design, not a language feature)
Status:                 candidate
```

Shape (the audit's, to be re-verified against the tree): split only what needs splitting (the 1 GiB entry holding the kernel image into 2 MiB entries, the blocks holding the image and page 0 into 4 KiB entries); mark `.text`, `.rodata` and the page-table frames read-only and NX in every alias; move the page tables, GDT and IDT to a page-aligned section the higher-half map already makes read-only; unmap page 0. All of it happens before the single CR3 load, so no flush is added. Two details the review added: after `lgdt` and `ltr` the GDT is read-only, so every descriptor's accessed bit must be preset (a segment load writes it), and `ltr` writes the TSS descriptor's busy bit, so it runs before the table is locked. Limits to state honestly: ring-0 code can still clear CR0.WP or reload CR3, so this stops a data write bug becoming a code change and is not a boundary against code that already runs; function pointers and return addresses in writable memory remain (supervisor CET is a later layer).
**HARD CONSTRAINT AREA:** `CLAUDE.md` says "1 GiB pages for the identity map and the HHDM alias" and lists the writable-`.text` risk as open; splitting those pages changes both statements. Bryden's explicit OK is needed before any edit there.
Test the audit proposes: `make test-wx` gains four faulting cases (write `.text` through the identity map, through the HHDM, write a page-table entry, write through a null pointer).

```
Title:                  Guard-region Wasm memory: one 8 GiB slot per memory32 instance, recycled, never unmapped
Date:                   2026-10-30
Source:                 outside AI (28 Oct architecture audit, ranked item 5; foundation plan, Phase 1 "M3 memory layout")
Problem it solves:      The 5-20% bounds-check budget (CLAUDE.md, register A4) and the speculative bounds-check gadget on linear memory.
Helps humans how / helps AI how:
                        Humans: an out-of-bounds load is a page fault that traps the instance, with no bounds-check instruction in generated code. AI: the rule "a fault past the committed size is a trap" is one testable sentence.
Alternatives rejected:  Explicit bounds checks only (the current budget). Pointer-masking features (PAC, BTI, LAM, UAI): they do not check bounds. Wasm memory64 (it brings explicit checks back; stay on memory32).
Earliest milestone boundary:
                        The M3 memory layout; the decision belongs before the verifier stage because it decides whether the verifier proves linear-memory bounds at all (register A4).
Admission Test status:  not run
Status:                 promoted 2026-10-31 (the written decision only; see the dated line below)
```

Shape (the audit's sketch, not compiled or booted): 4 GiB addressable plus a 4 GiB guard per instance, about 16,000 slots in the lower canonical half; making a non-present page present needs no TLB invalidation, so `memory.grow` stays flush-free; guard entries stay all-zero so a speculative walk carries no frame number. **The condition the review added:** a recycled slot may have been bigger than its next tenant. Recycling a slot must make every page beyond the NEW instance's size non-present again, and removing a present page is a flush. So teardown is a flush point; it is batched per epoch (one shootdown for every slot recycled in the epoch) and is never on the fast path. Zeroing the pages alone is not enough. Indirect-call tables and `br_table` still need hardening, and the check that no slot overlaps the HHDM's PML4 entry runs at boot.
**Promoted 2026-10-31 (Bryden, `docs/REFINEMENT.md` "Review 2026-10-30"): the written decision only**, including the flush-on-slot-recycle condition above. Nothing is built. `CLAUDE.md` is NOT edited.

```
Title:                  Restate "zero TLB flushes" as "no flush on the fast path", with the allowed flush points listed
Date:                   2026-10-30
Source:                 outside AI (28 Oct architecture audit, ranked item 8; foundation plan, Phase 1 "TLB rule")
Problem it solves:      The hard constraint "Zero TLB flushes" cannot survive the roadmap: JIT code publication (M4), sandbox slot teardown, SMP shootdowns and IOMMU revocation all remove or downgrade mappings.
Helps humans how / helps AI how:
                        Humans: the rule says what is forbidden and what is allowed, so a flush is a named, reviewed event. AI: every flush call site is named in a test or a comment a check reads, so a new flush fails a test.
Alternatives rejected:  Keep the absolute rule and design around it (it forces never unmapping, which the guard-region slot design cannot always honour). Drop the rule (loses the property the hard constraint exists for).
Earliest milestone boundary:
                        Before M3 is designed.
Admission Test status:  not applicable (OS design)
Status:                 promoted 2026-10-31 (the written decision only, as part of the guard-region decision; see the dated line below)
```

Allowed flush points the plan lists: sandbox teardown epochs, IOMMU revocation (device-side IOTLB, not the CPU fast path), JIT code publication. Unmaps stay off the fast path: never unmap code, recycle sandbox slots, batch any unavoidable unmap into one shootdown per epoch.
**HARD CONSTRAINT AREA:** this rewords a hard constraint in `CLAUDE.md` ("Paging stays on", "Zero TLB flushes"). Changing it needs Bryden's explicit OK, separately from any review decision on this entry.
**Promoted 2026-10-31 (Bryden, `docs/REFINEMENT.md` "Review 2026-10-30", decision 3): the written decision only.** Not decided: the rewording of the hard constraint in `CLAUDE.md`, which needs Bryden's separate OK and is deferred to Phase 1. Nothing is built.

```
Title:                  Kernel stack guard pages and a page-fault path with its own stack
Date:                   2026-10-30
Source:                 outside AI (review of the 28 Oct architecture audit)
Problem it solves:      Once pages can be non-present on purpose (guard regions, unmapped page 0, read-only tables), a stack overflow or a fault taken on a bad stack must not escalate to a double fault with no information. Today only #DF has its own IST stack.
Helps humans how / helps AI how:
                        Humans: a kernel stack overflow is a clean, logged fault. AI: a test can overflow a stack on purpose and read the log line.
Alternatives rejected:  Larger stacks only (hides the overflow, does not report it).
Earliest milestone boundary:
                        With the first protection that makes a page non-present on purpose (kernel write protection, above).
Admission Test status:  not applicable (kernel design)
Status:                 candidate
```

Shape (the reviewer's, to be designed): every kernel and task stack has an unmapped guard page below it; the #PF handler runs on its own IST stack, so a fault caused by a full stack still reaches a handler that can print. Interacts with the guard-region entry above (the #PF handler is also the Wasm trap path).

```
Title:                  A page fault is a guest's trap only when the faulting RIP is in that guest's generated code
Date:                   2026-10-30
Source:                 outside AI (review of the 28 Oct architecture audit; refines the audit's #PF sketch)
Problem it solves:      The audit's sketch treats any fault whose CR2 lies in the running instance's slot, past its committed size, as that instance's trap. A fault at such an address can also be taken by kernel or runtime code acting for the guest (a host call copying a buffer), and that is a runtime bug or a host-call check that failed, not a guest trap.
Helps humans how / helps AI how:
                        Humans: a bug in a host call is reported as a kernel fault, not blamed on the guest. AI: the attribution rule is one predicate over RIP.
Alternatives rejected:  Attribute by CR2 alone (the audit's sketch). Attribute by "currently running instance" (a fault in a host call is still blamed on it).
Earliest milestone boundary:
                        With the guard-region layout (M3 memory layout).
Admission Test status:  not applicable (runtime design)
Status:                 candidate
```

Open point for review: the rule needs the address range of each instance's generated code, which the runtime (WAMR or a replacement) must expose; with an interpreter there is no generated code, so the rule needs a second form there. This reasoning is Claude's expansion of the review's one-line note and is to be checked at review.

```
Title:                  The verifier's pointer model as capabilities (base, length, permissions)
Date:                   2026-10-30
Source:                 outside AI (28 Oct architecture audit, "CHERI, CXL and accelerators"; foundation plan, Phase 1)
Problem it solves:      A pointer model designed as raw addresses makes a later move to hardware capabilities (CHERI) a language change; designed as capabilities, only the backend changes.
Helps humans how / helps AI how:
                        Humans: one definition of "what a pointer may touch". AI: bounds and permissions are data the verifier states, not conventions.
Alternatives rejected:  Raw pointers with side-tables (no single definition). Waiting for CHERI (no x86 implementation or plan; a port means Morello or RISC-V, against the x86-only rule).
Earliest milestone boundary:
                        Verifier design (register A2 and A4 share this trigger).
Admission Test status:  not run (touches the pointer rules of the language spec)
Status:                 candidate
```

Distinct from "Capabilities instead of ambient authority" (2026-10-15), which is about authority to act (files, network), not about what memory a pointer may address. CHERI does not fix Spectre.

```
Title:                  Per-core, message-shaped scheduler, telemetry and IPC structures, kept that way from the start
Date:                   2026-10-30
Source:                 outside AI (28 Oct architecture audit, "Multi-core"; foundation plan, Phase 1)
Problem it solves:      SMP breaks three current rules (one shared run queue, no TLB flushes, one LAPIC timer calibrated once); structures written single-core and shared need a rewrite when a second core arrives.
Helps humans how / helps AI how:
                        Humans: SMP becomes a deployment change. AI: one shape (a ring or a message per pair of cores) to extend.
Alternatives rejected:  Global run queue with a lock (a contended line). Replicating state before there is a second core (add agreement protocols when SMP lands).
Earliest milestone boundary:
                        The first scheduler or IPC code written after M1 (M3).
Admission Test status:  not applicable (kernel design)
Status:                 candidate
```

Shape (the audit's, from Barrelfish): one scheduler and run queue per core, tasks move only by explicit message; one telemetry ring per core read by an aggregator; IPC as single-producer, single-consumer rings per core pair with head and tail on separate cache lines; a payload buffer has one owner and sending it moves ownership. Each core needs the calibrated timer rate. Nothing here asks for a second core now.

```
Title:                  One limits header and one fix module in the compiler
Date:                   2026-10-30
Source:                 outside AI (foundation plan, Phase 1 and Phase 2; round-4 findings 2 and 3)
Problem it solves:      Parser bounds live in ast.h, parser_state.h and ir.h, and the 16 MiB parser arena is a literal in the harnesses; round 4 found E0103 and E0110 unguarded after E0111 was guarded. Fix machinery (waiting E0261 fixes, rename finalisation, edit padding, the withhold rule) is spread over ir_expr.c, ir_rename.c and ir_selfcheck.c, and fix-its are the most defect-prone code.
Helps humans how / helps AI how:
                        Humans: every bound in one place; fix-its reviewable as a unit. AI: one function every fix producer calls to check a fixed source against all bounds.
Alternatives rejected:  Keep checking bounds per fix producer (what step 22b and 22c did, one code at a time).
Earliest milestone boundary:
                        The first steps of the verifier stage, before the compiler grows (foundation plan, Phase 2).
Admission Test status:  not applicable (internal refactor, no language change)
Status:                 candidate
```

Today's state, for the record: `ir_withhold_over_limit` in `ir_selfcheck.c` already checks a fixed source against the E0111 height, the E0103 depth and the arena (steps 22b and 22c) and `make test-ir-fix-limits` tests it. The entry is about moving the bounds and the fix machinery into one home, not about a missing check.

```
Title:                  Diagnostics carry related positions as data, not inside message text
Date:                   2026-10-30
Source:                 outside AI (foundation plan, Phase 2; round-4 finding 1)
Problem it solves:      E0210 prints L:C inside its message text, so the fix self-check's identity rule has to parse or ignore message text when a fix moves a line; a position in text cannot be mapped through an edit.
Helps humans how / helps AI how:
                        Humans: messages are for reading, positions are for tools. AI: positions in `manna check --json` are fields (register P2).
Alternatives rejected:  Keep positions in message text and map them in the self-check (what step 22a did for the cases found).
Earliest milestone boundary:
                        With `manna check --json` (register P2).
Admission Test status:  not applicable (diagnostics format; docs/error_codes.md is the spec)
Status:                 candidate
```

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
