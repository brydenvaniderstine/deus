# Docs index

One owner file per topic. Everything else **links** to the owner, never duplicates its content — if the same fact needs restating, that's a sign it belongs in the owner file and everywhere else should reference it instead.

| File | Owner of |
|---|---|
| [`CLAUDE.md`](../CLAUDE.md) | Hard constraints, goals, and the day-to-day working spec for Deus itself. The source of truth for what's actually being built. |
| [`VISION.md`](../VISION.md) | Longer-range cross-platform/cloud-guest goals, tracked (not committed) R&D candidates, and the "better than Linux" scorecard. |
| [`docs/M2_PLAN.md`](M2_PLAN.md) | The Milestone 2 blueprint: the Manna compiler's design - language spec, pipeline stages, grammar, test strategy, v1 roadmap, language design decisions. |
| [`docs/MANNA_PRINCIPLES.md`](MANNA_PRINCIPLES.md) | The Manna language charter: the 20 core/AI-native principles, lessons-learned tables, and the Feature Admission Test every language change must pass. |
| [`docs/error_codes.md`](error_codes.md) | The stable error-code registry (`E0xxx`) for `manna check --json`. |
| [`docs/STATUS.md`](STATUS.md) | Current milestone, what's done, what's next, and pointers to the open risks tracked elsewhere. Updated at every tag. |
| [`docs/IDEAS.md`](IDEAS.md) | Dated parking lot for new ideas that haven't been triaged into a milestone yet. |
| [`docs/history/`](history/) | Superseded documents, kept for the record. **Never a source of truth** - if something in `docs/history/` and a current owner file disagree, the owner file wins, always. |

## Consistency check (run at every tag)

Before tagging, re-read `docs/STATUS.md` against the actual repo state (`git log`, `git tag -l`, what's actually implemented) and `docs/README.md`'s own table against what files actually exist, and report any contradiction found - a doc claiming something that isn't true anymore, two docs disagreeing with each other, or a doc listed here that no longer exists (or vice versa). While `docs/TYPED_IR.md` exists, also check `docs/M2_PLAN.md`'s "Typed IR (stage 3)" subsection against it and against `tools/mannac/ir*.c`: every rule marked "specified, not implemented" must flip when its step lands, every `E02xx` in the working file's table must match `docs/error_codes.md` for the codes already emitted, and each §8 record #7-#15 must say whether its goldens exist.

## Fuzzing (`make fuzz`)

`make fuzz` (or `fuzz-lexer`/`fuzz-parser` individually) runs the libFuzzer harnesses described in `docs/M2_PLAN.md`'s "Test strategy" (Step 12). Needs brew's keg-only llvm (`brew install llvm`) - Apple's Xcode clang ships no libFuzzer runtime at all. Not part of `make test-*`/CI: it runs for a fixed wall-clock budget, not to a pass/fail verdict.

```
make fuzz                                    # both targets, default budgets (180s / 600s)
make fuzz-lexer DURATION_LEXER=600           # one target, custom budget (seconds)
make fuzz-parser DURATION_PARSER=1800
LLVM_CLANG=/path/to/clang make fuzz          # if brew's llvm isn't at /usr/local/opt/llvm
```

Each target's corpus lives at `tools/fuzz/corpus/{lexer,parser}/` - committed, pre-minimized, and the directory libFuzzer both reads from and writes newly-discovered inputs into (`tests/lexer`/`tests/parser` are passed as an additional read-only seed on every run, so new goldens are always re-offered). Periodically re-minimize it after it's grown from real runs: `./build/fuzz/fuzz_lexer -merge=1 <fresh-empty-dir> tools/fuzz/corpus/lexer` (same pattern for `fuzz_parser`), then replace the committed directory with the merge output. Every crash or timeout found becomes a golden test (minimized, then copied into `tests/lexer`/`tests/parser` with a name describing what broke) - never left as just a file under `build/fuzz`.

Commit corpus changes only as a re-minimized merge at milestone boundaries; never commit a grown corpus mid-milestone.
