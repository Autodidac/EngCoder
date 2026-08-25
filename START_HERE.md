# Start Here — ThreadSpire 0.6.0

## 1. Build and validate

From PowerShell in the ThreadSpire source directory:

```powershell
cmake --fresh --preset windows-vs2022
cmake --build --preset windows-vs2022-release --clean-first -- /m:1
ctest --preset windows-vs2022-release
```

Launch `out\build\windows-vs2022\bin\Release\ThreadSpire.exe` after the tests pass. The base application is native C++23 and does not require Python or PyTorch.

## 2. Use the unified Task workbench

Open **Task**, choose a modality, select a compatible provider, and submit the prompt. Ask/write, code help, repository work, images, speech, and tools share the same request, streaming, cancellation, and artifact path. Coding Agent and Long Horizon are execution profiles of that task instead of separate interfaces.

Connected OpenAI-compatible local servers can be used without an internal-training runtime. Repository tasks keep compiler/test details on **Run Details**, while generated image/audio artifacts are written under `%LOCALAPPDATA%\ThreadSpire\outputs`.

## 3. Install capability packs only when wanted

Open **Connections** to inspect projected download and installed sizes. Available pack classes are compact native inference, internal PyTorch training, Transformers/PEFT fine-tuning, image, and audio.

Selecting **Install pack** performs a staged, verified transaction. It does not select a model, start a host, queue training, or interrupt a running task. Python-based files and dependencies exist only inside the corresponding optional pack.

## 4. Train deliberately and safely

After explicitly installing and verifying the internal-training pack, use **Learning** to select the target and queue work. Package selection never changes the target. Durable jobs coalesce and move through queued, preparing, training, evaluating, promoting, and terminal states. A cancellation or crash leaves an atomic checkpoint that can resume.

Foreground inference has priority. A candidate trains beside the active generation and is tested in its own isolated host. The active model keeps serving until the candidate passes checksum, health, inference-canary, deterministic held-out validation, and regression checks and is committed atomically.

The strengthened English Gate v2 uses curated provenance, deduplication, deterministic anchors, and an independent verifier. Bootstrap caps are 24,000 steps for Micro, 36,000 for Standard, and 50,000 for Large; reaching a cap stops with diagnostics.

## 5. Watch storage

The Task page shows large cards for current model, accepted training data, installed packs, outputs/cache, total usage and growth, and free disk. **Storage & Logs** adds largest files/directories, pack projections, quotas, and safe no-follow cleanup for logs, unpromoted candidates, checkpoints, outputs, and caches.

The native installed base must remain at or below 100 MiB, excluding models and optional packs. See [BUILDING.md](BUILDING.md), [ARCHITECTURE.md](ARCHITECTURE.md), and [CUSTOM_TRAINING.md](CUSTOM_TRAINING.md).