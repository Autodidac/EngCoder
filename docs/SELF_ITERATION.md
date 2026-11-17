# Candidate self-iteration

Use a disposable copy or a separate Git worktree of a project you own. To work on EngCoder itself, use a second EngCoder checkout as the candidate. Configure a fresh CMake build inside that checkout with tests enabled. Launch a separate EngCoder instance with its own data directory.

```powershell
$env:ENGCODER_WORKSPACE = 'D:\work\engcoder-candidate'
$env:ENGCODER_ITERATION_CANDIDATE = $env:ENGCODER_WORKSPACE
$env:ENGCODER_ITERATION_BUILD_DIR = 'out/build/windows-vs2022'
$env:ENGCODER_DATA_ROOT = 'D:\work\engcoder-candidate-state'
```

Use the same frontend as the running EngCoder release. When launching a separate server, point its `--web-root` at that release’s `projects/web` and choose an unused loopback port with `--port`. Open the local UI, select **EngCoder Lab → Runtime → Use candidate self-iteration for the next send**, and send one specific coding objective.

When working in Codex, run `EngCoderServer.exe` with the isolated workspace and data directory and open its loopback URL in the Codex preview. This uses the same shared frontend and keeps the run in view. Keep candidate tests and build configuration outside the edit you are approving.

The candidate setting is consumed on submission. KeyMaker can inspect the checkout and propose edits. Approve each exact edit in chat. Existing files require an exact patch. If the file changes while the approval is pending, EngCoder stops and requests a fresh review. New-file creation cannot replace an existing file. EngCoder checkpoints the files and runs CMake and CTest in order against the configured candidate, using Release and a five-minute timeout per check. An empty test suite fails the gate. A nonzero exit sends bounded diagnostics back to the selected model for at most two fresh repair attempts within the normal turn budget. Stop cancels the task. Run info records check starts, exits and repair attempts.

A passing task leaves the candidate available for review. Inspect its diff and decide whether to keep the candidate or integrate the change through your normal Git workflow. Deny declines the pending tool call. Stop ends the current task and discards any pending approval. The native host issues a single-use approval ID for each proposal, including when a model repeats a tool-call ID. Check Run info for the terminal result since the approval acknowledgment arrives before edit and validation results. The isolated maintenance candidate shows the applied checkpoint and reported check outcomes in a separate Run info section. A check with only a start receipt remains Started after the task ends. Every new task requires a fresh opt-in and fresh edit approvals. Candidate edits and check receipts are separate from neural training.

The candidate directory is operator-designated and the build cache must point into it. Use trusted build scripts and test targets. Compiler and test processes run with the host account’s permissions. For untrusted builds, use a separately confined environment. Keep other editors and builds off the candidate while the run is active. This workflow leaves campaign queues, restart/resume and automatic source promotion outside its scope.

## EngCoder maintenance

Use the local KeyMaker model to propose the first change for a bounded mission. Give it the symptom, source paths, expected behavior, and one validation target. Keep independent tests outside the proposed edit. For multi-file work, use a separate EngCoder checkout and retain an exact diff for review before integrating it into the working source.

Start with the smallest model that can complete the mission. If the run stalls, compare the tool calls, provider timing, and latest candidate receipt before retrying. Change the route, input context, or runtime condition that caused the failure. Keep a passing build and test receipt, the model and candidate identities, and what the operator approved. Report provider token counts and decode speed when measured. Browser estimates and whole-device GPU measurements have their own labels.

The first pass covers task lifecycle, approval recovery, and visible candidate evidence. The task terminal events repair passed in a disposable EngCoder checkout after a standalone row-selection trial. Use short, exact source excerpts for small context windows. Patch text must match the file's current line endings. Track working-source integration and platform acceptance in [Future missions](FUTURE_MISSIONS.md) and [Validation](../VALIDATION.md).
