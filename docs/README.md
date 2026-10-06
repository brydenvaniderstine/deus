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

Before tagging, re-read `docs/STATUS.md` against the actual repo state (`git log`, `git tag -l`, what's actually implemented) and `docs/README.md`'s own table against what files actually exist, and report any contradiction found - a doc claiming something that isn't true anymore, two docs disagreeing with each other, or a doc listed here that no longer exists (or vice versa).
