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

## 2026-10-09: Per-task execution budget (M3)

- Extends M2's loop-counter injection to a runtime budget per Wasm task; on exhaustion the task traps cleanly.
- Cost documented (P10). No model-token budgets: Deus runs no models.

## 2026-10-09: Considered and rejected ("Omni/AetherOS" brainstorm)

One line of why each, so they aren't re-proposed:

- **Strict/fluid functions:** nondeterministic, unbounded cost (P4, P10, P12).
- **Neural runtime type checks:** nondeterministic; contracts cover this.
- **Kernel vector filesystem:** no storage yet; TCB and the 120k LOC cap.
- **Token hypervisor / neural compute scheduling:** no accelerators; scope guard.
- **Agent embedding bus:** models' embeddings are incompatible; unauditable.
- **"Up to 90%" token saving:** unsourced claim.

(Add new entries above this line as they come up.)
