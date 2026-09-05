# EngCoder 0.7.2 retained-review and EpochGui verification report

Date: 2026-08-30
Branch: `codex/threadspire-unified-workbench`

## Outcome

This pass fixes the mismatch between what the user taught, what Live Auto reviewed, and what the interface reported. It also moves the expensive part of Workspace resizing onto a bounded EpochGui layout path without importing Epoch application or editor code.

Three approved replies remain three durable teachings. A single queued neural-weight job is a coalesced topic update, not a claim that only one teaching exists. A cancelled or unavailable reviewer now leaves the exact interaction at the Live Auto ledger cursor as **retry pending**. It is retried after a 30-second backoff and is not misreported as rejected, consumed, or trained.

## What is in phrases and what is in weights

A modern language model is not a filing cabinet of complete phrases. Text is split into tokens and training adjusts many numeric parameters so the model assigns useful probabilities to token sequences in context. A familiar answer can emerge from those distributed relationships without one stored row containing the complete sentence.

EngCoder's current built-in path is deliberately different and the interface must say so: the base release contains **0 B of trained neural weights** and no trained built-in LLM. It ships a deterministic curriculum and uses verified retrieval memory for approved replies. That makes a newly accepted phrase usable immediately and inspectable, but it is memory/retrieval rather than neural generalization. Actual weight optimization requires the optional training pack and must produce, test, and promote a separate candidate before it can replace the working model.

The two visible counts therefore mean different things:

- **User-taught replies** counts durable human-approved or corrected examples in `memory/episodes.jsonl`.
- **Neural-weight jobs** counts coalesced topic updates in the persistent trainer queue.

Coalescing prevents three closely related approvals from launching three redundant retraining passes. It does not delete or merge the three source teachings. A single human-approved phrase stays memory-only; weight admission requires four distinct approvals for the skill or independent compiler, test, or reference evidence.

## Live Auto retention repair

Live Auto still requires distinct external local reviewers; EngCoder cannot approve its own output. The Qwen and Nemotron roles remain independent. Reviewer cancellation, endpoint failure, missing required reviewer, missing confirmer, and malformed reviewer JSON are now transient states:

1. Append a complete `retry-pending` audit record.
2. Keep the candidate visible in the Training Library as **Pending independent review**.
3. Leave the ledger cursor on that exact interaction.
4. Apply a 30-second retry backoff while Chat keeps priority.
5. Make no acceptance, rejection, source-feedback, memory, or weight decision.

Only a completed negative review advances the cursor as rejected. Accepted, revised, needs-evidence, rejected, excluded, and retry-pending decisions remain individually inspectable.

## EpochGui integration boundary

The vendored source is the clean backend-neutral subtree from:

- Source: `<authorized EpochGui source>`
- Parent commit: `e974921622643e796be134b356f9ca4f4137f811`
- EpochGui subtree tree: `191badda3412f195b4a1cfc635b05b2f3df9584c`
- Last subtree change: `261f7fff8473432bba8a948d14988dba290329cf`
- Declared version: 0.89.30
- License: `LicenseRef-MIT-NoSell`; notices and provenance are preserved.

All 51 copied source, project, test, documentation, and license files were compared by SHA-256 with the authorized subtree: 51 matched, 0 missing, 0 different. No Epoch application/editor adapters, renderer/runtime modules, build output, generated workspace data, or unrelated parent-worktree files were copied. Epoch itself was not edited.

EngCoder links EpochGui as a C++23 static library and uses its splitter and rectangle primitives. EngCoder continues to own HWND lifetime, Win32 events, native commands, painting, text, and presentation.

## Resize and visibility work

- Workspace conversation/activity geometry now comes from an EpochGui 70/30 splitter with bounded minimum sizes.
- Desired child bounds are cached as EpochGui rectangles, eliminating repeated `GetWindowRect` and coordinate remapping for unchanged controls.
- `WM_SIZE` storms are coalesced to one layout pass every 33 ms.
- Hidden pages and unchanged controls are skipped.
- Changed controls are committed with one deferred native move batch per parent.
- Ending a drag performs one exact layout and one full dark-tree redraw.
- The persistent activity strip retains the latest Live Auto review, taught-reply count, neural-job count, and retry state instead of flashing and disappearing.

## Verification

The final Debug build completed and Debug CTest passed 15/15. The final Release build completed and Release CTest passed 15/15. The suite includes EngCoder core, workbench, native inference, base-footprint, and curriculum-guard coverage plus ten EpochGui layout/input/workspace tests. `git diff --check` reported no whitespace errors.

Fresh automated visual inspection was attempted twice, but the Windows computer-control helper could not start because the host sandbox failed while applying read ACLs. No visual-QA claim is made. The resize behavior is covered by compiled implementation tests and source contracts; a real-window eye check remains the honest final visual verification when the host helper is available.

## Repository recovery

This worktree's `.git` pointer referred to repository metadata that had been moved to the Recycle Bin when the original Downloads checkout was deleted. The exact repository metadata was validated and copied back to its original path so the existing worktree and branch history could be used again. No source tree was restored over EngCoder, no reset was performed, and no Epoch repository was changed. `git fsck --full --no-reflogs` found no corruption; only ordinary dangling objects from prior work remain.
