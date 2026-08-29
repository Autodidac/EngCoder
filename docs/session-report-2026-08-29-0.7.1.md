# EngCoder Live Auto Queue Repair Verification — 2026-08-29

## Scope

This pass repairs the stalled **Live Auto Training** state visible after a built-in-only reply failed. It keeps foreground AI selection, automatic interaction review, idle practice generation, and neural-weight optimization as separate controls and queues.

No Epoch source, checkout, process, or application data was touched. EngCoder application data was inspected read-only and preserved. No conversation, live candidate, accepted example, queued weight update, user memory, model generation, or model weight was reset.

## Observed state

- The selected foreground route was **Built-in EngCoder only**. Older Qwen and Nemotron replies remained correctly labeled in the transcript as historical per-reply records; changing the current selector does not rewrite conversation history.
- The selected howdy request used only built-in EngCoder and produced the visible no-answer result.
- candidates/live-interactions.jsonl contained one cleaned interaction waiting for Live Auto review.
- No Live Auto attempt log or completed cursor existed, so that candidate had never reached either authorized reviewer.
- jobs/training-state.json contained one queued english-foundation neural-weight update.
- The internal model footprint remained **0 B neural weights** because the optional Internal PyTorch Training pack and weight files were absent.
- Both exact loopback reviewers were available through LM Studio: qwen/qwen3.8-27b and nvidia/nemotron-3-nano-4b.

## Root causes and repair

- The post-reply admission path read the older idle-practice checkbox instead of the separate **Live Auto Training** checkbox. It now reads only the Live Auto control.
- The post-reply path overwrote the candidate's two-second review schedule with the older two-minute idle-practice delay. Live candidates now retain their short foreground-idle schedule.
- A merely queued neural-weight update was treated as active weight training. Because the optional trainer was absent, that queued update could never run and permanently blocked Live Auto review. Queued weight work no longer blocks interaction review; genuinely active neural training remains mutually exclusive.
- The persistent activity text now prioritizes pending Live Auto review and also reports queued weight work. When the trainer is absent, the status explicitly explains that weights remain 0 B, the optional pack is not installed, and no queued data was lost.

## Resulting contract

- Foreground Chat still calls only the selected route. Manual no-fallback selection remains no-fallback.
- Live Auto uses Qwen and Nemotron only after the foreground reply, while Chat is idle, and only for the independently audited review workflow.
- The existing cleaned candidate can resume review after restart without resetting or re-approving it.
- Accepted evidence and queued weight updates remain stored even when the neural trainer is unavailable.
- EngCoder never silently installs the multi-gigabyte optional training runtime. Neural weights remain 0 B until that pack is explicitly installed and the gated candidate-training pipeline succeeds.

## Verification

- Visual Studio 2022 Debug build: passed.
- Debug CTest: 5/5 passed.
- Visual Studio 2022 Release build: passed.
- Release CTest: 5/5 passed.
- Release EngCoder.exe: 5,372,928 bytes; SHA-256 d23b402f544960a3da2cfbd60c0e0945005be5caf3c41f8825b48f269ec089d8.
- Release engcoder-native-host.exe: 286,208 bytes; SHA-256 4deee2e24a83ef3cc5fd2f52b788b1bb15b00a8c2d0c9fa1eaa34951183c8c83.
- Debug EngCoder.exe: 9,770,496 bytes; SHA-256 b6c362720a9fa4b3469a3a1850d02af6af3d28d66bd3c31142bce95fb0e9db2f.
- Debug engcoder-native-host.exe: 1,527,808 bytes; SHA-256 e98bab035e3e8844641f4cb6d990e5e36b83f51c572bde5b86b34c723b7d2822.

No fresh automated visual-QA claim is made. The user-supplied screenshot established the visible symptom; routing, scheduling, queue separation, missing-trainer disclosure, and persistence behavior are covered by the compiled source contracts and tests.
