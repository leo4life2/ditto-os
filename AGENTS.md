# Scout storage hygiene

Follow STORAGE-HYGIENE.md for build and test artifacts. This project shares its
task directory with unrelated keyboard work; do not clean sibling projects.

- Reuse this project's `build/`. Never copy a populated build directory into
  test evidence. For compiler experiments, keep at most two temporary build
  trees; after comparison retain their source/configuration and compact results,
  then remove the experiment builds. Preserve the current verified build.
- Use `tools/storage_hygiene.py`'s `scratch_directory` for raw render outputs and
  ad hoc host executables. Do not leave named raw dumps in `/tmp` or `/private/tmp`.
  Keep evidence as PNG, MP4, CSV/JSON metrics, small reports and source patches.
- Before finishing, run `../../venv/bin/python tools/storage_hygiene.py --check`.
  Test evidence is capped at 1.25 GiB; render scratch at 512 MiB; each visual
  capture directory at 64 MiB. Inspect and prune regenerable data before raising
  limits. Never discard unique source, active logs, required assets, or firmware
  being used for a current test to satisfy a cap.
- Update the existing preview directory on reruns. Keep current review media and
  one useful prior comparison; delete unneeded intermediate capture variants.
  Hash comparisons and compact reports normally replace full raw dumps once
  checked. Keep a minimal lossless reproducer only for an unresolved issue.
- Keep the current sealed firmware/source checkpoint and a useful verified
  rollback. Prune superseded checkpoint copies after verifying their replacement.
  Never create recurring broad deletion of `/private/tmp` or another task's data.
