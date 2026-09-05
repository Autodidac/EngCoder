# Building EngCoder

## Windows target

Requirements:

- Windows 10 or newer
- Visual Studio 2022 with **Desktop development with C++**
- Windows 10/11 SDK
- CMake 4.4 or newer
- Git for repository-agent work

Python, PyTorch, Transformers, PEFT, and media engines are not base build or runtime dependencies.

Run a clean Visual Studio 2022 release build and its tests from the repository root:

```powershell
cmake --fresh --preset windows-vs2022
cmake --build --preset windows-vs2022-release --clean-first -- /m:1
ctest --preset windows-vs2022-release
```

The executable is written to:

```text
out\build\windows-vs2022\bin\Release\EngCoder.exe
```

Install the base into a clean staging directory, then enforce the 100 MiB ceiling:

```powershell
cmake --install out\build\windows-vs2022 --config Release --prefix out\install\windows-vs2022
cmake -DTHREADSPIRE_BASE_ROOT=out\install\windows-vs2022 -P tools\check_base_footprint.cmake
```

EngCoder stores live application data under `%LOCALAPPDATA%\EngCoder`. The obsolete `%LOCALAPPDATA%\ThreadSpire` directory is not recreated.

The base release installs `EngCoder.exe`, the bundled compact native inference helper under its compatibility `optional-packs` path, and the required EngCoder/EpochGui license notices. The compact helper is part of the measured base and contains no neural weights. User-installed model weights and heavyweight training or media capability packs remain outside the base release and footprint measurement. The base does not materialize Python scripts or install a private Python runtime.

## C++23 modules

CMake 4.4 `FILE_SET CXX_MODULES` publishes these named modules:

- `threadspire.core`
- `threadspire.platform`
- `threadspire.storage`
- `threadspire.models`
- `threadspire.providers`
- `threadspire.tasks`
- `threadspire.scheduler`
- `threadspire.learning`
- `threadspire.media`
- `threadspire.mcp`
- `threadspire.ui`

Implementation units and native tests consume the interfaces with `import`. Windows SDK and third-party headers stay in global module fragments or platform implementation units.

## Capability packs

Heavy functionality is separated into capability packs. EngCoder manages the small built-in answer engine automatically; built-in learning setup and repair live under **AI & Tools**; advanced fine-tuning, image, and audio add-ons are added explicitly under **Capabilities**. Each pack is staged and verified before an atomic commit; cancellation or failure restores the prior pack. Adding an optional pack never starts training, selects a model, starts a host, or interrupts a task.

Supported pack classes are:

- compact native inference
- internal PyTorch training
- optional Transformers/PEFT fine-tuning (dependent on the training pack)
- optional image engine
- optional audio engine

A prepared pack source must contain every file declared for that pack; EngCoder writes the installed `pack.json` only after validation and an atomic commit. Python is needed only inside packs that declare it. Internal training payloads include `model_worker.py`, `internal_model.py`, `curriculum_guard.py`, and `curriculum_compiler.py`; these files are source assets for pack assembly and are not embedded in the base executable.

On a Windows training machine, prepare that pack without touching the base install:

```powershell
.\tools\prepare_internal_training_pack.ps1
```

The script creates an isolated Python runtime, installs PyTorch, NumPy, and Safetensors, runs the backend doctor, exercises NumPy, performs a Safetensors write/read round trip, and—unless CPU smoke-test mode was explicitly allowed—executes and synchronizes CUDA tensor math. It records the exact installed dependency versions and only then atomically places the completed payload in EngCoder's `pack-inbox`. It downloads no model weights. Pass `-Wheelhouse` for an offline, controlled wheel source. `-AllowCpuTraining` is intended only for slow smoke tests; normal internal training requires CUDA.

To select a CUDA-specific PyTorch channel, pass a trusted index explicitly:

```powershell
.\tools\prepare_internal_training_pack.ps1 `
  -TorchIndexUrl https://download.pytorch.org/whl/cu132
```

The `cu132` channel above is the one exercised for this release on the target RTX 5080; select the official channel compatible with the target driver when rebuilding elsewhere. `-TorchIndexUrl` accepts only an HTTPS default-port URL beneath `download.pytorch.org/whl/`, rejects queries and fragments, and cannot be combined with `-Wheelhouse`. NumPy and Safetensors still come from the normal Python index; only PyTorch is constrained to the selected trusted CUDA index. Pack preparation fails before the atomic commit if dependency import, NumPy operation, Safetensors serialization, or required CUDA operation fails.

Normal builds never migrate, quarantine, or rewrite `%LOCALAPPDATA%\EngCoder`. The legacy English-foundation migration is an explicit maintenance action under `tools`; run it only when intentionally upgrading old application data.

## Validation

The CTest suite covers module consumption, unified routing and multimodal providers, cancellation and artifacts, transactional pack rollback, model-selection isolation, durable training resume/coalescing, atomic candidate-host promotion, storage accounting/cleanup, reset-path validation, the English Gate, and the base footprint.

Optional Python validation for pack maintainers:

```powershell
python -m py_compile resources\model_worker.py resources\internal_model.py resources\curriculum_guard.py resources\curriculum_compiler.py
python tests\curriculum_compiler_smoke.py --source-root .
python tests\curriculum_guard_smoke.py --source-root .
python tests\internal_model_smoke.py --source-root .
python tests\internal_server_smoke.py --source-root .
```

The internal-model and internal-server smoke tests require a compatible PyTorch/Safetensors environment. All four smoke tests use temporary state and do not modify the selected application model.

## Guarded application-state reset

A release reset is permitted only after the replacement release build, CTest, smoke tests, and footprint check all pass. Validate first:

```powershell
.\tools\reset_engcoder_appdata.ps1 `
  -Target "$env:LOCALAPPDATA\EngCoder" `
  -ReplacementVersion 0.9.1 `
  -ValidateOnly
```

The destructive invocation accepts only the exact resolved `%LOCALAPPDATA%\EngCoder` directory, rejects reparse roots and escaped links, stops only EngCoder-owned processes, writes a dated size/version/deletion manifest beside the directory, and performs a no-follow deletion. It must never be pointed at a source checkout.

## Linux core validation

```bash
cmake --fresh --preset linux-ninja
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

Linux validates the platform-neutral module library and POSIX adapters. The Win32 GUI target is Windows-only.

## Regenerating bootstrap data

```bash
python3 tools/generate_bootstrap_data.py \
    --handcrafted resources/seed.jsonl \
    --output resources/bootstrap.jsonl \
    --anchors resources/bootstrap_anchors.jsonl \
    --anchor-count 320
```

The generator keeps provenance, deduplicates normalized messages, and excludes deterministic held-out anchors from the training file. Generated or teacher-authored text remains untrusted until an independent verifier admits it.
