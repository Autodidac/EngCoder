# Building ThreadSpire

## Windows target

Requirements:

- Windows 10 or newer
- Visual Studio 2022 or 2026
- Desktop development with C++
- Windows 10/11 SDK
- CMake 3.30+
- 64-bit Python 3.12 recommended for the ML runtime
- Git for repository-agent work

From PowerShell in the repository root, build, validate, and launch:

```powershell
.\BUILD_AND_RUN.cmd clean
```

`BUILD_AND_RUN.cmd` calls the lower-level `build.cmd`, detects the installed Visual Studio generator, configures CMake, builds Release with `/W4 /WX /permissive-`, runs CTest, closes an older running ThreadSpire instance, and launches the validated executable. Use `.\build.cmd clean` when a build-only workflow is required.

Visual Studio 2022 output:

```text
out\build\windows-vs2022\bin\Release\ThreadSpire.exe
```

Visual Studio 2026 output:

```text
out\build\windows-vs2026\bin\Release\ThreadSpire.exe
```

PowerShell does not search the current directory automatically; use the `./` or `.\` prefix.

## Private ML runtime

The GUI creates, installs, verifies, and repairs its private virtual environment automatically at launch. Manual Local AI controls are collapsed by default behind **Show advanced recovery controls**; they are not required setup steps. The internal model requires:

- CUDA-enabled PyTorch
- safetensors

The optional external-model LoRA workflow additionally uses:

- Transformers
- PEFT
- bitsandbytes
- datasets
- Accelerate
- sentencepiece

The installer currently installs the complete set so every retained tab works. Existing compatible Python/PyTorch environments can be selected on **Local AI**.

Materialized backend files:

```text
%LOCALAPPDATA%\ThreadSpire\backend\model_worker.py
%LOCALAPPDATA%\ThreadSpire\backend\internal_model.py
```

Internal weights:

```text
%LOCALAPPDATA%\ThreadSpire\models\internal
```

No PowerShell controller is used at runtime. C++ launches and supervises the worker with Windows process APIs and a Job Object. The lifecycle controller resumes exact model/optimizer/data/RNG checkpoints, publishes an atomic progress heartbeat, retries failed stages, and restarts the model host after an unexpected exit.

## Manual CMake

Visual Studio 2022:

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022-release --parallel
ctest --preset windows-vs2022-release
```

Visual Studio 2026:

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-release --parallel
ctest --preset windows-vs2026-release
```

## Linux core validation

```bash
cmake --preset linux-ninja
cmake --build --preset linux-release --parallel
ctest --preset linux-release
python3 -m py_compile \
    resources/model_worker.py \
    resources/internal_model.py \
    tools/generate_bootstrap_data.py \
    tests/internal_model_smoke.py
python3 tests/internal_model_smoke.py --source-root .
```

Linux does not build the Win32 GUI. It validates the platform-neutral C++23 core, POSIX process/HTTP adapters, Python syntax, and a CPU-only random-model train/preempt/resume/promote cycle.

## Regenerating bootstrap data

```bash
python3 tools/generate_bootstrap_data.py \
    --handcrafted resources/seed.jsonl \
    --output resources/bootstrap.jsonl \
    --anchors resources/bootstrap_anchors.jsonl \
    --anchor-count 320
```

The generator deduplicates normalized messages and removes held-out anchors from the training file.

## Clean rebuild after embedded-resource changes

```powershell
.\build.cmd clean
```

A clean configure is required after changing CMake, the resource template, manifest, worker, internal model, model registry, or embedded datasets.
