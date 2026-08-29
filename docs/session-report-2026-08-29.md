# EngCoder Live Auto Training Verification — 2026-08-29

## Scope

This pass adds a visible, continuously inspectable **Live Auto Training** workflow without replacing the existing manual reply review, idle verified-practice generation, or neural-weight controls. It preserves the foreground rule that one selected AI answers a user request. The two authorized local teacher models are used together only by the background review pipeline.

No Epoch source, checkout, process, or application data was touched. Existing EngCoder application data was preserved. No user memory, conversation, accepted example, generation, or model weight was reset.

## Final design

- Added a separate **Live Auto Training** checkbox immediately after the per-reply controls. Its setting is stored independently from idle practice-example generation.
- Successful live interactions are sanitized and appended to a non-wrapping candidate ledger. Request errors, local controls, calculator results, repository receipts, verified-memory hits, emoji, private reasoning, branding leakage, and duplicates are not treated as reusable training truth.
- Personal, private, proprietary, machine-local, credential-like, or nonpublic material is excluded with a redacted audit entry. The rejected content is not copied into the audit record.
- Current or obscure facts require tool/research evidence or explicit user confirmation. Coding examples require compiler and test evidence. Missing evidence produces a visible `needs-evidence` decision instead of fabricated training data.
- The only authorized Live Auto reviewers are exact loopback OpenAI-compatible endpoints for `qwen/qwen3.8-27b` and `nvidia/nemotron-3-nano-4b`. Both distinct model families must approve the final form before it can be stored.
- Review decisions are `accept`, `revise`, `reject`, or `needs-evidence`. A revision is independently checked by the other model. Straight acceptances also require the second model's confirmation.
- Up to four requests per teacher model can run concurrently, for a bounded maximum of eight background review attempts. Foreground Chat retains priority.
- Accepted reusable behavior enters the existing verified episode store and automatic microbatch queue. The active generation is never trained in place: a separate candidate is compiled, trained, validated, and promoted only if it passes the existing gates.
- Curriculum advances from conversation to planning and then coding only after measurable accepted volume, at least one trained generation, and the required validation score. Code remains constrained by compiler/test evidence.
- Reviewer outcomes feed the model registry's reliability feedback, allowing the internal EngCoder generation to earn more automatic traffic as its measured behavior improves.
- Every Live Auto decision is visible in the Training Library and durable activity log. The always-visible activity strip reports queued, reviewing, waiting, completed, failed, and evidence-held work rather than flashing a transient status.

The complete behavior, files, gates, failure handling, and security contract are documented in [Live Auto Training](live-auto-training.md).

## Verification

- Visual Studio 2022 Debug build: passed.
- Debug CTest: 5/5 passed.
- Visual Studio 2022 Release build: passed.
- Release CTest: 5/5 passed.
- Release `EngCoder.exe`: 5,372,416 bytes; SHA-256 `bf30746d1775098dcd7738a214cf4ffe56881c61d59734972a45354bf3b2f073`.
- Release `engcoder-native-host.exe`: 286,208 bytes; SHA-256 `c483ccc0ca4db0cc93d1d1dd59e3c14aeee64e40a6c8e9fa58e6f4f756fcd812`.
- Debug `EngCoder.exe`: 9,766,400 bytes; SHA-256 `604708dda2f2ccd997677068b91762a5b3fa0f6f4f417a62a10aebe89ad77fba`.
- Debug `engcoder-native-host.exe`: 1,527,808 bytes; SHA-256 `08e92d16618647e188a9f476c59d1ff7b669d95dbd4def62c42d39b61c29bd39`.
- Repository checksum manifest: refreshed for all 140 tracked source/documentation entries other than the manifest itself.

The Windows Computer Use visual harness was attempted twice, including its required kernel reset, but the trusted Node process exited before a session could initialize. This was a host automation failure rather than an EngCoder process failure. No fresh visual-inspection or screenshot claim is made. Layout, activity persistence, control placement, exact reviewer allowlisting, ledger behavior, review gates, and non-wrapping cursor behavior are covered by the compiled workbench/source contracts.

## Cleanup

Nine stale `threadspire-ai-tests-*` directories were removed only after every resolved target was verified as a non-reparse directory directly beneath the Windows temporary root. Debug and Release binaries were retained in their documented build folders. No branch, patch, user data, model data, or unrelated checkout was deleted.
