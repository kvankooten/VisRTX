# Agent rules for VisRTX

Guidance for AI coding agents working in this repo. Local-machine-specific
paths and setup notes live in `AGENTS.local.md` (git-ignored, see
`.gitignore`).

## Code style

- Do **not** introduce trailing whitespace.
- Match the existing line endings of the file you are editing; do not
  convert a file from one style to another as a side effect of an edit.
- Scope braces `{` and `}` go on **their own lines** (Allman style),
  unless an existing file uses the other style throughout -- in that
  case match the file.
- Match indentation, naming, and include ordering of the surrounding
  code rather than imposing a global preference.

## Comments

- Keep comments **lean**. Explain non-obvious intent, trade-offs, or
  constraints that the code can't convey on its own.
- Do **not** add narration-style comments that restate what the code is
  doing (`// increment counter`, `// import module`, `// return result`,
  etc.).
- Do **not** put hardcoded values into comments (`// e.g. starts at
  (0,0,5)`) -- those drift the moment the code changes.
- In **library** code, do **not** reference dependent libraries, specific
  consumers, or use-case-specific terminology (`// used by SrtxViewport
  for fly-through navigation`). Keep library comments to the library's
  own generic context. Use-case-level commentary belongs in the
  consumer, not the dependency.

## Cross-repo work

VisRTX is the consumer of two separate libraries that live in sibling
repos (see `AGENTS.local.md` for the absolute paths on the current
machine):

- `srtx-render-library` -- generic gRPC render client. Builds against
  C++14; do **not** rely on C++17-only standard-library features
  (`<filesystem>`, `std::string_view` overloads, etc.) when editing
  this library. Use simpler portable alternatives or simply don't add
  the feature there.
- `AnariSrtxDevice` (under `usddevice-build-pipeline`) -- ANARI device
  that wraps `srtx-render-library` and translates ANARI parameters.

When a change spans repos, decide deliberately where each piece of code
and each comment belongs: generic plumbing in the library, app-specific
logic and naming in VisRTX.

## Matrix layouts

- ANARI / OpenGL use **column-major** matrices (translation in the last
  column of `mat4`).
- USD and the SRTX gRPC `omni:fabric:worldMatrix` are **row-major**
  (translation in the last row).
- When converting between them: **do element-wise copies with explicit
  index mapping**, do **not** add a blanket transpose -- the two
  conventions also disagree on storage order, so a transpose-and-copy
  often cancels out in unintended ways.

## Git

- Never run `git stash` casually as part of an investigation: it
  collapses the user's index / working-tree split, and `stash pop`
  restores everything as unstaged. If a pre-edit baseline is needed:
  - prefer a separate worktree (`git worktree add ...`), or
  - use `git stash push --keep-index` to preserve the staged state.
- Never create commits, push, force-push, or amend without an explicit
  user request.
- Do not modify `git config`.

## Build & profiling

- VisRTX is built out-of-tree under a `_build*` directory (git-ignored
  by the project's `_*/` rule).
- The `tsdSrtxViewer` app surfaces an in-app FPS / wait-time overlay and
  optional **stream-timing capture** (CSV). The CSV format is
  self-describing via a `#`-prefixed metadata header block followed by
  the column header; use it for client-side perf analysis.
- Platform-specific profiling tools and known issues live in
  `AGENTS.local.md`.

## Working style

- Use a TODO list for any task with 3+ distinct steps.
- Don't proactively run "verification" tests or commits the user didn't
  ask for.
- When picking up gRPC schema questions, the v1alpha3 protos are what
  `srtx-render-library` currently uses; v1alpha4 reorganizes the render
  surface into a `USDSensorService` (see `AGENTS.local.md` for the local
  path to the proto source).
