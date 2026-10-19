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

**Review 2026-10-13 (`m2-parser-v0.1`):** the six 2026-10-11 candidates are **Parked** (reason: none belongs in the IR skeleton); none promoted, none rejected. §2 says "pick at most 3 candidates"; reviewing all six is a deliberate deviation: the cap exists to limit promotions and Admission Test runs, and neither happened (no Admission Test was run, nothing entered the next stage's plan). Recorded in each entry's status line in `docs/IDEAS.md`.

Five rejections were logged the same day under "Considered and rejected" in `docs/IDEAS.md` (distinct integer types, compile-time evaluation, catch-all match arm, expression-bodied functions, string interpolation), each with its reason.

## 2. Review (at each tag)

1. Pick **at most 3** candidates from `docs/IDEAS.md`.
2. Run the Feature Admission Test (`docs/MANNA_PRINCIPLES.md`) on each.
3. Decide, for each: **Promote** (into the next stage's plan document), **Park** (stays in IDEAS.md, with the reason), or **Reject** (move it to the rejected log in IDEAS.md with one line of why).
4. **At most 3 promotions per tag.** **Nothing is promoted mid-stage:** an idea that arrives while a stage is running waits for the next tag, however good it is.

Record the decisions in the plan document of the next stage (promotions) and in IDEAS.md (parks and rejections).

## 3. Retro (at each tag)

A dated section below: **what went wrong this stage, the rule that prevents it, and where that rule now lives** (CLAUDE.md, a test, or CI). A rule that lives nowhere enforceable is stated as such.

### Retro 2026-10-08: parser r1 Phase A, commit `da9f01c`

- **What went wrong.** Phase A re-baselined the golden `tests/parser/err_recovery_stress_no_infinite_loop.ast` from 7 diagnostics to 1 so that it would match a regression in `sync_skip` (a lone unmatched `(` or `[` swallowed every following `}`, `fn` and `struct`). CI stayed green because the golden had been changed to match the output. The regression shipped inside the tag `m2-parser-r1` and was found by an external audit afterwards (`docs/STATUS.md`, r1 entry).
- **The rule.** *A golden that loses diagnostics or AST nodes is STOP-tier:* the report must list every removed line and why.
- **Where it lives.** `CLAUDE.md`, section "Lessons", the last bullet ("A golden that loses diagnostics or AST nodes in a commit is STOP-tier ..."), which cites `da9f01c` by name. The golden itself was restored to 7 diagnostics in `e3d6652` and still has 7 (checked by command on 2026-10-12). **No automated check enforces the rule:** it is a review rule, because CI only compares output to the golden and cannot see that the golden was edited to match.

### Retro 2026-10-13: typed-IR step 2 / tag `m2-parser-v0.1`

- **(a) What went wrong.** `tools/height_probe.c` never freed its input buffer. Under the reviewer's Linux LSan run every measurement probe exited nonzero from the leak report and the measurement script classed each exit as a crash (`docs/STATUS.md`, reviewer's Linux run). Fixed in `b4bc92b`.
  - **The rule.** Test harnesses and measurement probes must be leak-clean like product code, and a measurement script counts only a signal exit (rc > 128) as a crash.
  - **Where it lives.** The leak-clean half is enforced only by the reviewer's Linux LSan run (no CI job runs LSan). The rc > 128 half is a script rule: stated here, and applied in `tools/measure_ir_stack.sh` (typed-IR step 3b). `tools/measure_expr_height.sh` is not yet changed to follow it.
- **(b) What went wrong.** `leaks --atExit` hung on macOS during the `m2-parser-v0.1` verification: `arena_test` sat at 0% CPU for ~6 minutes and was killed, so no figure was produced.
  - **The rule.** The reviewer's Linux LSan run is the leak evidence of record; a macOS `leaks` figure is optional and its absence is not a gap.
  - **Where it lives.** This file and the `docs/STATUS.md` verification snapshot, which already says no macOS `leaks` figure is claimed for this tag. Not enforced by any check.

## 4. Measure (at each tag)

Add one row per tag. Fill a cell only with a number from a command run at that tag, name the command, and leave the cell blank otherwise. A blank is information; a guess is not.

| Tag | Date | Goldens (lexer + parser) | Unit tests (PASS lines) | Fuzz, zero crashes | Open known issues | Token-benchmark ratio |
|---|---|---|---|---|---|---|
| `m2-parser-v0.1` | 2026-10-12 | 15 + 376 = 391 | `parser_fixes_test` 20, `ast_test` 7, `parser_state_test` 5, `arena_test` 9 = 41 | 601 s + 601 s (479,554 + 439,834 execs), 0 crashes (*) | 0 open in STATUS "Known issues" (1 fixed); 5 accepted limitations in M2_PLAN "Known limitations" | |

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

The token-benchmark ratio stays blank until the benchmark in `docs/IDEAS.md` ("Token benchmark suite") exists and has baseline numbers.

## 5. Guardrails

- **`VISION.md` and `docs/IDEAS.md` are never commitments.** A candidate is not planned work until it is promoted at a tag, and not built work until its stage's tests pass.
- **The loop never changes the current stage's scope.** Review happens between stages, and nothing is promoted mid-stage.
- **The loop does not weaken the Feature Admission Test.** Promotion means "enters the next stage's plan"; the test's five criteria still have to hold, with goldens, before a language change is admitted.
