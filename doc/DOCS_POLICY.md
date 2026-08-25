# Documentation policy (#152)

## Authoritative surface

- `docs/refman/PROGRAMMERS_GUIDE.md` and the auto-generated pages under
  `docs/refman/` (per-module `<module>.md` plus `index.md`, `interfaces.md`,
  `functions.md`, `types.md`, `aspects.md`, `runtime.md`) are **authoritative**.
- Everything under `doc/` (design notes, proposals, dated reviews) is
  **historical / design-in-progress**: it may label shipped features as planned
  or stubbed and can lag reality. To confirm current behaviour, trust the guide +
  refman cross-checked against `build/vyb`, not a design doc.

## Checklist when you land a feature change

Run this before closing an issue that touches language, stdlib, or compiler
behaviour:

1. **Refman** — if the change is user-facing (new syntax, stdlib symbol,
   intrinsic, changed error shape), regenerate the refman
   (`tools/refman.py`) and update any guide section that describes the feature.
2. **Contradictions** — grep the tree for stale wording about the feature
   (`stub`, `placeholder`, `not implemented`, `planned`, `not yet`) and delete or
   replace it. Do not leave a "stub" claim in one doc for something that now
   ships in another.
3. **Status docs** — update the feature's row in `doc/FEATURE_STATUS.md` and the
   corresponding item in `TODO.md`.
4. **Design docs** — if a `doc/*` design page describes the feature as
   planned/not-implemented and it now ships, mark that page `ARCHIVED` or update
   its Status line.
5. **Counts** — re-run the suite before citing any test count; doc-stated counts
   lag reality.
