# The refinement loop

How new ideas get into the build, and how each stage teaches the next one. This file owns the loop; `docs/IDEAS.md` is where ideas wait, `docs/MANNA_PRINCIPLES.md` holds the Feature Admission Test they must pass, and `CLAUDE.md` points here. **VISION.md and IDEAS.md are never commitments, and the loop never changes the current stage's scope** (section 5).

The loop runs at every tag, before the next stage starts: **Capture** (any time) -> **Review** -> **Retro** -> **Measure**.

## 1. Capture (any time)

Every idea goes to `docs/IDEAS.md`. Use this template for each new entry:

```
Title:
Date:
Source:                 Bryden | Claude | outside AI | lesson
Problem it solves:      one line
Helps humans how / helps AI how:
Alternatives rejected:  one line each
Earliest milestone boundary:
Admission Test status:  not run | pass | fail + which criterion
Status:                 candidate | promoted | parked | rejected
```

**Nothing enters the build from IDEAS directly.** An entry becomes work only by being promoted at a tag (section 2).

IDEAS.md entries written before this file existed (up to 2026-10-09) use the older free-form format and are not rewritten.

### Capture log

The first entries recorded under this loop. The entries themselves live in `docs/IDEAS.md` (the headings below are the exact section titles there); this table points at them and does not repeat them.

| Date | Title (section in `docs/IDEAS.md`) | Source | Earliest milestone boundary | Admission Test | Status |
|---|---|---|---|---|---|
| 2026-10-11 | Exhaustive `match` on enums | Bryden | v1, with enums | not run | parked 2026-10-13 |
| 2026-10-11 | Required argument labels on multi-parameter calls | Bryden | v1 edition boundary (open tension with P21) | not run | parked 2026-10-13 |
| 2026-10-11 | Wrapping arithmetic builtins | Bryden | v1 stdlib, before kernel code is written in Manna | not run | parked 2026-10-13 |
| 2026-10-11 | Formatting as a check | Bryden | after `manna fmt` exists | not run | parked 2026-10-13 |
| 2026-10-11 | Offline self-describing toolchain | Bryden | tooling, after typed IR | not run | parked 2026-10-13 |
| 2026-10-11 | Change-impact report | Bryden | tooling, after effects + contracts | not run | parked 2026-10-13 |
| 2026-10-14 | Decide the generics story before the v1 stdlib | lesson | v1, before the stdlib | not run | candidate |
| 2026-10-14 | Small, in-tree v1 standard library, scoped to Deus | lesson | v1, after the generics entry (2026-10-14) is decided | not run | candidate |
| 2026-10-15 | Capabilities instead of ambient authority | lesson | v1, decided together with effects | not run | candidate |
| 2026-10-15 | Bugs trap the sandbox; errors are values | lesson | M3 (Wasm runtime), with the spec line at v1 | not run | candidate |
| 2026-10-15 | AI fluency scorecard | lesson | after the typed IR stage | not applicable | candidate |
| 2026-10-15 | Deterministic record and replay of apps | Claude | M3 (Wasm runtime design) | not run | candidate |
| 2026-10-15 | Undoable agent actions | Claude | post-M3, with the agent service | not applicable | candidate |
| 2026-10-15 | Contracts as the human review surface | Claude | v1, with contracts | not run | candidate |
| 2026-10-15 | Deterministic simulation testing of kernel logic | Claude | M3 (when IPC exists) | not applicable | candidate |
| 2026-10-15 | Hardware backstop for Spectre | lesson | the Spectre mitigation plan CLAUDE.md requires before networking | not applicable | candidate |
| 2026-10-18 | Small trusted checker, untrusted compiler | Claude | before the verifier is designed (M2) | not applicable | candidate |
| 2026-10-19 | Supervised restart of trapped tasks | outside AI | M3 (Wasm runtime + IPC) | not applicable | candidate |
| 2026-10-19 | Live component replacement | outside AI | post-M3 | not applicable | candidate |
| 2026-10-19 | Provenance on every module | outside AI | M3 (Wasm loader) | not applicable | candidate |
| 2026-10-25 | Model-chosen resource policy (AI policy agent) | Bryden | decision at M3 start (resolves register R4); build after M3, with the agent service | not applicable | candidate |
| 2026-10-25 | Single-service mode for cloud guests | Bryden | decide at M3; build after the M3 runtime and virtio exist | not applicable | candidate |
| 2026-10-30 | Kernel write protection: read-only aliases, read-only tables, page 0 unmapped | outside AI | before any untrusted code runs (register K3) | not applicable | candidate |
| 2026-10-30 | Guard-region Wasm memory: one 8 GiB slot per memory32 instance, recycled, never unmapped | outside AI | the M3 memory layout; decided before the verifier stage (register A4) | not run | candidate |
| 2026-10-30 | Restate "zero TLB flushes" as "no flush on the fast path", with the allowed flush points listed | outside AI | before M3 is designed | not applicable | candidate |
| 2026-10-30 | Kernel stack guard pages and a page-fault path with its own stack | outside AI | with the first protection that makes a page non-present on purpose | not applicable | candidate |
| 2026-10-30 | A page fault is a guest's trap only when the faulting RIP is in that guest's generated code | outside AI | with the guard-region layout | not applicable | candidate |
| 2026-10-30 | The verifier's pointer model as capabilities (base, length, permissions) | outside AI | verifier design (registers A2, A4) | not run | candidate |
| 2026-10-30 | Per-core, message-shaped scheduler, telemetry and IPC structures, kept that way from the start | outside AI | the first scheduler or IPC code after M1 (M3) | not applicable | candidate |
| 2026-10-30 | One limits header and one fix module in the compiler | outside AI | first steps of the verifier stage | not applicable | candidate |
| 2026-10-30 | Diagnostics carry related positions as data, not inside message text | outside AI | with `manna check --json` (register P2) | not applicable | candidate |

Two existing entries were **updated, not duplicated**, on 2026-10-30 (a dated update block inside each, status unchanged): "Hardware backstop for Spectre" (a Spectre boundary for mutually distrusting tenants; the trigger moves from "before networking" to "before M3 runs untrusted code" if approved) and "Small trusted checker, untrusted compiler" (the Wasm runtime as the real trusted base; the resident compiler as an unprivileged Wasm guest). The nine new entries come from the 28 Oct architecture audit and foundation plan and the reviews of them; the template fields are in the entries.

**Review 2026-10-13 (`m2-parser-v0.1`):** the six 2026-10-11 candidates are **Parked** (reason: none belongs in the IR skeleton); none promoted, none rejected. §2 says "pick at most 3 candidates"; reviewing all six is a deliberate deviation: the cap exists to limit promotions and Admission Test runs, and neither happened (no Admission Test was run, nothing entered the next stage's plan). Recorded in each entry's status line in `docs/IDEAS.md`.

Five rejections were logged the same day under "Considered and rejected" in `docs/IDEAS.md` (distinct integer types, compile-time evaluation, catch-all match arm, expression-bodied functions, string interpolation), each with its reason.

## 2. Review (at each tag)

1. Pick **at most 3** candidates from `docs/IDEAS.md`.
2. Run the Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) on each.
3. Decide, for each: **Promote** (into the next stage's plan document), **Park** (stays in IDEAS.md, with the reason), or **Reject** (move it to the rejected log in IDEAS.md with one line of why).
4. **At most 3 promotions per tag.** **Nothing is promoted mid-stage:** an idea that arrives while a stage is running waits for the next tag, however good it is.

Record the decisions in the plan document of the next stage (promotions) and in IDEAS.md (parks and rejections).

**Review 2026-10-30 (`m2-ir-v0`): DECIDED by Bryden on 2026-10-31** (decision cards; the proposals were made by Claude Code on 2026-10-30 and are kept below as made). Three candidates were picked (the cap is 3) and all three are promoted, which is the cap of three promotions for this tag. `CLAUDE.md` is untouched: rewording the TLB hard constraint needs Bryden's separate explicit OK and is deferred to Phase 1. Where each decision is recorded: this section, `docs/IDEAS.md` (status "promoted" on each entry), and `docs/DEGRADATION.md` (A2 closed, A5's trigger moved, R2 closed). **There is no verifier-stage plan document yet** (`docs/history/` holds only the archived typed-IR and parser working files and the audit briefs), so the three decisions are not recorded in a plan document; none was created. Carry them into the verifier-stage plan when it is written.

**Decisions (Bryden, 2026-10-31):**

1. **Small trusted checker, untrusted compiler: PROMOTED as a decision for the verifier stage.** Closes register A2. The trusted checker's line budget is **5,000 lines of C, excluding tests and generated tables, revisited when the verifier-stage plan is written** (Bryden, 2026-10-31); it is separate from the kernel's 120k. Not decided: how the checker is specified or proved, and which component is checked. Nothing is built now, and there is no Wasm-guest build of the compiler (that waits for M3).
2. **Spectre boundary: PROMOTED, the threat model only.** Register A5's trigger moves from "before networking" to "before M3 runs untrusted code" (a settled decision changed: A5's trigger-id goes from `m3-start` to `untrusted-code`). Not decided: the choice between (a) protection keys and (b) separate page tables, which stays open for the M3 design. The one-CR3 rule is unchanged.
3. **Guard-region Wasm memory and the TLB rule: PROMOTED, the written decision only**, including the flush-on-slot-recycle condition (recycling a slot makes every page beyond the new instance's size non-present again, which is a flush; teardown is a flush point, batched per epoch, never on the fast path). Nothing is built. `CLAUDE.md` is NOT edited: rewording "zero TLB flushes" needs Bryden's separate OK and is deferred to Phase 1.

The Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) is written for language changes. None of the three is one, so criteria 2 and 5 are read as "has a defined test / has no second way in the project", which is a stretch of the text and is said here so it is not mistaken for a plain pass.

| Criterion | 1. Small trusted checker, untrusted compiler | 2. Spectre boundary for distrusting tenants (with the threat model) | 3. Guard regions with the TLB rule |
|---|---|---|---|
| 1. Violates no principle 1-20 or locked decision | holds: supports principle 8 (unsafe by declaration) and principle 11 (AI-emitted AST passes the same verifier); principle 12 (deterministic builds) is not touched | holds: no principle is touched (a threat model is a document) | holds: principle 10 (predictable cost) is served by the removal of per-access checks; no locked decision touched |
| 2. Spec entry, goldens, error messages | no language spec entry needed; proposed test: a deliberately crashing mannac build leaves the kernel and other guests running (needs M3, so not testable in the next stage) | no spec entry; proposed test: a Spectre-v1 Wasm program cannot read another sandbox's secret (needs M3) | no language spec entry; proposed test: an out-of-bounds load traps with no bounds-check instruction emitted (needs M3 and a backend) |
| 3. Fits the next stage's scope | holds for the **decision** (verifier design is the next stage, register A2's trigger is `verifier-design`); the guest-compiler build does not fit before M3 | holds for the **document** only; the mitigation does not fit before M3 | holds for the **written decision** (it decides whether the verifier proves linear-memory bounds, register A4); the layout and slot allocator are M3 work |
| 4. Machine cost documented | not yet: needs the trusted checker's line budget (separate from the kernel's 120k) | not yet: the cost of option (a) (PKS support check) or (b) (breaks one CR3) is unmeasured | partly: the audit gives the layout's cost (about 16,000 slots, split-page TLB entries) but not a measurement; teardown flushes are the cost to state |
| 5. No second way to express something that exists | holds | holds | holds |

**Proposals as made 2026-10-30** (one reason each; decided above, kept for the record):

1. **Small trusted checker, untrusted compiler: PROPOSED Promote**, as a decision for the verifier-stage plan (it closes register A2 by being decided). Reason: the next stage designs the verifier, and A2's trigger is exactly "before the verifier is designed"; deciding it afterwards leaves the whole compiler in the trusted base by default. This repo has measured evidence: four external audit rounds and fuzzing still found internal errors at the boundaries (round 4), and its fix self-check is already a checker that does not trust the producer of a fix. Condition: the promotion is the decision and the checker's line budget, not building a Wasm-guest compiler, which waits for M3.
2. **Spectre boundary: PROPOSED Promote the threat model only** (a document naming which tenants may share the address space and the second boundary for those that distrust each other). Reason: the audit's argument that language isolation cannot keep confidentiality in one address space has not been answered anywhere in this repo (register A5 has a soft trigger, "before networking"), and the verifier's design depends on whether it must insert speculation barriers. The choice between options (a) protection keys and (b) separate page tables stays open: (b) breaks the one-CR3 constraint and is Bryden's decision. Moving A5's trigger to "before M3 runs untrusted code" is a register change that follows only if he approves it.
3. **Guard regions with the TLB rule: PROPOSED Promote the written decision** (the TLB rule restated, the allowed flush points listed, slot recycling as a flush point batched per epoch), **not the build**. Reason: it decides whether the verifier needs to prove linear-memory bounds at all (register A4's 5-20% budget assumes no guard pages), which is verifier-design input; building it needs the M3 runtime and a backend that does not exist. This is a **hard-constraint area**: the restated rule must not be written into `CLAUDE.md` until Bryden says so explicitly.

All three were promoted, the cap of three promotions for this tag. Not proposed this tag, and why: kernel write protection is the audit's own rank 1 but is already covered by register K3 (trigger `untrusted-code`) and needs a hard-constraint edit too; the other seven captured entries wait for their own boundaries.

## 3. Retro (at each tag)

A dated section below: **what went wrong this stage, the rule that prevents it, and where that rule now lives** (CLAUDE.md, a test, or CI). A rule that lives nowhere enforceable is stated as such.

### Retro 2026-10-08: parser r1 Phase A, commit `da9f01c`

- **What went wrong.** Phase A re-baselined the golden `tests/parser/err_recovery_stress_no_infinite_loop.ast` from 7 diagnostics to 1 so that it would match a regression in `sync_skip` (a lone unmatched `(` or `[` swallowed every following `}`, `fn` and `struct`). CI stayed green because the golden had been changed to match the output. The regression shipped inside the tag `m2-parser-r1` and was found by an external audit afterwards (`docs/STATUS.md`, r1 entry).
- **The rule.** *A golden that loses diagnostics or AST nodes is STOP-tier:* the report must list every removed line and why.
- **Where it lives.** `CLAUDE.md`, section "Lessons", the last bullet ("A golden that loses diagnostics or AST nodes in a commit is STOP-tier ..."), which cites `da9f01c` by name. The golden itself was restored to 7 diagnostics in `e3d6652` and still has 7 (checked by command on 2026-10-12). **Enforced since step 20a by `make test-ratchet` (`tools/ratchet.py`):** CI compares the number of diagnostics in every expected output under `tests/` across the whole history of the baseline file (`tests/ratchet.json`: every consecutive pair of committed versions), so a golden edited to match a regression fails, whichever CI run was cancelled or red, unless the register carries a `RATCHET-WAIVER` line for it. Until step 20 it was a review rule, because CI only compared output to the golden.

### Retro 2026-10-13: typed-IR step 2 / tag `m2-parser-v0.1`

- **(a) What went wrong.** `tools/height_probe.c` never freed its input buffer. Under the reviewer's Linux LSan run every measurement probe exited nonzero from the leak report and the measurement script classed each exit as a crash (`docs/STATUS.md`, reviewer's Linux run). Fixed in `b4bc92b`.
  - **The rule.** Test harnesses and measurement probes must be leak-clean like product code, and a measurement script counts only a signal exit (rc > 128) as a crash.
  - **Where it lives.** The leak-clean half is enforced only by the reviewer's Linux LSan run (no CI job runs LSan). The rc > 128 half is a script rule: stated here, and applied in `tools/measure_ir_stack.sh` (typed-IR step 3b). `tools/measure_expr_height.sh` is not yet changed to follow it.
- **(b) What went wrong.** `leaks --atExit` hung on macOS during the `m2-parser-v0.1` verification: `arena_test` sat at 0% CPU for ~6 minutes and was killed, so no figure was produced.
  - **The rule.** The reviewer's Linux LSan run is the leak evidence of record; a macOS `leaks` figure is optional and its absence is not a gap.
  - **Where it lives.** This file and the `docs/STATUS.md` verification snapshot, which already says no macOS `leaks` figure is claimed for this tag. Not enforced by any check.

### Retro 2026-10-30: typed-IR stage / tag `m2-ir-v0`

Four external audit rounds and steps 22a to 22l (round 4). Severity fell round by round; round 4 found no wrong IR and no accepted invalid program, and its findings sat at limits, in tooling, or where two policies met. One rule each, with where it lives.

- **(a) What went wrong.** A fix-it was checked only against the one parser bound somebody had thought of. E0111 (expression height) was guarded after round 1; round 4 then found the E0261 wrap past E0103 (nesting depth, finding 2) and a rename past E0110 (the 16 MiB parser arena, finding 3), each producing a fix whose fixed source the parser rejects.
  - **The rule.** A fix is checked against every parser limit before it is offered, by parsing the fixed source, never by estimating; a fix that would exceed one is withheld and the mistake is reported without it.
  - **Where it lives.** `ir_withhold_over_limit` (`tools/mannac/ir_selfcheck.c`, run first by `ir_fix_selfcheck`), documented in `docs/error_codes.md` ("Fix-it rules"), tested by `make test-ir-fix-limits` (the arena boundary at 35708 and 35709 uses) and by the step-22b/22c fix cases under `make test-ir-fix`. **Corrected 2026-10-31** (the earlier text said the E0103 and E0111 boundary tests did not exist; step 22A added some). What exists, all for fixes of E0261 and run by `make test-ir-fix` (and its ASan twin): E0103, parentheses, `tests/ir_fix/fix_e0261_wrap_holds_at_253_nested_parentheses` (kept) and `..._would_pass_the_nesting_limit_at_254_nested_parentheses_so_there_is_no_fix` (withheld); E0103, nested ifs, `fix_e0261_wrap_holds_at_126_nested_ifs` (kept) and `..._at_127_nested_ifs_so_there_is_no_fix` (withheld), generated by `tools/gen_ir_fix_cases_step22.py`; E0111, `fix_e0261_chain_of_510_operations_keeps_its_fix` / `..._of_511_operations_has_no_fix_...` and the same pair "at the bottom of" a chain, plus `fix_e0261_on_a_short_path_beside_a_512_high_expression_keeps_its_fix` (step 19). In `ir_test.c` `test_fix_limits` (`make test-ir-unit`) pins a withheld fix at 300 parentheses (E0103) and at a 600-operation chain (E0111), far past each bound, and one kept control fix; it has no at-the-bound or bound-minus-one case. **Still missing:** (1) the E0103 and E0111 boundary cases are not a target of their own: `make test-ir-fix-limits` (`tools/ir_fix_limits_test.py`) covers only the E0110 arena boundary (35708 offered, 35709 withheld); (2) no boundary case exists for a fix other than E0261's wrap (a rename can make a source taller or deeper only through its own text length, so the E0110 case is the one that applies to it); (3) nothing checks that every parser limit has such a pair (the foundation plan's "Every bound gets boundary tests"). That part is register entry R6's and the plan's, not enforced.
- **(b) What went wrong.** The fix self-check compared a fix's result by line and column, so a fix that deleted or moved lines (a multi-line attribute, round 4 finding 1) made it compare the wrong positions, and E0210 prints a position inside its message text, which no edit-mapping can reach.
  - **The rule.** The self-check never assumes that an edit keeps lines or quoted positions: every position is mapped through the applied edits.
  - **Where it lives.** `ir_fix_selfcheck` and `ir_rename.c` (step 22a, `cfc52cbf`); the cases are the four `tests/ir_fix/fix_selfcheck_*` inputs about a deleted two-line attribute (one rename moved up a line, three E0210 cases), generated by `tools/gen_ir_fix_cases_step22.py` and run by `make test-ir-fix`. The idea of positions as data is `docs/IDEAS.md` "Diagnostics carry related positions as data".
- **(c) What went wrong.** 46 of the 449 generated no-cascade goldens never reached the consumer their name claims (`Decl_function_decl_return_type__*`, `Field_type__*`, `Type_slice_elem__param_*`): a declaration error stops the build before the bodies, so the named body consumer never ran, and the proof counted them as evidence (round 4 finding 9).
  - **The rule.** A generated golden must reach the consumer its name claims; one that cannot is named for what it tests (`declaration_stops_bodies_`).
  - **Where it lives.** `make check-nocascade-reach` (`tools/check_nocascade_reach.py`, step 22k): an added unrelated body mistake must show beside a body golden's own diagnostics, and must not change a declaration-level golden. Run on the base's goldens it failed exactly the 46. It is in `make nocascade-all` and both CI jobs that run the sweep. Limit: variants whose unknown type reaches a body consumer for those three holes are not produced under the settled design (register C1 records it).
- **(d) What went wrong.** A prompt line of step 22B (the control line) contradicted a settled decision, C2c's STOP 2 (the struct total as a lower bound); step 22d (`dd37c013`) followed the prompt, and `0c648fa3` restored C2c afterwards.
  - **The rule.** A prompt or plan line that touches a settled decision must name that decision, so that the author and the reader both see the conflict before work starts. **This rule is process only: no tool can check it.**
  - **Where it lives.** This file only, and register entry R6 (review-time rules, re-read at every tag): stated as unenforced, with R6's each-tag trigger.

**Points for the next review** (found while closing the stage; none is a defect report, each is a place to look):

1. The **step-10 unit-silent rule** beside a poisoned part: E0226 (a `unit` value used as a value) is "not reported where another error already says the same thing" (`docs/error_codes.md`); check whether that silence still holds when the other part of the expression is poisoned (the D1 rule, step 22g).
2. **E0234 leaving a parameter spelling unrenamed:** the real case from round 4 is `g(myY: 1)`: a named argument whose parameter spelling (`myY`) is not renamed when the parameter is, because E0234 checks the named argument for its own mistakes only; check the rename fix against it.
3. **Fix withholding lives inside the self-check, and its largest-bytes rule can over-drop on depth.** When only the combination of fixes exceeds a limit, the fix adding the most bytes goes first (`docs/error_codes.md`, fix rules); for the depth limit (E0103) the byte count is not what decides, so a different fix than the largest may be the one to withhold, and the rule may withhold a fix that was not needed.

## 4. Measure (at each tag)

Add one row per tag. Fill a cell only with a number from a command run at that tag, name the command, and leave the cell blank otherwise. A blank is information; a guess is not.

| Tag | Date | Goldens (lexer + parser) | Unit tests (PASS lines) | Fuzz, zero crashes | Open known issues | Token-benchmark ratio | IR goldens (`tests/ir`) | No-cascade goldens | Fix cases (`tests/ir_fix`) | Audit repros |
|---|---|---|---|---|---|---|---|---|---|---|
| `m2-parser-v0.1` | 2026-10-12 | 15 + 376 = 391 | `parser_fixes_test` 20, `ast_test` 7, `parser_state_test` 5, `arena_test` 9 = 41 | 601 s + 601 s (479,554 + 439,834 execs), 0 crashes (*) | 0 open in STATUS "Known issues" (1 fixed); 5 accepted limitations in M2_PLAN "Known limitations" | | | | | | |
| `m2-ir-v0` | measured 2026-10-30 at `645177b6`; tag date filled at tagging | 15 + 376 = 391 | `ir_test` 96, `parser_fixes_test` 20, `ast_test` 7, `parser_state_test` 5, `arena_test` 9 = 137 | `fuzz-ir` 600 s (228,757 execs) + `fuzz-ir-ungated` 600 s (225,014 execs), 0 crashes (**) | 24 open + 2 partly closed of 41 register entries (`docs/DEGRADATION.md`); the STATUS "Known issues" list is not counted | | 501 | 449 | 145 | 58 |

(*) Cited from the runs recorded in the `docs/STATUS.md` verification snapshot (`make fuzz-parser DURATION_PARSER=600`, twice, 2026-10-12), not re-run for this commit; every other cell was produced by the commands below at the time of writing.

Commands run for the `m2-parser-v0.1` row (all on a clean tree at `29f0037`+, 2026-10-12):

```
ls tests/lexer/*.mn  | wc -l                    # 15
ls tests/parser/*.mn | wc -l                    # 376
make test-lexer            # 15 PASS / 0 FAIL
make test-parser           # 376 PASS / 0 FAIL
make test-determinism      # 391 PASS / 0 FAIL  (= 15 + 376)
make test-parser-fixes     # 20 PASS / 0 FAIL
make test-ast              # 7 PASS / 0 FAIL
make test-parser-state     # 5 PASS / 0 FAIL
make test-arena            # 9 PASS / 0 FAIL
awk '/^### Known limitations/{p=1;next} /^### /{p=0} p&&/^- /' docs/M2_PLAN.md | wc -l    # 5
```

(**) The `m2-ir-v0` fuzz cell is cited from the 22D batch run (`make fuzz-ir` and `make fuzz-ir-ungated`, 600 s each, at `645177b6`, 2026-10-30; "No crash/timeout artifacts" for both), not re-run for this docs-only commit. No lexer or parser fuzz was run for this row, so it says nothing about them. Every other cell of the row was produced by the commands below at `645177b6` plus these docs, 2026-10-30.

Commands run for the `m2-ir-v0` row (clean tree apart from the docs and untracked fuzz corpus growth):

```
make test-lexer; make test-parser          # 15 PASS, 376 PASS (the lexer + parser goldens, unchanged: 15 + 376 = 391)
make test-ir-unit                          # 96 PASS
make test-parser-fixes; make test-ast; make test-parser-state; make test-arena    # 20, 7, 5, 9 PASS
ls tests/ir/*.mn | wc -l                   # 501
ls tests/nocascade/*.mn | wc -l            # 449 (tests/nocascade/INDEX.tsv has 449 rows)
make test-ir-fix                           # 145 PASS (one per input of tests/ir_fix: 292 .mn files, 106 .remaining)
make test-audit-repros                     # "58 inputs, all match"
make check-register                        # "41 entries, 130 citations checked"
grep -E '^- \*\*Status:\*\*' docs/DEGRADATION.md | awk '{print $2,$3}' | sort | uniq -c    # OPEN 24, PARTLY 2, CLOSED 14, NOT A DEGRADATION 1
```

**The foundation plan's metrics at `m2-ir-v0`** (the plan of 28 Oct 2026, measured at its `761eb7a`; each "Measured" cell comes from the command in the last column at `645177b6` plus these docs, 2026-10-30; a blank is not measured; where this file's definition differs from the plan's, the plan's number is not comparable and the cell says so):

| Metric | Plan, 28 Oct | Measured at `m2-ir-v0` | Command |
|---|---|---|---|
| Internal errors (exit 4) found by an external audit | 3 in round 4 | | the audit report (no round has run since; not a command) |
| Parser bounds that fix-its are checked against | E0111 only | 3 named by the fix rule (E0103, E0110, E0111); boundary tests: E0110 in `make test-ir-fix-limits` (35708 offered, 35709 withheld); E0103 (253/254 parentheses, 126/127 nested ifs) and E0111 (510/511-operation chains) as E0261 fix cases under `make test-ir-fix`, none of them a target of their own (corrected 2026-10-31; see Retro 2026-10-30 (a)) | `grep -n "A fix whose fixed source exceeds" docs/error_codes.md`; `make test-ir-fix-limits` |
| No-cascade sinks proven only by a crash | 18 of 53 | 19 of 59 (56 static sinks + 3 muting sites, 59 proven); all 19 carry a written reason (rule F8); the plan's target of 15 is **not met** | `make check-nocascade-prove` (the `NUMBERS:` line) |
| Generated no-cascade goldens that reach their consumer | 403 of 449 | 403 body goldens pass the check; the other 46 are declaration-level, renamed `declaration_stops_bodies_*`, and pinned as not reaching a body; 449 of 449 pass the check | `make check-nocascade-reach` |
| Single-mistake mutations giving anything but one E0230 | 0 of 6,111 | | the auditor's script is not in the repo and no CI target runs it (the plan's practice 2 is not done) |
| Fix-it coverage (register P1) | 8 of 55 codes | 8 of 55 codes (E0102, E0107, E0108, E0109, E0215, E0261, E0280, E0281) | `python3` count of the rows of `docs/error_codes.md` whose fix column does not start with "None" (command below); `make test-ir-error-codes` says "44 documented as emitted, 43 with a golden, 28 with a unit test" (a different count) |
| Guard scripts | 32 files, 4,909 lines | | the plan's definition of a "guard script" is not in the repo, so no number is claimed; all of `tools/*.py` and `tools/*.sh`: 87 files, 10,383 lines (plan: 81 scripts, 9,762 lines); `tools/check_*.py`: 16 files, 1,945 lines |
| Open register entries | 24 open, 2 partly closed (41 total) | 24 open, 2 partly closed, 14 closed, 1 not a degradation (41 total); none past its trigger | `make check-register` and the status count above |
| Test and tooling code : product code | 15,281 : 10,738 (1.4 : 1) | 16,297 : 10,650 (1.53 : 1) with this file's definition (below); the plan's definition is not in the repo | `wc -l` over the file lists below |
| STATUS.md length | 9,627 words | 9,731 words in 119 lines | `wc -w docs/STATUS.md` |
| Top-level docs | 64,564 words | 66,129 words | `wc -w *.md docs/*.md` |
| CI time per push | no-cascade job 487 s; others not recorded | run 41156604750 (`645177b6`): wall 11 min 19 s; `mannac tests` 11 min 13 s, `Kernel boot smoke` 58 s, `No-cascade injection sweep` 9 min 23 s (563 s), `Linux gcc` 9 min 25 s. The run before (41136356718, `96dfe215`): wall 11 min 54 s; no-cascade 5 min 20 s, `mannac tests` 11 min 47 s, Linux 10 min 44 s, boot 56 s. **No-cascade 320 s to 563 s, cause (R3, 2026-10-31, from `gh run view <id> --json jobs`, step times): the runner, not the new work.** Every step slowed 1.7x to 2.7x, including ones no commit touched: checkout 1 s to 7 s, Toolchain 14 s to 25 s, `check-inject-hook-absent` 6 s to 16 s (`tools/check_inject_hook_absent.sh` unchanged), `test-nocascade-inject` 66 s to 124 s (sweep tool unchanged; same 449 goldens), `check-nocascade-sinks` 26 s to 46 s, `check-nocascade-prove` 201 s to 336 s. The new work is small: `PATHS.lock` went from 215 to 222 lines (+7, about 3%), `check-nocascade-reach` is not a step of that job (it runs in the macOS and Linux jobs), and the 46 renamed goldens are the same count. Not isolated by a local timing; the Linux job went the other way (10 min 44 s to 9 min 25 s) | `gh run view <id> --json jobs` (started and completed times) |

Definitions for the line-count rows (the plan's own are not in the repo): **product** = `kernel/` (`*.c`, `*.h`, `*.S`, `*.ld`: 1,303 lines) plus the mannac files `ir.c ir.h ir_body.c ir_build.c ir_calls.c ir_decls.c ir_expr.c ir_internal.h ir_names.c ir_rename.c ir_selfcheck.c` (4,296), `parser.* parser_state.* lexer.* ast.* arena.*` (4,053) and `diag.c diag.h` (998) = 10,650; **test and tooling** = `tools/*.py` and `tools/*.sh` (10,383) plus every other `tools/mannac/*.c|*.h` (drivers, unit tests, fuzz harnesses: 5,914) = 16,297. `ir_expr.c` alone is 1,298 lines (plan: 1,275). `tools/gen_*.py`: 31 files, 3,330 lines. The fix-it count command:

```
python3 - <<'PY'
import re
rows=[l for l in open('docs/error_codes.md') if re.match(r'\| `E\d{4}` \|',l)]
cells=lambda l:[c.strip() for c in l.strip().strip('|').split(' | ')]
print(len(rows), sum(1 for l in rows if not cells(l)[-1].startswith('None')))   # 55 8
PY
```

The token-benchmark ratio stays blank until the benchmark in `docs/IDEAS.md` ("Token benchmark suite") exists and has baseline numbers.

## 5. Guardrails

- **`VISION.md` and `docs/IDEAS.md` are never commitments.** A candidate is not planned work until it is promoted at a tag, and not built work until its stage's tests pass.
- **The loop never changes the current stage's scope.** Review happens between stages, and nothing is promoted mid-stage.
- **The loop does not weaken the Feature Admission Test.** Promotion means "enters the next stage's plan"; the test's five criteria still have to hold, with goldens, before a language change is admitted.

## Retro-rule inventory (step 20i)

`CLAUDE.md` ("No degradation"): *every retro rule lives in a check, or sits in the register as "unenforced" with a trigger.* This table is that rule's check. `make check-retro-rules` (`tools/check_retro_rules.py`) enumerates the rules itself (every bullet of `CLAUDE.md` "Lessons", "No degradation" and "Working conventions", and every retro line of this file that is marked in bold as the rule) and fails if one has no row, if an anchor is on no rule, if a `make` target named here does not exist or no workflow runs it, or if a register entry named here does not exist or is CLOSED. A row whose only item is a register entry is an UNENFORCED rule; a row that names a `make` target AND a register entry is PARTLY enforced (the check covers only part of the rule, such as the batch cap for the approval tiers or W^X for "ask before changing a hard constraint"; the register entry holds the rest). The checker prints the three counts apart: enforced, partly, unenforced. The review-time rules sit in register entry R6, re-read at every tag.

| Source | Anchor (a quote of the rule) | Enforced by |
|---|---|---|
| CLAUDE.md | Proof over reasoning, always - run the actual limit under ASan | make test-ir-unit; make test-parser-fixes |
| CLAUDE.md | Grammar guarantees underpin recovery - re-verify them before reusing them. | register R6 |
| CLAUDE.md | Any new recursive parse function needs the depth invariant checked, not assumed. | make test-parser-fixes; make test-parser |
| CLAUDE.md | Quote-check doc changes against the actual repo, not memory. | make check-register; register R6 |
| CLAUDE.md | Verify CI actually runs the tests you think it does, every time you add one. | make check-ci-targets |
| CLAUDE.md | Prefer a prebuilt binary over building a toolchain from source via brew | register R6 |
| CLAUDE.md | A golden that loses diagnostics or AST nodes in a commit is STOP-tier | make test-ratchet |
| docs/REFINEMENT.md | A golden that loses diagnostics or AST nodes is STOP-tier | make test-ratchet |
| docs/REFINEMENT.md | Test harnesses and measurement probes must be leak-clean like product code | make test-measure-height-script; make test-ir-unit-asan |
| docs/REFINEMENT.md | The reviewer's Linux LSan run is the leak evidence of record | make test-ir-unit-asan |
| CLAUDE.md | Nothing is stubbed, removed, withheld, weakened, or labelled | make check-register; register R5 |
| CLAUDE.md | No tag or milestone closes while any register entry's trigger has passed. | make check-register |
| CLAUDE.md | A milestone closes only on its full spec. | make check-register; register K2 |
| CLAUDE.md | A test, golden, fix case, repro or diagnostic count never decreases | make test-ratchet |
| CLAUDE.md | Every retro rule lives in a check, or sits in the register as | make check-retro-rules |
| CLAUDE.md | Silent fallbacks are banned: a self-check failure is a hard failure | make test-ir; make test-e0261-exhaustive |
| CLAUDE.md | The batch cap (at most 3 steps, then STOP; see | make check-batch-cap |
| CLAUDE.md | Neither Claude Code nor the reviewer offers | register R5 |
| CLAUDE.md | Small commits, one subsystem each. Run `make run` before claiming success. | make test-boot; register R6 |
| CLAUDE.md | Read `serial.log` and `qemu.log` to verify. Don't assume. | make test-boot |
| CLAUDE.md | Ask before changing any hard constraint above. | make test-wx; register R6 |
| CLAUDE.md | Write commit messages via a quoted heredoc | register R6 |
| CLAUDE.md | **Approval tiers**, for incremental multi-step work | register R6 |
| CLAUDE.md | **ROUTINE** - tests pass, no design choice made, no spec deviation | make check-batch-cap; register R6 |
| CLAUDE.md | **STOP** - a design choice, a spec deviation, a failing test | register R6 |
| CLAUDE.md | Every step report includes a plain-English summary | register R6 |
| CLAUDE.md | **Rule: no behavior change without a test.** | make test-ratchet; register R6 |
| CLAUDE.md | **At every tag**, list uncovered lines in `lexer.c` | make coverage-check |
| CLAUDE.md | At every tag, run the refinement loop in docs/REFINEMENT.md | register R6 |
| CLAUDE.md | A silencer is proven by removing it, not by reading it. | make check-nocascade-prove; make check-nocascade-wired |
| CLAUDE.md | A red CI run is not always a failing test: read the check-run annotations before the code. | make test-batch-cap-script; make test-makefile-no-qemu |
| docs/REFINEMENT.md | A fix is checked against every parser limit before it is offered | make test-ir-fix-limits; make test-ir-fix |
| docs/REFINEMENT.md | The self-check never assumes that an edit keeps lines or quoted positions | make test-ir-fix |
| docs/REFINEMENT.md | A generated golden must reach the consumer its name claims | make check-nocascade-reach |
| docs/REFINEMENT.md | A prompt or plan line that touches a settled decision must name that decision | register R6 |
