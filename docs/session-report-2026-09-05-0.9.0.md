# EngCoder 0.9.0 measured-gap autonomy and durable-training report

Date: 2026-09-05
Branch: `codex/threadspire-unified-workbench`

## Outcome

EngCoder now has two deliberately separate learning loops. **Live Auto Training** handles ordinary interaction capture, cleanup, independent local-teacher review, revision, durable admission, and bounded candidate scheduling. **Build English Foundation** remains an explicit, resumable one-time action for rights-cleared long-form material and a full internal-model candidate. Neither workflow silently copies Qwen or Nemotron weights.

Nemotron 3 Nano 4B owns routine short/medium English breadth with adaptive concurrency up to four. Qwen 3.8 27B verifies that routine material and becomes a generator only for an explicit foundation build or a measured difficult gap. Three independently reviewed reusable conversational failures wake one Qwen repair at a time; Nemotron must independently verify the replacement. The failed replies stay visible as negative evidence and cannot enter positive memory or weights.

## Admission and audit boundaries

- A central privacy gate now covers every learning entry point. Credentials, personal identifiers, private/nonpublic facts, provider branding, emoji-only noise, error drafts, corrupt output, copied passages, and unsupported volatile facts are excluded from weight admission.
- Current or tool-derived claims require a syntactically valid `tool-trace-v1` receipt. A vague claim of verification is not evidence.
- Cancelled, unavailable, or malformed teacher reviews remain retry-pending at the exact durable cursor. Completed negative reviews remain inspectable rejections.
- A generator cannot be its own verifier. The routine and measured-gap paths require distinct authorized loopback Qwen and Nemotron model identities.
- Each generated record exposes its cleaned prompt and answer, source span, generator, verifier, decision, revision or rejection reason, weight eligibility, and eventual candidate generation in the Training Library.
- Human teachings and neural jobs are counted separately. Several durable approved examples may intentionally coalesce into one bounded topic update; coalescing does not erase or merge the teachings.

Permanent user memory remains editable retrieval context and never becomes weight data. Raw foundation sources stay outside the model weights and release archives unless their rights explicitly permit redistribution.

## Foundation and neural-training contract

Long-form sources are normalized, deduplicated, divided into sections, and assigned to training or held-out partitions at the document/section boundary before examples are generated. The compiled foundation evaluation set contains all 320 bundled anchors plus independently verified document-held-out examples. Only `foundation-training.jsonl` is used as the training input.

The optional **Internal PyTorch Training** pack is the trainer for EngCoder's custom internal transformer. It is not the external Transformers/PEFT pack. The new pack-preparation tool builds an isolated version-recorded Python environment, verifies PyTorch/Safetensors and CUDA, and atomically places a complete payload in the pack inbox. It downloads no model weights and never modifies the portable base.

Every internal candidate is bound to immutable training and evaluation fingerprints, one durable job, one parent generation, and its exact checkpoint. Restart recovery reconciles one nonterminal job per interrupted candidate. A mismatched skill, base, parent, candidate, checkpoint, or state transition fails closed. Training, evaluation, and promotion transitions must be durably recorded before progress continues, and promotion is a compare-and-swap operation that cannot replace a newer active generation.

The first foundation candidate must pass held-out loss, all anchors, structural-output canaries, identity, privacy, generation readiness, and regression checks. Later skill work freezes shared weights by default and updates bounded experts. Routing earns preference only from accepted outcomes and response gates, never from the mere presence of a model file.

## Interface and responsiveness

The always-visible activity card retains the active or most recently completed operation and reports teachers, verifier, source, decision totals, measured gaps, queued neural jobs, token/checkpoint progress, candidate, model size, disk use, and EngCoder-owned RAM. Foreground Chat preempts background work without discarding its durable position.

The Win32 layout path now formats only transcript deltas, caches unchanged EpochGui bounds, batches child moves by parent, skips hidden pages, coalesces resize storms to a 33 ms layout tick, and defers heavyweight storage/model/queue telemetry only during the active drag. A final exact layout and refresh run when the drag ends. Localhost model discovery now checks cancellation between probes, so app shutdown and foreground work do not wait for the complete scan.

An isolated hidden real-window probe exercised 120 size changes in 95 ms. The app remained responsive, working set moved from 38,182,912 to 38,227,968 bytes, and a close posted during automatic server discovery completed in 1,590 ms after the cancellation repair. This verifies window creation, message-loop response, layout execution, discovery cancellation, and clean process exit; it is not a visual screenshot review.

## Verification

- Fresh Visual Studio 2022 Debug build: passed.
- Debug CTest: 15/15 passed.
- Clean Visual Studio 2022 Release build: passed.
- Release CTest: 15/15 passed.
- Python syntax compilation: passed for seven trainer, server, guard, smoke-test, and generator files without creating bytecode artifacts.
- PowerShell parser: passed for `prepare_internal_training_pack.ps1`.
- Deterministic bootstrap regeneration: 4,247 unique training rows and 320 unique held-out anchors; regenerated files matched the tracked files exactly.
- Installed portable base: 6,436,060 bytes (6.13 MiB), below the 100 MiB ceiling.
- Release `EngCoder.exe`: 5,898,752 bytes; SHA-256 `775fd713358a03e91e30259f2e4a293422b2b57063f1de08c7393b0416b8ce1d`.
- Release `engcoder-native-host.exe`: 534,016 bytes; SHA-256 `1049dc71240743acf7a4822eef1b98f3f8d7636a440c7b94067d3930bbca71c9`.
- PE dependency inspection found only Windows system DLLs; no separately installed Visual C++ runtime is required.
- Source whitespace check: passed.

PyTorch and Safetensors were not installed on this release host, so the two tensor-optimization smoke tests were correctly not registered. The release therefore makes no claim that an actual neural candidate was trained or promoted. The portable base remains **0 B of trained neural weights**; verified memory and native retrieval are useful immediately but are not mislabeled as neural learning.

No fresh screenshot-level visual QA claim is made. The active Epoch worktree was consulted read-only for state-machine design principles and was not modified, built, launched, or interrupted. EngCoder continues to use only its already-vendored backend-neutral EpochGui boundary.
