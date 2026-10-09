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

(Add new entries above this line as they come up.)
