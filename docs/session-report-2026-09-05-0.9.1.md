# EngCoder 0.9.1 minimal-curriculum validation report

Date: 2026-09-05

Branch: `codex/threadspire-unified-workbench`

Result: release validation passed

The exact implementation commit, annotated-tag object, archive sizes, archive hashes, and Site deployment receipt are recorded outside this source tree after the immutable release artifacts are created. Embedding those values here would make the source archive hash self-referential.

## Outcome

EngCoder no longer treats its full append-only evidence history as a neural-training dataset. Every internal weight job now derives a compact, immutable candidate snapshot:

```text
append-only evidence
  -> safety and scope admission
  -> exact and canonical behavior clustering
  -> repeated answer-target cap
  -> authoritative representatives
  -> fresh delta plus bounded replay
  -> isolated candidate training and held-out evaluation
  -> atomic promotion only after every gate passes
```

The evidence ledger remains broad enough to audit. Rejected, superseded, retry-pending, and privacy-excluded records remain visible but cannot become positive gradients. The compiler never rewrites the source ledgers.

## Minimal curriculum measurement

The shipped 4,247-record corpus and 320 held-out records were compiled twice with different `PYTHONHASHSEED` values. Both selected JSONL files and both manifests were byte-identical.

- Source rows parsed: 4,247 of 4,247
- Eligible records before deduplication: 3,118
- Canonical behavior clusters: 635
- Selected records: 465
- Selected supervised byte tokens: 28,987
- Selected JSONL size: 301,282 bytes
- Source-row reduction: 89.1%
- Exact-copy representatives retained: 1
- Selected JSONL SHA-256: `8d71ecde7fd6787da7f51c0ecabe370488e12c1f9af5d544a4ab48d68d6007de`
- Selection-manifest SHA-256: `b99359dc0c849228df431ddc72ec723f254c8560034bd7394420dd793dfb16d2`

The exclusions were 2,483 canonical duplicates, 217 direct held-out prompt overlaps, 170 repeated answer targets, 882 records outside the English admission gate, 29 unsupported volatile records, and one private record. The compiler stripped 834 legacy `curriculum_weight` fields. No selected row is physically multiplied.

The change reduces redundant gradient pressure, memorization risk, and replay work. It does not claim a mathematical globally minimal dataset, and it does not change a fixed transformer's tensor size. Weight-file size is controlled by architecture, parameter count, precision or quantization, pruning, and expert design.

## Trust and promotion hardening

- JSONL evidence reload uses typed JSON parsing; quoted text cannot forge trust flags.
- New hard corrections remain authoritative for exact, semantic, and typo-tolerant verified retrieval. A stale answer cannot return on a score tie.
- Fuzzy curated retrieval rejects polarity conflicts and ambiguous equal candidates.
- Training promotion uses a cross-process lock and parent-generation compare-and-swap.
- Model lookup and generation share the internal-model cache lock, preventing reload/use races.
- Expert-registry and active-pointer changes use one recoverable transaction with rollback and restart reconciliation.
- Candidate failures now report the actual failed readiness, loss, identity, privacy, structure, or foundation gate.

## Interface and activity fixes

- Successful native moves are the only bounds committed to the layout cache; failed batches replay through the fallback path.
- Conversation tail detection includes the final visible line, so new replies remain above their review controls.
- Fast and concurrent completion notices are retained by operation key and the three newest completions remain readable.
- Curriculum progress distinguishes source, parsed, eligible, clustered, and selected counts.
- Curriculum manifests fail closed unless the schema, policy, counts, reasons, and SHA-256 fields are valid.
- Shipped curriculum and held-out provenance now uses EngCoder naming instead of displaying obsolete ThreadSpire branding.
- Active training is bounded by token, replay, optimizer-step, checkpoint, and resource ceilings rather than a wall-clock countdown.

## Native builds and tests

The final source state was configured afresh with CMake 4.4.2 and Visual Studio 2022 / MSVC 19.44.35227.0.

- Clean Debug build: passed
- Debug CTest: 16 of 16 passed
- Clean Release build: passed
- Release CTest: 16 of 16 passed
- `git diff --check`: passed
- Changed PowerShell scripts: parser validation passed
- Current executable file/product version: 0.9.1

Final native binaries:

| Binary | Configuration | Bytes | SHA-256 |
|---|---:|---:|---|
| `EngCoder.exe` | Debug | 12,410,880 | `81d669cc49c29e4a316e274d9f63e630f949440bbf48951a28835c8c8c8056fc` |
| `engcoder-native-host.exe` | Debug | 3,380,736 | `5f68bd29fd0fca538dc16811ab5e92998f07711b2ba09a60fe651b25f6770955` |
| `EngCoder.exe` | Release | 5,933,568 | `fa21f2ee859dcb9348b5106ea11ea509acf5b7da8adffe1a10631f993162ccc0` |
| `engcoder-native-host.exe` | Release | 534,016 | `a53d1c743c33f24dc3dbbbc8c6e94cd839bcbc66eaaf09043bb84a3c7ab4754e` |

Release `EngCoder.exe` imports only Windows system libraries: Comctl32, Dwmapi, Gdi32, Ole32, Shell32, UxTheme, WinHTTP, Kernel32, and User32. The native helper imports Bcrypt, Ws2_32, and Kernel32. Neither release binary requires a dynamically deployed Visual C++ runtime.

## Optional trainer validation

The installed Internal PyTorch Training pack was validated directly rather than through the system Python environment:

- Python 3.11.9
- PyTorch 2.14.0+cu132; CUDA runtime 13.2
- NumPy 2.4.6
- Safetensors 0.8.0
- NVIDIA GeForce RTX 5080, 15.92 GiB; BF16 available
- Backend doctor: passed in the repository and installed pack
- NumPy matrix operation: passed
- Safetensors exact write/read round trip: passed
- Synchronized CUDA matrix operation: passed
- Python syntax compilation: 9 files passed
- Curriculum compiler smoke: passed
- Curriculum guard smoke: passed
- Internal model checkpoint/resume/evaluation/promotion smoke: passed
- Active and isolated shadow-model server smoke: passed

After generated backend bytecode caches were removed, the installed optional pack contains 22,744 files and 3,210,771,384 bytes (2.990 GiB). That is Python/PyTorch/CUDA training software, not EngCoder model weights. It contains no model weight file, is not bundled in the portable release, and Transformers/PEFT is absent by design.

## Base footprint and isolation

The clean Release install contains exactly five files:

- `EngCoder.exe`
- `optional-packs/compact-native-inference/bin/engcoder-native-host.exe`
- `license.txt`
- `third-party-notices.txt`
- `licenses/EpochGui-LICENSE.txt`

Total installed size is 6,471,146 bytes (6.17 MiB), below the 100 MiB ceiling. It includes no Python runtime, trainer, PyTorch package, neural weights, user memory, learning history, candidate, checkpoint, cache, test binary, or debug symbol.

The base release remains exactly 0 B of trained neural weights. Immediate known replies come from deterministic operations and verified native retrieval, not hidden neural training. A real neural candidate exists only after the optional trainer creates weights and every promotion gate passes.

## Window lifecycle evidence

The final Release executable was launched against a new isolated `ENGCODER_DATA_ROOT`, created its real top-level window, processed five successive resize requests, answered a synchronous responsiveness probe after each, and exited with code 0 through normal window close.

- Actual minimum clamp observed: 995 × 717
- Other observed sizes: 1280 × 720, 1800 × 1000, and 1440 × 900
- Resize/probe round trips: 1–16 ms
- Hung-window detections: 0
- Working set during the sequence: approximately 38.7 MiB
- Private bytes during the sequence: approximately 18.9 MiB

Native-app capture was unavailable to this task, so this is lifecycle, responsiveness, and geometry evidence—not a fresh screenshot-level visual-QA claim. No synthetic screenshot is used.

## Boundaries

- Epoch and its worktree were not modified. EngCoder continues to vendor only the recorded EpochGui subtree boundary.
- Connected Qwen and Nemotron models remain teachers/verifiers; their weights are never copied into EngCoder.
- The 320 anchors and document held-outs remain evaluation-only.
- The compiler is deterministic and dependency-free, but candidate evaluation remains the authority on whether a compact selection is sufficient.
- Release archive hashes, source-ledger verification, exact commit/tag identifiers, cleanup, and Site live-edge verification belong to the external immutable release receipt created after this source tree is frozen.

## Related documentation

- [Minimal curriculum compiler](minimal-curriculum-compiler.md)
- [Continual learning pipeline](learning-pipeline.md)
- [Autonomous curriculum and training](autonomous-curriculum-and-training.md)
- [Learning and retraining guide](../custom-training.md)
