# Repository guidance

## Scope and verification

- Keep changes focused and preserve unrelated working-tree edits.
- Use existing tests where they cover the changed behavior. Documentation-only edits need link and whitespace checks, not firmware builds.
- Firmware build success does not establish device behavior. Report host tests, firmware builds and hardware verification separately.
- Flashing, erasing device storage and publishing releases require explicit authorization.
- Keep credentials, account data, machine-specific paths and raw device logs out of commits.

## ESP-IDF

- Check `eim --version` and `eim list` before building. Use the target registry's SDK version explicitly; maintained targets currently use v6.1.
- Run from the repository root: `eim run "idf.py -C firmware/targets/<target> build" v6.1`.
- Reuse incremental builds for normal checks. Regenerate dependencies only when their inputs change or a dependency-integrity issue requires it.
- Check command exit codes and generated configuration. If EIM changes the selected SDK unexpectedly, restore the original selection.
- Managed dependencies are generated. Keep local vendor changes documented in the corresponding `UPSTREAM.md` and preserve license notices.

## Code Comment Policy

Comments explain the current implementation to maintainers without conversation context. Prefer clear names, types, constants and control flow; retain useful comments without unnecessary refactoring.

- Explain non-obvious intent, invariants, hardware or third-party constraints, timing, units, side effects, ownership, lifetime and concurrency requirements. Do not restate the code.
- Use concise, objective technical language and established terminology. State the underlying constraint rather than a conversational warning: `// The controller requires at least 120 µs after reset before the first SPI transaction.`
- Keep implementation history, rejected approaches, user instructions and discussion in Git, issues or ADRs. Comments are not changelogs or scratchpads.
- Delete obsolete code, including commented-out code. A temporarily disabled operation requires a TODO naming the concrete blocking condition or remaining action.
- Document public API behavior, parameters, return values, errors, valid ranges, ownership and usage requirements where callers need them. Use Doxygen tags for C/C++ APIs when appropriate.
- When changing code, review nearby comments and remove or update obsolete, redundant, conversational or historical material. Every added or modified comment must be accurate, necessary and understandable independently of the task.

## Windows editing

Prefer direct PowerShell or Python text edits for small changes. Verify the resulting diff and `git -c core.whitespace=cr-at-eol diff --check`.
