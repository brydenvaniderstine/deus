# Degradation register

Written 2026-10-23 from Bryden's rule "No degradation" (`CLAUDE.md`) and the reviewer's whole-project audit (`degradation-audit-2026-10-23.md`, kept by Bryden; sections 1-6 are seeded below). **Every entry here is a promise that is weakened, stubbed, withheld or labelled accepted.** Nothing may be added without Bryden's explicit OK; no tag or milestone closes while an entry's trigger has passed.

How to read an entry: **Promise** is what the project said (a `file:line` quote), **Where it stands** is what exists, **Trigger** is the hard revisit point (a tag, milestone or step). Triggers marked "PROPOSED by Claude Code, approved by Bryden 2026-10-24" were chosen by Claude Code where the audit gave none; Bryden approved them as written. **Approval record (Bryden, 2026-10-24; C9 and C10 and the closing of C5 approved the same day):** Bryden approves every entry's existence and its trigger. No entry is approved as permanent: each closes by its trigger, and no tag or milestone closes while an entry's trigger has passed. All entries below were found by the audit and predate the rule. Quotes are checked against the repository at the commit that added this file (historical quotes name their commit). Line numbers drift; the quote is the anchor.

Step numbers: step 19 is the third audit round's fixes (batch 2, 19d-19f, is done locally; batch 3, 19g-19h, is pending), step 20 is the guardrails step (CI ratchet, CI parity, an automated check of this register's quotes), step 21 is the no-cascade proof.

## Trigger ids

Each entry's **Trigger-id** is one of the ids below, in the order they fall due. **Current point:** `step-20`. `make check-register` fails if an OPEN or PARTLY CLOSED entry's id comes BEFORE the current point (its trigger has passed); `make check-register AT=<id>` (run when a tag or milestone closes) also fails for an id equal to or before `<id>`. The person closing a step or tag advances the current point in the same commit. `each-tag` is due at every tag: it counts as due at the next tag (`m2-ir-v0`) and again at each later one.

- `step-19` - the third audit round's fixes
- `step-20` - this guardrails step
- `step-21` - the no-cascade proof
- `m2-ir-v0` - the tag `m2-ir-v0`
- `each-tag` - every tag (checked as due at `m2-ir-v0`)
- `post-m2-ir-v0` - the fix-it / recovery pass after the tag
- `tag-after-m2-ir-v0` - the first tag after `m2-ir-v0`
- `loop-counter-injection` - before loop-counter injection starts
- `verifier-design` - before the verifier is designed
- `wasm-backend` - before the Wasm backend starts
- `m3-start` - M3 start
- `untrusted-code` - before any untrusted code runs
- `m3-drivers` - before M3 drivers start
- `v1` - v1

## 1. Kernel (M1, tag `m1-boot`)

### K1 - TSC-deadline timer mode is not implemented

- **Status:** OPEN
- **Reason:** M1 shipped LAPIC periodic mode only; detection and the switch-over were never written.
- **Promise:** `CLAUDE.md:23` "use TSC-deadline when supported"
- **Where it stands:** `CLAUDE.md:141` "TSC-deadline timer mode isn't implemented."
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** M3 start (M3 cannot start until this is closed)
- **Trigger-id:** `m3-start`

### K2 - Calibration cascade is 1 source of 3, and M1 was recorded as fully verified

- **Status:** OPEN
- **Reason:** CPUID 0x15/0x16 and HPET calibration are stubs that log and skip; only the PIT works. The milestone was closed on 'all acceptance criteria verified' anyway.
- **Promise:** `CLAUDE.md:59` "Sources, in order: **CPUID 0x15/0x16 → HPET → PIT**."
- **Where it stands:** `CLAUDE.md:139` "CPUID 0x15/0x16 calibration is a stub."; `CLAUDE.md:140` "HPET calibration is a stub."; `docs/STATUS.md @2794f66:24` "All Milestone 1 acceptance criteria verified"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** M3 start (M3 cannot start until this is closed). STATUS.md now reads 'complete except register items K1, K2'.
- **Trigger-id:** `m3-start`

### K3 - W^X is enforced on the kernel-image alias only

- **Status:** OPEN
- **Reason:** The same physical bytes stay reachable read/write through the 1 GiB identity and HHDM aliases; closing it needs the kernel's range split out of those pages.
- **Promise:** `docs/STATUS.md:26` "W^X+NX enforced and regression-tested (`make test-wx`)"
- **Where it stands:** `CLAUDE.md:142` "Kernel `.text` is writable via identity/HHDM aliases (1 GiB RW pages)."; `CLAUDE.md:142` "Revisit before untrusted code runs."
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Before any untrusted code runs
- **Trigger-id:** `untrusted-code`

### K4 - `make test-wx` is not run by CI

- **Status:** OPEN
- **Reason:** CI compiles the kernel and never boots it, so the W^X regression test runs only when someone runs it.
- **Promise:** `docs/STATUS.md:26` "W^X+NX enforced and regression-tested (`make test-wx`)"
- **Where it stands:** `VISION.md:97` "`make test-wx` is run by hand; CI builds the kernel (`make all`) but does not run it."
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (guardrails: a QEMU boot smoke test plus `make test-wx` in CI)
- **Trigger-id:** `step-20`

### K5 - No automated boot test

- **Status:** OPEN
- **Reason:** The CI job is a compile check; boots are manual.
- **Promise:** `CLAUDE.md:7` "Every milestone must boot in QEMU."
- **Where it stands:** `.github/workflows/ci.yml:62` "# Kernel compile check only (make all -> build/kernel.elf). Does not"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (a QEMU boot smoke test asserting the M1 serial-log lines)
- **Trigger-id:** `step-20`

### K6 - MMIO caching choice: not verified

- **Status:** OPEN
- **Reason:** The audit could read the docs only; whether the choice (firmware MTRRs, or PCD/PWT on MMIO pages) was made and written down is unknown.
- **Promise:** `CLAUDE.md:21` "Rely on firmware MTRRs for UC, or set PCD/PWT on MMIO pages. Document the choice."
- **Where it stands:** Unknown: the kernel source was not part of the audit packs; the code-level kernel review the audit says is still owed will read it.
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Before M3 start, as part of the code-level kernel review the audit says is still owed (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `m3-start`

### K7 - The kernel LOC budget is not checked

- **Status:** OPEN
- **Reason:** A goal, not a hard constraint, so nothing measures it.
- **Promise:** `CLAUDE.md:80` "**Kernel scope: single-person-comprehensible, ~120k LOC max.**"
- **Where it stands:** `CLAUDE.md:79` "aspirational, not enforced by any build check today."
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (a kernel LOC count in CI against the 120k budget)
- **Trigger-id:** `step-20`

### K8 - 64-bit PCI BAR window is unmapped

- **Status:** OPEN
- **Reason:** Deliberately excluded from the identity map (1024+ 1 GiB pages in one PDPT); a second PML4/PDPT entry is needed.
- **Promise:** `CLAUDE.md:16` "and any 64-bit PCI BARs."
- **Where it stands:** `CLAUDE.md:138` "required before real PCI/virtio driver work."
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** M3 start (M3 drivers cannot start until this is closed)
- **Trigger-id:** `m3-drivers`

## 2. Manna principles vs what was built

### P1 - Principle 6 'Errors teach': 47 of 55 codes have no fix-it

- **Status:** OPEN
- **Reason:** Principle 6 was never amended; the 'documented exception' entries grew instead. Several rest on a mechanism limit that no longer holds (E0245, E0265: 'needs source text the fix mechanism does not carry'; E0257: 'would need every use', and the use list exists since step 16). Measured on error_codes.md at 2794f66: 55 codes, 8 with a fix (E0102, E0107, E0108, E0109, E0215, E0261, E0280, E0281), 47 'None' (the audit counted 7 and 48). Fix-it policy C (CLAUDE.md, docs/error_codes.md): every diagnostic gets a fix or a short list of choices; the six safety limits below are the only exceptions. One entry per code follows in section 7.
- **Promise:** `docs/MANNA_PRINCIPLES.md:22` "Fix hints are machine-applicable edits"
- **Where it stands:** `docs/error_codes.md:78` "a reorder needs source text the fix mechanism does not carry"; `docs/error_codes.md:87` "a rename fix-it would need every use"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Post-m2-ir-v0 fix-it pass (after the tag)
- **Trigger-id:** `post-m2-ir-v0`

### P2 - `manna check --json` does not exist

- **Status:** OPEN
- **Reason:** Deferred in the plan with no trigger; the parser was tagged long ago and there is still no JSON output and no `manna` binary.
- **Promise:** `docs/MANNA_PRINCIPLES.md:45` "`manna check --json` returns structured errors and fix-it edits in milliseconds."
- **Where it stands:** `docs/M2_PLAN.md:816` "(implemented after the parser exists — not yet)"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** After the tag m2-ir-v0 and before the next compiler stage (loop-counter injection) starts
- **Trigger-id:** `loop-counter-injection`

### P3 - Principle 11: no canonical AST interchange

- **Status:** OPEN
- **Reason:** Output-only printer today; nothing reads the dump back and no ingestion path exists.
- **Promise:** `docs/MANNA_PRINCIPLES.md:31` "The AST has a stable serialized form."
- **Where it stands:** `docs/M2_PLAN.md:440` "**status: planned, not built.**"
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** Before the Wasm backend starts: the backend is the first consumer that needs a stable form (an ingestion path must go through the parser's validation and the typed IR, per TYPED_IR.md) (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `wasm-backend`

### P4 - Principle 20: no official example corpus

- **Status:** OPEN
- **Reason:** Never started; `git ls-files` lists no examples directory.
- **Promise:** `docs/MANNA_PRINCIPLES.md:53` "A few hundred small, correct, idiomatic programs, maintained alongside the spec."
- **Where it stands:** No examples directory exists (`git ls-files`).
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** The tag after m2-ir-v0 (the first tag at which `manna check` can be run on an example) (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `tag-after-m2-ir-v0`

### P5 - Principle 17: inner-node IDs deferred to v1

- **Status:** NOT A DEGRADATION
- **Reason:** Listed because the audit lists it: a locked, documented v0 decision (locked decision 5), not a stub made to make progress.
- **Promise:** `docs/MANNA_PRINCIPLES.md:47` "Addressable code structure."
- **Where it stands:** `docs/MANNA_PRINCIPLES.md:149` "Inner-node IDs are deferred to v1."
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** v1 (the decision's own boundary) (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `v1`

## 3. Compiler (M2): parser, lexer, typed IR

### C1 - The no-cascade proof was downgraded

- **Status:** OPEN
- **Reason:** The plan promised a no-cascade golden per poisoning path; what shipped is a script that checks each poisoning line is RUN by a one-diagnostic golden, and step 18e deleted the promise from TYPED_IR.md. The script also under-counts (re-audit 3, finding 9: 25 silent-consumption lines are not counted).
- **Promise:** `docs/history/TYPED_IR.md @6b3f537:140` "plus a no-cascade golden for each poisoning path."
- **Where it stands:** `tools/audit_ir_poison.py:133` "(this is not a no-cascade proof)"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 21 (the no-cascade proof)
- **Trigger-id:** `step-21`

### C2 - Parser 'Known limitations': recovery discards or cascades, none with a trigger

- **Status:** OPEN
- **Reason:** Five limitations recorded as accepted: the for-header recovery discards content, EOF drops the whole function, one attribute group resumes, E0108 reports the first pair only, a bracket cascade.
- **Promise:** `docs/MANNA_PRINCIPLES.md:21` "Every error gives line:col, what was expected, what was found, and a fix hint."
- **Where it stands:** `docs/M2_PLAN.md:500` "A `for`-header's error recovery discards, rather than preserves or re-reports, anything between a header failure and the next resume point"; `docs/M2_PLAN.md:501` "A block cut off by EOF still drops its function"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Post-m2-ir-v0 recovery pass, together with the fix-it pass (the parser is a tagged component: audit after changing) (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `post-m2-ir-v0`

### C3 - IR known issues: `--fixed` NUL truncation and the linear field lookup (the E0261 511-chain issue is fixed)

- **Status:** PARTLY CLOSED
- **Reason:** The 511-operation E0261 fix that produced E0111 was fixed in step 19 (19c-5, de84d89). Open: `ir_dump --fixed` truncates at a NUL byte; field lookup in an access is a linear scan (0.8 s to 1.7 s plain, 5 s to 10 s under ASan for 20,000 fields accessed 20,000 times).
- **Promise:** `docs/STATUS.md:22` "Revisit (per-struct hash) at the `m2-ir-v0` tag."
- **Where it stands:** `docs/STATUS.md:21` "`--fixed` is a test harness, not the compiler."; `docs/STATUS.md:22` "Field lookup in an access is a linear scan:"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** The m2-ir-v0 tag (STATUS already says so for the lookup; the NUL truncation is added to it) (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `m2-ir-v0`

### C4 - E0261: missing fixes presented as conservatism

- **Status:** OPEN
- **Reason:** When neither direction is a lossless widening (`u8` and `i8`) there is no fix, and the docs call that a feature. Policy C wants a fix or a list of choices (for example a signed or unsigned wrap to either type).
- **Promise:** `docs/MANNA_PRINCIPLES.md:22` "Fix hints are machine-applicable edits"
- **Where it stands:** `docs/error_codes.md:89` "If neither direction is lossless (`u8` and `i8`) there is **no fix**"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Post-m2-ir-v0 fix-it pass
- **Trigger-id:** `post-m2-ir-v0`

### C5 - Step 19 as first approved: three degradations

- **Status:** CLOSED (2026-10-24, step 19 batch 2)
- **Reason:** 19c-1 removed working fixes in un-annotated `let`; 19d withheld renames instead of completing the use list; choice 6 documented a quadratic scan. Revised 2026-10-23 and done: 19c-1 became 'the fix waits for the local's uses' (157534d), choice 6 became an index (90f048c), 19d renames the stale spellings (c460bff), and the 511-chain known issue was fixed (de84d89). The withheld first version of 19d sits on the local branch wip-19de, unmerged and not to be merged.
- **Promise:** `docs/M2_PLAN.md@f981a51:753` "the declaration and every resolved use"
- **Where it stands:** `docs/M2_PLAN.md:753` "renamed along rather than left stale"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 19 batch 2 (done); kept for the record until the m2-ir-v0 tag
- **Trigger-id:** `step-19`

### C10 - Renames are withheld while a declaration error or the E0203 cap stops the build

- **Status:** OPEN
- **Reason:** A declaration error (E0220 in a signature or a struct field, among others) stops the build before the bodies are checked, so the uses of every misnamed name are unknown and no rename is attached; the cap (E0203) unwinds the build the same way. The rename is offered on a second pass, once that error is fixed. Examples: a misnamed function used as a type in a parameter (`fn g(x: myFn)`), in a return type (`-> myFn`) and in a struct field (`x: myFn`) gets no rename, because the signature's E0220 stops the build. Approved by Bryden 2026-10-24.
- **Promise:** `docs/MANNA_PRINCIPLES.md:22` "Fix hints are machine-applicable edits"
- **Where it stands:** `docs/error_codes.md:14` "While an earlier declaration error or the E0203 cap stopped the build the uses are unknown"
- **Owner:** Claude Code (work); Bryden (approval, 2026-10-24)
- **Trigger:** The post-m2-ir-v0 fix-it pass (collect the uses of declarations in a pass that does not stop at a declaration error)
- **Trigger-id:** `post-m2-ir-v0`

### C6 - The audit repros are not run by CI

- **Status:** OPEN
- **Reason:** `tools/run_audit_repros.sh` exists but no make target or CI step runs it.
- **Promise:** `CLAUDE.md:174` "**Verify CI actually runs the tests you think it does, every time you add one.**"
- **Where it stands:** `tests/audit_repros/README.md:11` "These files are not part of `make test-*`"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (`make test-audit-repros` in CI)
- **Trigger-id:** `step-20`

### C7 - Generated-test counts are not asserted

- **Status:** CLOSED (2026-10-24, step 19 batch 3 and step 20a)
- **Reason:** `make test-e0261-exhaustive` and `make test-rename-property` printed their counts and asserted only 'no violation', so a generator that silently shrank passed. Done in 19g/19h (both compare their whole report with `tests/e0261_exhaustive.expected` and `tests/rename_property.expected`) and in 20a (`make test-ratchet` ratchets the numbers in those files, the golden, fix-case and repro counts and the memory-stress case count).
- **Promise:** `CLAUDE.md:160` "Rule: no behavior change without a test."
- **Where it stands:** `Makefile:319` "@diff -u tests/e0261_exhaustive.expected"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (every generated-test count is asserted in the Makefile/CI); done
- **Trigger-id:** `step-20`

### C8 - M2_PLAN: two error-policy sections still say 'pending review'

- **Status:** OPEN
- **Reason:** Steps 11 and 12 recorded their policy points as 'pending review'; whether the review happened is not recorded.
- **Promise:** `docs/M2_PLAN.md:693` "the points the rules above leave open, pending review"
- **Where it stands:** `docs/M2_PLAN.md:720` "the points the rules above leave open, pending review"
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** The m2-ir-v0 tag (the refinement loop's review step) (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `m2-ir-v0`

### C9 - E0261: two un-annotated lets whose fixes meet in one operator get no fix

- **Status:** OPEN
- **Reason:** `let r = a + b; let s = a + b; return r + s;` has two E0261 whose fixes would both be right (each local becomes u32, `r + s` agrees), but a binary operator with two different waiting fixes withdraws both (step 19c-4), so neither is offered. A missed fix, not a wrong one.
- **Promise:** `docs/MANNA_PRINCIPLES.md:22` "Fix hints are machine-applicable edits"
- **Where it stands:** `tests/ir_fix/fix_e0261_two_lets_met_in_one_operator_have_no_fix_yet.mn:5` "    return r + s;"
- **Owner:** Claude Code (work); Bryden (approval, 2026-10-24)
- **Trigger:** The post-m2-ir-v0 fix-it pass (a fix list may offer both fixes together, or one choice per mistake)
- **Trigger-id:** `post-m2-ir-v0`

## 4. Verification and CI

### V1 - Fuzzing is not continuous

- **Status:** OPEN
- **Reason:** The TCB rule says fuzz continuously; fuzzing is 10 minutes by hand per round and 'deliberately not in CI'.
- **Promise:** `CLAUDE.md:134` "Fuzz them continuously."
- **Where it stands:** `docs/M2_PLAN.md:786` "Deliberately not in CI - a fixed wall-clock budget, not a pass/fail check."
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (a short fuzz run on every push and a long scheduled one)
- **Trigger-id:** `step-20`

### V2 - No CI job runs LSan

- **Status:** OPEN
- **Reason:** Leak-clean is enforced only by the reviewer's Linux LSan run.
- **Promise:** `docs/REFINEMENT.md:80` "Test harnesses and measurement probes must be leak-clean like product code"
- **Where it stands:** `docs/REFINEMENT.md:81` "(no CI job runs LSan)"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (an Ubuntu gcc ASan+UBSan+LSan job)
- **Trigger-id:** `step-20`

### V3 - CI is macOS clang only

- **Status:** OPEN
- **Reason:** The C is meant to build elsewhere (an open IDEAS question: portable C on any OS); the only Linux/gcc runs are the reviewer's manual ones.
- **Promise:** `docs/IDEAS.md:19` "Mac-only vs portable C on any OS"
- **Where it stands:** `.github/workflows/ci.yml:22` "runs-on: macos-14"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (an Ubuntu gcc job)
- **Trigger-id:** `step-20`

### V4 - The da9f01c rule (no golden may lose diagnostics) is a review rule

- **Status:** CLOSED (2026-10-24, step 20a)
- **Reason:** CI compared output to the golden and could not see that the golden was edited to match. Now `make test-ratchet` compares the diagnostic count of every expected output under `tests/` with the baseline at the push's `before` commit, and every test count with its baseline; a decrease fails unless a `RATCHET-WAIVER` line names a register entry.
- **Promise:** `CLAUDE.md:176` "A golden that loses diagnostics or AST nodes in a commit is STOP-tier"
- **Where it stands:** `docs/REFINEMENT.md:75` "**Enforced since step 20a by `make test-ratchet` (`tools/ratchet.py`):**"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20 (the CI ratchet: counts that never decrease, diagnostics per golden); done in 20a
- **Trigger-id:** `step-20`

### V5 - The rc > 128 crash rule is not applied in `measure_expr_height.sh`

- **Status:** OPEN
- **Reason:** Stated in REFINEMENT, applied in `measure_ir_stack.sh` only.
- **Promise:** `docs/REFINEMENT.md:80` "a measurement script counts only a signal exit (rc > 128) as a crash"
- **Where it stands:** `docs/REFINEMENT.md:81` "`tools/measure_expr_height.sh` is not yet changed to follow it."
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Step 20
- **Trigger-id:** `step-20`

### V6 - Coverage justification at every tag is manual

- **Status:** OPEN
- **Reason:** `make coverage` is not in CI; the uncovered-line list is written by hand at each tag.
- **Promise:** `CLAUDE.md:161` "- **At every tag**, list uncovered lines in `lexer.c`, `parser.c` and the typed-IR files `ir*.c`"
- **Where it stands:** `make coverage` is a Makefile target that `.github/workflows/ci.yml` does not run.
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** The m2-ir-v0 tag: a coverage job in CI that fails on any uncovered line not on a committed justified list (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `m2-ir-v0`

### V7 - CI is one job 'to save minutes'

- **Status:** OPEN
- **Reason:** A cost decision shapes what CI covers; it is stated only as a comment in ci.yml.
- **Promise:** `.github/workflows/ci.yml:17` "# plus the kernel compile check, in one job - a separate job per the"
- **Where it stands:** `.github/workflows/ci.yml:19` "more than the few extra seconds of sharing a runner."
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** Step 20 (write the decision, and what it leaves out, into docs/STATUS.md while the CI parity jobs are added) (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `step-20`

## 5. Process

### R1 - The batch cap was not enforced (steps 17, 18, 19 were reviewed as 7-8 commit batches)

- **Status:** OPEN
- **Reason:** The cap says three steps, then stop; steps were bundled.
- **Promise:** `CLAUDE.md:157` "**A batch is at most 3 steps**"
- **Where it stands:** Enforced by review only until step 20i adds a check on the number of distinct step ids in the unpushed commits.
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** Enforced from 2026-10-23 (CLAUDE.md 'No degradation'); verified at each tag (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `each-tag`

### R2 - The refinement loop is due at m2-ir-v0

- **Status:** OPEN
- **Reason:** It ran at m2-parser-v0.1 and has not run for the typed IR stage.
- **Promise:** `CLAUDE.md:162` "At every tag, run the refinement loop in docs/REFINEMENT.md before starting the next stage."
- **Where it stands:** The last refinement-loop record is for the tag `m2-parser-v0.1` (`docs/REFINEMENT.md`).
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** The m2-ir-v0 tag (from the audit: 'due at m2-ir-v0')
- **Trigger-id:** `m2-ir-v0`

### R3 - Docs lag the repository

- **Status:** PARTLY CLOSED
- **Reason:** STATUS.md said step 18 was unpushed and not fuzzed (both done; brought up to date in the commit that created this register); VISION.md is last re-verified 2026-10-08.
- **Promise:** `docs/README.md:20` "re-read `docs/STATUS.md` against the actual repo state"
- **Where it stands:** `VISION.md:95` "Last re-verified 2026-10-08"
- **Owner:** Claude Code (work); Bryden (approval)
- **Trigger:** The m2-ir-v0 tag (consistency check re-reads VISION.md and the project-file snapshot) (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `m2-ir-v0`

### R4 - Two docs contradict each other about models in Deus

- **Status:** OPEN
- **Reason:** Open since 2026-10-15; IDEAS says Deus runs no models, VISION describes an off-box or local model behind an agent service (parked for M3).
- **Promise:** `docs/IDEAS.md:72` "No model-token budgets: Deus runs no models."
- **Where it stands:** `VISION.md:56` "The model never runs in the kernel; it is reached off-box (API) or, later, run as a local model."
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** Before M3 start (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `m3-start`

### R5 - Reviewer conduct: degradations approved or proposed

- **Status:** OPEN
- **Reason:** Three degradations approved in step 19 and two more proposed on 2026-10-23, all withdrawn; the rule below forbids offering them.
- **Promise:** `CLAUDE.md:37` "Neither Claude Code nor the reviewer offers"
- **Where it stands:** The rule is written (`CLAUDE.md` "No degradation"); nothing checks conduct, so each tag re-reads the step reports for an offered 'turn off', 'withhold' or 'accept'.
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** Closed by the CLAUDE.md rule of 2026-10-23; re-checked at each tag (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `each-tag`

## 6. Planned work: degradation risks ahead

### A1 - The native backend (the kernel's real compile target) has no milestone

- **Status:** OPEN
- **Reason:** LBS for kernel code depends on it plus the verifier; the plan says it is not scoped.
- **Promise:** `CLAUDE.md:45` "**Kernel code** compiles to native machine code."
- **Where it stands:** `docs/M2_PLAN.md:103` "handles these - not scoped here."
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** Before verifier design: the native backend gets a milestone
- **Trigger-id:** `verifier-design`

### A2 - The 'small trusted checker, untrusted compiler' decision is open

- **Status:** OPEN
- **Reason:** If the verifier is designed without it the TCB stays the whole compiler by default.
- **Promise:** `docs/IDEAS.md:253` "Earliest milestone boundary: before the verifier is designed (M2)."
- **Where it stands:** `CLAUDE.md:134` "- **TCB:** the compiler, verifier, and Wasm runtime all run in ring 0."
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** Before verifier design
- **Trigger-id:** `verifier-design`

### A3 - WAMR is third-party C in ring 0 with no fuzzing or validation plan

- **Status:** OPEN
- **Reason:** Named for M3; nothing says how it is validated before adoption.
- **Promise:** `CLAUDE.md:146` "- M3: bare-metal Wasm runtime (WAMR) and zero-copy IPC queues."
- **Where it stands:** `CLAUDE.md:134` "One bug compromises everything. Fuzz them continuously."
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** M3 start: a fuzzing and validation plan for the runtime is written before WAMR is adopted (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `m3-start`

### A4 - The verifier's bounds-check elimination has no measurement plan

- **Status:** OPEN
- **Reason:** The 5-20% overhead is a budget; without a plan 'eliminate as many as it can' can drift to none.
- **Promise:** `docs/M2_PLAN.md:85` "The verifier's job is to eliminate as many of"
- **Where it stands:** `CLAUDE.md:44` "Budget a 5–20% bounds-check overhead without guard pages."
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** Verifier design: the measurement plan (which programs, which counts, what threshold) is part of the design document (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `verifier-design`

### A5 - Spectre mitigation has no plan and a soft trigger

- **Status:** OPEN
- **Reason:** 'Before networking' is not a milestone; both candidate mitigations are unvalidated.
- **Promise:** `CLAUDE.md:133` "A mitigation plan is required before networking."
- **Where it stands:** `CLAUDE.md:133` "Neither is implemented or proven sufficient; both need real measurement before being trusted."
- **Owner:** Bryden (decision); Claude Code (work)
- **Trigger:** M3 start: the plan is written and the networking milestone is blocked on it (PROPOSED by Claude Code, approved by Bryden 2026-10-24)
- **Trigger-id:** `m3-start`

## 7. Fix-it exceptions (principle 6 / policy C)

Policy C (`CLAUDE.md` "No degradation", top of `docs/error_codes.md`): every diagnostic gets a machine-applicable fix or a short list of choices; it never guesses; every offered fix re-checks clean. Entry **P1** is the umbrella. The table lists every code whose fix-it column still reads "None"; each stays as written for now.

### 7a. Approved exceptions: the six safety limits

**Trigger-id:** `permanent`

Bryden's policy C names these six as the only exceptions: each limit exists to stop work, so a fix would have nothing to apply. They are not degradations; they are listed so the exception list is closed. **Trigger:** none (permanent), unless Bryden amends policy C.

| Code | Location | Quote |
|---|---|---|
| E0104 | `docs/error_codes.md:34` | "None - documented exception: fix the earlier errors first; the cap is a cascade" |
| E0110 | `docs/error_codes.md:40` | "None - documented exception: the only remedy is a smaller input (split the file" |
| E0111 | `docs/error_codes.md:41` | "None - documented exception: the input has to be split into several statements" |
| E0201 | `docs/error_codes.md:51` | "None - documented exception: the only remedy is a smaller input (split the" |
| E0203 | `docs/error_codes.md:52` | "None - documented exception: fix the earlier errors first; the cap is a cascade" |
| E0222 | `docs/error_codes.md:61` | "None - documented exception: the type has to be smaller, which is not an edit" |

### 7b. Fix-it exceptions to close (41 codes)

**Trigger-id:** `post-m2-ir-v0`

Each is a register entry (reason: the exception's own text; promise: `docs/MANNA_PRINCIPLES.md` principle 6) with **Owner:** Claude Code (work), Bryden (approval) and **Trigger:** the post-m2-ir-v0 fix-it pass (after the tag). Approval of the exceptions as they stand: pending.

| Code | Location | The exception, quoted |
|---|---|---|
| E0101 | `docs/error_codes.md:31` | "None - documented exception. The natural rewrite, `a < b && b < c`, evaluates" |
| E0103 | `docs/error_codes.md:33` | "None - documented exception: the input has to be restructured, there is no" |
| E0105 | `docs/error_codes.md:35` | "None by default - documented exception: a generic expected/found has no single" |
| E0106 | `docs/error_codes.md:36` | "None - documented exception: rewriting the version to `v0` would assert the" |
| E0210 | `docs/error_codes.md:53` | "None - documented exception: which declaration to rename or delete is the" |
| E0211 | `docs/error_codes.md:54` | "None - documented exception: renaming needs the author's intent." |
| E0212 | `docs/error_codes.md:55` | "None - documented exception: renaming needs the author's intent." |
| E0213 | `docs/error_codes.md:56` | "None - documented exception: renaming needs the author's intent." |
| E0214 | `docs/error_codes.md:57` | "None - documented exception: which attribute was meant is the author's call." |
| E0220 | `docs/error_codes.md:59` | "None - documented exception: the intended type is unknowable from the text." |
| E0221 | `docs/error_codes.md:60` | "None - documented exception: breaking a cycle means choosing which field to" |
| E0223 | `docs/error_codes.md:62` | "None - documented exception: which side to change is the author's call." |
| E0224 | `docs/error_codes.md:63` | "None - documented exception: which value was meant is the author's call." |
| E0225 | `docs/error_codes.md:64` | "None - documented exception: the intended type is the author's call (the" |
| E0226 | `docs/error_codes.md:65` | "None - documented exception: what to bind or pass instead is the author's call." |
| E0227 | `docs/error_codes.md:66` | "None - documented exception: the field has to be redesigned (e.g. an array, or" |
| E0230 | `docs/error_codes.md:67` | "None - documented exception: which name was meant is the author's call." |
| E0231 | `docs/error_codes.md:68` | "None - documented exception: functions are called, not passed (v0 has no" |
| E0232 | `docs/error_codes.md:69` | "None - documented exception: which function was meant is the author's call." |
| E0233 | `docs/error_codes.md:70` | "None - documented exception: which argument to add or drop is the author's" |
| E0234 | `docs/error_codes.md:71` | "None - documented exception: dropping the names is the author's call, and they" |
| E0235 | `docs/error_codes.md:72` | "None - documented exception: breaking a cycle is a redesign." |
| E0240 | `docs/error_codes.md:73` | "None - documented exception: which field each value was for is the author's" |
| E0241 | `docs/error_codes.md:74` | "None - documented exception: the author's call." |
| E0242 | `docs/error_codes.md:75` | "None - documented exception: a rename-to-the-nearest-field hint needs a" |
| E0243 | `docs/error_codes.md:76` | "None - documented exception: the value to give is the author's." |
| E0244 | `docs/error_codes.md:77` | "None - documented exception: which of the two values was meant is the author's" |
| E0245 | `docs/error_codes.md:78` | "None - documented exception: a reorder needs source text the fix mechanism does" |
| E0251 | `docs/error_codes.md:79` | "None - documented exception: the author's call." |
| E0252 | `docs/error_codes.md:82` | "None - documented exception: the author's call." |
| E0253 | `docs/error_codes.md:83` | "None - documented exception: the author's call." |
| E0254 | `docs/error_codes.md:84` | "None - documented exception: which argument was meant is the author's call." |
| E0255 | `docs/error_codes.md:85` | "None - documented exception: the author's call." |
| E0256 | `docs/error_codes.md:86` | "None - documented exception: the call to write needs its argument." |
| E0257 | `docs/error_codes.md:87` | "None - documented exception: the new name is the author's call (a rename fix-it" |
| E0260 | `docs/error_codes.md:88` | "None - documented exception: which operator or type was meant is the author's" |
| E0265 | `docs/error_codes.md:90` | "None - documented exception: `x != 0` needs source text the fix mechanism does" |
| E0270 | `docs/error_codes.md:91` | "None - documented exception: what to assign to is the author's call." |
| E0275 | `docs/error_codes.md:92` | "None - documented exception: the loop (or a different statement) is the" |
| E0276 | `docs/error_codes.md:93` | "None - documented exception: whether to add a value or drop it is the author's" |
| E0277 | `docs/error_codes.md:94` | "None - documented exception: where the return belongs is the author's call." |

Promise, for every row: `docs/MANNA_PRINCIPLES.md:22` "Fix hints are machine-applicable edits".
