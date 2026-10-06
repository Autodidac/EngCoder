# EngCoder 0.9.2 task-first workbench release report

Date: 2026-10-02

Branch: `codex/threadspire-unified-workbench`

Result: **passed native validation; immutable commit/tag/package/Site receipts are recorded externally after source freeze**

This report contains only evidence measured from the frozen 0.9.2 source and its clean native builds. Exact commit, annotated-tag object, source-tree, archive hashes, GitHub status, and Site deployment receipts are recorded in the external immutable release receipt after the source archive exists; embedding those values here would make the source record self-referential.

## Source-confirmed changes

- The native workbench is organized around a compact task/routing header, dominant conversation, reply-local review controls, composer, run-details rail, and persistent global activity card.
- Important activity text wraps, active work remains visible, and completed work is retained rather than being replaced immediately by a generic ready state.
- The release-bundled compact native answer host runs in place. It is a zero-neural-weight base component and is not copied into application data or silently recorded as an installed optional package on first use.
- Optional capabilities are explicit. Missing Internal PyTorch Training requirements preserve queued evidence and identify the missing files plus the **Install selected package** action; startup, Chat, Live Auto, and foundation work do not prepare or install an optional package implicitly.
- An exact locally installed Qwen or Nemotron teacher may be loaded on demand from an authorized loopback LM Studio server with a bounded role-specific context request. EngCoder does not download a teacher or unload or reconfigure an already-loaded model.
- Derived teacher attempts have stable source/assignment slots. Accepted terminal slots are written only after durable episode storage or a confirmed duplicate; transient review, cancellation, malformed output, audit, or storage failures remain retry-pending at the bounded batch-start cursor.
- The English foundation request remains distinct from later scoped training work, and accepted evidence is compiled into a bounded minimal curriculum rather than replaying the full append-only audit history.
- Foreground Chat has exactly one answer owner. A second AI is contacted only after a real failure when the user selected automatic fallback; exact/manual selections never fall through. A pre-dispatch unavailable-model failure remains a durable failed reply with the original files/context and a working Retry action, and it cannot become training data.
- A successful promotion rechecks preserved evidence and schedules one follow-up job when another complete microbatch accumulated during training. A rejected candidate never retries itself automatically. Independently rejected Qwen architect material remains audit evidence and cannot seed Nemotron variants or positive training rows.
- Positive coding evidence requires successful compiler and test receipts. Plausible prose or teacher agreement alone cannot make unexecuted code eligible for neural-weight training.

## Final native validation

- Fresh CMake configure: **passed** with Visual Studio 2022 / MSVC 19.44.35227 and Windows SDK 10.0.26100.0.
- Clean Debug build: **passed**.
- Debug CTest: **16/16 passed**.
- Clean Release build: **passed**.
- Release CTest: **16/16 passed**.
- Python AST checks: **4/4 passed** for the changed model/server smoke sources.
- Internal tensor/model smoke: **passed** with the already-present trainer-pack interpreter; nothing was installed.
- Active and isolated shadow native-server smoke: **passed** against the Release native host.
- `git diff --check`: **passed**; Git emitted only the repository's expected `core.autocrlf` conversion notices.
- PowerShell parser validation for the changed reset script: **passed**.
- Release executable file/product version: **0.9.2 / 0.9.2**.

## Binary and footprint evidence

| Binary | Configuration | Bytes | SHA-256 |
|---|---:|---:|---|
| `EngCoder.exe` | Debug | 12,636,672 | `e017641eecff424931956646dbbbd9a37f0e789a1d4604fb8e0d69822c2d5b51` |
| `engcoder-native-host.exe` | Debug | 3,397,120 | `1895bbeaaaddabc59afde369fec420a2f64e6eb7cd32c5f707a98a04497201d2` |
| `EngCoder.exe` | Release | 6,039,552 | `1ea20d756d9188a78b2e9571cfcb5e11a5459d596c1bb72c59e8af1d88462f9a` |
| `engcoder-native-host.exe` | Release | 543,232 | `ccb5dc37f88df69d819053a69ef391fb76057da6afb0a90558006e030905c800` |

- Clean installed-base file count: **5**.
- Clean installed-base bytes: **6,586,346 (6.28 MiB)**.
- 100 MiB base-footprint gate: **passed**.
- Base neural-weight payload: **0 B**.
- Optional trainer or other heavyweight pack bundled: **no**.

## Runtime and interface evidence

- Active and isolated-shadow host readiness, tensor-backed training/checkpoint/resume, candidate rejection/rollback, and atomic promotion were exercised in temporary data roots.
- Deterministic native tests cover single-model foreground routing, loaded/unloaded/missing/failed/cancelled teachers, manual no-fallback, durable failed replies, exact Retry, responsive layout contracts, activity retention, and the no-silent-install boundary. Shared real LM Studio models were deliberately not reconfigured or unloaded during validation.
- One Release workbench instance started normally from the build tree. After five seconds it used **41,127,936 working-set bytes**, **19,595,264 private bytes**, and **9 threads**.
- Fresh screenshot-level visual QA was unavailable because this task had no native-app computer-control surface. No screenshot or visual-quality claim is made.

## Package and publication evidence

- The runtime archive must use the established versioned portable root and include an internal `checksums.txt` for every packaged file except itself.
- The source archive must be produced strictly from the final annotated tag with one versioned root; root `checksums.sha256` covers every tagged source file except itself.
- The release supplies a combined archive checksum file plus one normalized `.sha256` sidecar per archive. Independent extraction and checksum verification are release gates.
- Exact commit/tree/tag objects, package file lists, archive/sidecar bytes and SHA-256 values, cleanup, GitHub disposition, Site version/deployment, and live-edge checks are recorded in the external release receipts after this report is frozen.

## Boundaries

- Epoch and its worktree are outside this release pass. EngCoder continues to use only its existing vendored EpochGui boundary.
- Application data, user memory, learning history, connected-model weights, candidates, checkpoints, caches, and optional trainer dependencies are not source or portable-release payloads.
- Qwen and Nemotron remain evidence generators/reviewers; their weights are never copied into EngCoder.
- A native-foundation answer is not proof that trained neural weights exist.

## Related documentation

- [Start here](../start-here.md)
- [Architecture](../architecture.md)
- [Building](../building.md)
- [Live Auto Training](live-auto-training.md)
- [Autonomous curriculum and training](autonomous-curriculum-and-training.md)
- [Minimal curriculum compiler](minimal-curriculum-compiler.md)
