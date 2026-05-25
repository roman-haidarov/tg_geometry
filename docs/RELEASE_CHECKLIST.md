# First public release checklist

This checklist is intentionally conservative. Do not publish until every required item is satisfied.

## Required state before release

- [ ] All release-core specs pass locally.
- [ ] GC.stress specs pass.
- [ ] GC.compact specs pass.
- [ ] ASAN status is documented.
- [ ] Valgrind status is documented.
- [ ] Benchmark scripts exist and have been run for the full scenario matrix.
- [ ] README and docs are complete.
- [ ] Gem metadata is final:
  - gem name: `tg_geometry`;
  - author: `Roman Haydarov`;
  - email: `romnhajdarov@gmail.com`;
  - repository: `https://github.com/roman-haidarov/tg_geometry`.
- [ ] Vendored TG and rtree versions are pinned in `VERSION` files.
- [ ] Upstream license files are included under `ext/tg_geometry/vendor/*/LICENSE`.
- [ ] `CHANGELOG.md` exists.

## Do not release if any of these are true

- [ ] Any OPEN QUESTION affects correctness.
- [ ] Any memory accounting path is approximate.
- [ ] Any failed build path waits for GC instead of immediate dispose.
- [ ] `strategy: :auto` is enabled without benchmark-derived threshold.
- [ ] Public API differs from the contract without Roman approval.
- [ ] Ractor support is claimed.
- [ ] Performance claims are present without project-owned benchmark output.

## OPEN QUESTION: ASAN setup

ASAN setup is not fully specified in the contract and the worker must not research it independently. The repository contains a placeholder CI job that fails with an explicit message until Roman approves the exact ASAN approach.

## OPEN QUESTION: Valgrind setup

Valgrind setup can vary by CI image and Ruby build. The repository contains a placeholder CI job that installs/runs nothing by default and asks for approval before adding a final Valgrind configuration.
