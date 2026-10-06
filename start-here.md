# Start here

## Build and verify

From PowerShell in the EngCoder source directory:

```powershell
cmake --fresh --preset windows-vs2022
cmake --build --preset windows-vs2022-release --clean-first -- /m:1
ctest --preset windows-vs2022-release
```

Launch:

```text
out\build\windows-vs2022\bin\Release\EngCoder.exe
```

The base executable is native C++23 and does not require Python or PyTorch. Its compact native answer host ships beside it and runs in place; first launch does not install or copy a package into `%LOCALAPPDATA%`.

## Understand the first-run state

A fresh run starts the bundled native foundation automatically. It can answer deterministic or curriculum-backed prompts with **0 B neural weights**. That is expected and is reported as **Native foundation**, not as a trained model.

The Training Library does not start at zero total records because the executable ships 4,247 curriculum examples and 320 held-out checks. The header separately shows:

- your approved, rejected, and superseded learning;
- automatic teacher attempts;
- bundled curriculum;
- held-out checks.

After a fresh reset, the first two categories start at zero. The bundled categories remain because they are application resources, not previous user training.

## Use Chat

Leave AI access on automatic for normal use. Choose **Built-in EngCoder only** to test only EngCoder. Locking an exact AI disables automatic switching; a failure is shown instead of silently contacting another server.

Accepting a reply stores one cleaned, deduplicated lesson. It does not repeatedly amplify the same phrase. A correction immediately replaces the old exact answer and keeps the rejected draft only as excluded audit evidence.

The Workspace follows the same reading order as the work itself: session and routing, conversation, reply review, composer, then the persistent activity card. The right rail explains the selected answer. Current and recently completed work remains visible in the activity card, while Activity retains the detailed history.

## Improve the neural model

Automatic idle learning collects independently checked practice material while the app is idle. It needs a connected teacher AI for generated examples and does not modify weights.

Press **Compile data and retrain model** to derive the smallest bounded curriculum it can justify from pending verified evidence, then retrain a separate test copy. Equivalent phrases collapse to one behavior, repeated answer targets are capped, and normal updates contain only the new delta plus bounded regression replay. Chat retains priority, checkpoints resume after interruption, and the working model remains active until the candidate passes every check.

Actual neural optimization requires the optional **Internal PyTorch Training** pack. EngCoder never installs it automatically. Without it, approved lessons still work through verified memory and remain queued for a future weight job; the interface truthfully continues to report **0 B neural weights**.

When that job is blocked, the requirement reports the missing pack files rather than a generic failure. A complete Internal PyTorch Training source contains:

- `runtime/Scripts/python.exe`
- `backend/model_worker.py`
- `backend/internal_model.py`
- `backend/curriculum_guard.py`
- `backend/curriculum_compiler.py`

Prepare or choose that package yourself under **Settings → Capabilities**, then select **Install selected package**. No failed Chat or background-learning operation may trigger installation.

Live Auto and the English foundation may load the exact authorized local Qwen or Nemotron teacher on demand when it is already installed in the loopback model server, using a bounded role-specific context request for that initial load. That is runtime loading, not package or model installation. EngCoder never downloads a teacher or unloads or reconfigures an already-loaded model, and an unavailable teacher stays visible as retry-pending.

## Inspect disk and RAM

Activity distinguishes:

- active answer engine;
- neural model history;
- curriculum and learning data;
- installed software packs;
- outputs/cache;
- total usage and growth;
- free disk;
- EngCoder-owned live RAM.

Other AI applications such as LM Studio are not counted as EngCoder RAM.

See [readme.md](readme.md), [architecture.md](architecture.md), [building.md](building.md), [autonomous curriculum and training](docs/autonomous-curriculum-and-training.md), and [custom-training.md](custom-training.md).
