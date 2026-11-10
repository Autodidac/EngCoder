# Candidate self-iteration

Use a disposable copy or a separate Git worktree of a project you own. To work on EngCoder itself, use a second EngCoder checkout as the candidate. Configure a fresh CMake build inside that checkout with tests enabled. Launch a separate EngCoder instance with its own data directory.

```powershell
$env:ENGCODER_WORKSPACE = 'D:\work\engcoder-candidate'
$env:ENGCODER_ITERATION_CANDIDATE = $env:ENGCODER_WORKSPACE
$env:ENGCODER_ITERATION_BUILD_DIR = 'out/build/windows-vs2022'
$env:ENGCODER_DATA_ROOT = 'D:\work\engcoder-candidate-state'
```

Use the same frontend as the running EngCoder release. When launching a separate server, point its `--web-root` at that release’s `projects/web` and choose an unused loopback port with `--port`. Open the local UI, select **EngCoder Lab → Runtime → Use candidate self-iteration for the next send**, and send one specific coding objective.

The candidate setting is consumed on submission. KeyMaker can inspect the checkout and propose edits. Approve each exact edit in chat. Existing files require an exact patch. If the file changes while the approval is pending, EngCoder stops and requests a fresh review. New-file creation cannot replace an existing file. EngCoder checkpoints the files and runs CMake and CTest in order against the configured candidate, using Release and a five-minute timeout per check. An empty test suite fails the gate. A nonzero exit sends bounded diagnostics back to the selected model for at most two fresh repair attempts within the normal turn budget. Stop cancels the task. Run info records check starts, exits and repair attempts.

A passing task leaves the candidate available for review. Inspect its diff and decide whether to keep the candidate or integrate the change through your normal Git workflow. Every new task requires a fresh opt-in and fresh edit approvals. Candidate edits and check receipts are separate from neural training.

The candidate directory is operator-designated and the build cache must point into it. Use trusted build scripts and test targets. Compiler and test processes run with the host account’s permissions. For untrusted builds, use a separately confined environment. Keep other editors and builds off the candidate while the run is active. This workflow leaves campaign queues, restart/resume and automatic source promotion outside its scope.
