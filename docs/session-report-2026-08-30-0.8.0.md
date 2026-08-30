# EngCoder 0.8.0 autonomous curriculum and training report

Date: 2026-08-30  
Branch: `codex/threadspire-unified-workbench`

## Outcome

EngCoder 0.8.0 adds an explicit, resumable **Build English Foundation** workflow beside the continuing **Live Auto Training** workflow. Qwen 3.8 27B is the deep English curriculum architect and difficult-example teacher. Nemotron 3 Nano 4B is the high-throughput short/medium conversation generator. Each model verifies material produced by the other; neither model can approve its own output and neither model's weights are copied.

The foundation workflow accepts only public-domain, permissively licensed, or user-owned UTF-8 text/Markdown. It records source rights, origin, content hash, ingestion time, chapter/section spans, and a document-level training/held-out split before generating examples. Raw source text remains local under `%LOCALAPPDATA%\EngCoder\curriculum` and is not included in the application or release archives.

## Curriculum and audit behavior

- Qwen creates competency maps and difficult long-context lessons at measured concurrency, normally one request.
- Nemotron creates one to four conversational variants with adaptive concurrency up to four requests.
- Weak but useful material may be revised once and must then be independently confirmed.
- Endpoint failure, cancellation, or malformed JSON stays retry-pending at the durable chunk cursor.
- Completed negative reviews remain visible rejections.
- Local admission rejects personal/private information, credentials, volatile facts, provider identity, emoji noise, corrupt/error text, copied spans, duplicate examples, and non-weight-eligible material before a training or held-out row is written.
- Resume does not duplicate token-training or evaluation rows. Explicit rebuild resets the active corpus but preserves the append-only attempt audit.
- The Training Library exposes cleaned prompt/answer text, source span, generator, verifier, decision, revision/rejection reason, weight eligibility, and candidate generation.

The complete durable-file, progression, routing, and failure contract is documented in [Autonomous Curriculum and Training](autonomous-curriculum-and-training.md).

## Neural training truth

The base portable release still contains **0 B of trained neural weights**. Verified memory and native retrieval remain immediately useful, but they are not described as neural learning.

Foundation records queue only EngCoder's custom internal model, `threadspire://coder`. They never enter the external Transformers/PEFT path. The optional **Internal PyTorch Training** pack trains an isolated first full-weight candidate, evaluates every one of the 320 bundled anchors plus accepted document-held-out records, applies structural-output, identity, privacy, loss, readiness, and regression gates, and atomically promotes only a passing candidate. Later verified microbatches freeze shared weights by default and update bounded skill experts.

The Internal PyTorch Training pack was not installed on the release host. Only Compact Native Inference was installed, and the host Python reported `torch=not-installed`. Therefore no tensor-optimization smoke or promoted neural-weight claim is made. The compiled candidate pipeline, checkpoint/resume, rejection/rollback, and atomic-promotion contracts are covered by the native tests. Installing the optional trainer remains an explicit multi-gigabyte action; it is never silently downloaded.

## Interface

The Learning page now provides source selection, rights status, Build/resume, Pause, and Rebuild. Chat preempts the foundation teacher lane and arms automatic resume at the same durable chunk. A manual Pause remains paused. The persistent activity surfaces show the current operation, teacher, verifier, source span, generated/accepted/revised/rejected/retry counts, chunks, tokens, queued weight jobs, checkpoint, candidate, active model size, and promotion result.

Manual Copy, Edit, Retry, Accept, Reject, and Learn controls remain available. Live Auto Training remains directly beside them and continues after the one-time foundation build. Conversation remains the first curriculum; planning and coding unlock only through measured evidence, and coding admissions continue to require compiler/test evidence.

## Verification

- Visual Studio 2022 Debug build: passed.
- Debug CTest: 15/15 passed.
- Visual Studio 2022 Release build: passed.
- Release CTest: 15/15 passed.
- Python bytecode compilation: passed for `internal_model.py`, `model_worker.py`, and `curriculum_guard.py`.
- `git diff --check`: passed.
- Release `EngCoder.exe`: 5,554,176 bytes; SHA-256 `290082a27eb9b31fb84b2d0dcac91042afb25a0f770ccd51629e0ec9da2d3306`.
- Release `engcoder-native-host.exe`: 286,208 bytes; SHA-256 `d563ede7bb211629ea437f692362a6ccda5b1095ee05e5aac768187db634dc5c`.
- Installed runtime before documentation: 5,840,384 bytes (5.57 MiB).
- Both release executables are portable and unsigned.

The tests cover long-document parsing, rights admission, section/document splitting, deduplication, delayed fake teachers, independent roles, adaptive concurrency, foreground stop/resume, bounded checkpoints, one-revision confirmation, retry-pending recovery, append-only rebuild audit, private/current-material exclusion, self-verifier rejection, all-anchor foundation gates, and internal-model binding.

Fresh automated real-window visual QA was attempted through the Windows computer-control path, including its required recovery cycle, but the helper could not start because the host sandbox failed while applying deny-read ACLs (`helper_unknown_error`). No fresh visual-QA claim is made. Epoch was not accessed or edited during this pass; the existing vendored EpochGui boundary was preserved.
