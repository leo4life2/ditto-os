# Build and evidence retention

The September 22 cleanup removed duplicated experimental builds and raw animation
dumps, preserving source snapshots, reports, compact visual evidence, the active
build/toolchain, and verified rollback firmware. The audit and exact deletion
manifest are in `../../../outputs/storage-cleanup-2026-09-22/`.

## Working limits

| Area | Limit / policy |
| --- | --- |
| Current build | One reusable `build/`; never duplicate it into test evidence |
| Experimental builds | At most two while actively comparing; remove after recording results |
| Saved test evidence | 1.25 GiB total; preserve current review and useful historical measurements |
| Render scratch | 512 MiB total reservation; owned directories under `.scratch/` |
| One visual capture directory | 64 MiB; overwrite reruns, retain selected PNGs and compressed clips |
| Release checkpoints | Current plus one useful verified rollback; historic small packages may remain when referenced |

Run `../../venv/bin/python tools/storage_hygiene.py --check` before declaring a
task complete. Preview scripts and checkpoint packaging also check evidence
budgets. A failed check asks for review; it does not automatically delete evidence.
Reservations constrain concurrent known render workloads, not the operating
system's filesystem quota. Avoid arbitrary-size writes inside a reservation.

## Temporary data

Use `from storage_hygiene import scratch_directory` in host tools. Its context
manager cleans raw frames and host binaries on success and ordinary exceptions.
Emote review now encodes one 150-frame clip at a time and joins compressed MP4s
without re-encoding; it no longer builds a 2,400-frame raw concatenation. The
rendered PNGs and individual clips retain the same quality settings.

An uncatchable process kill can leave scratch behind. Inspect with:

```
../../venv/bin/python tools/storage_hygiene.py --prune-stale
../../venv/bin/python tools/storage_hygiene.py --prune-stale --apply
```

Only this project's marked directories older than 24 hours, with a dead owning
process and no open files, qualify. Unmarked paths, symlinks and live jobs are
preserved. Nothing scans or deletes global temporary directories. This is an
explicit command, not a background cleanup job.

For permanent evidence, retain the test inputs/source identity, results, hashes,
representative lossless PNGs and a short MP4. Remove raw RGB565/body dumps after
comparisons finish; unresolved rendering defects may justify a bounded raw
reproducer. Keep unique source snapshots and meaningful failed-test reports.
Retire superseded diagnostic binaries, `.o`/`.a` trees and installer archives
only after confirming that they are unused. All installed ESP-IDF tools remain.

Do not truncate an active bridge/preview log. Use bounded logs when restarting
services for normal development; inspect any unusually large capture before
keeping it. No firmware or display behavior changes are part of this cleanup.
