# ThreadSpire

ThreadSpire is a local task workbench: describe one outcome, choose a capability, and run it through a compatible local model or tool harness. Text, code help, repository work, MCP tools, image generation, and speech generation share one **Task** interface. Connections, verified learning, detailed runs, and storage are supporting surfaces rather than separate competing products.

ThreadSpire can also run and continuously improve a **locally initialized internal language model**. Learning is isolated from package/model selection: the selected training target changes only when the user changes it, foreground work preempts and checkpoints training, and automatic repair resumes the strongest quality-gated candidate.

The internal model does not load Qwen, Llama, OpenAI, LM Studio, Ollama, or Hugging Face model weights. Its transformer weights are created from random initialization on the user's machine, trained by the private PyTorch worker, evaluated as shadow generations, and promoted only when the candidate passes the configured gates.

Connected local LLM servers remain available as optional bridges, teachers, verifiers, and external-model LoRA targets. They are not the internal model.

## What is included

- One C++23 Win32 GUI: `ThreadSpire.exe`
- Dark native Windows interface, including a dark title bar and dark controls
- Automatic install, verification, exact-resume bootstrap, host startup, crash recovery, and bounded retry
- Live phase, overall progress, verified-token count, elapsed time, measured ETA, heartbeat, and exact failure reporting
- Custom decoder-only transformer implemented in `resources/internal_model.py`
- Deterministic UTF-8 byte tokenizer owned by ThreadSpire
- Per-layer modular skill experts for:
  - English core
  - C++23 core
  - C++23 debugging
  - planning
- Random local initialization with no external base checkpoint
- 3,596 unique bootstrap training records
- 320 disjoint held-out anchors
- Full-weight shadow-candidate training
- Exact-resume checkpoints containing model, optimizer, pending gradients, accumulation phase, data order/cursor, and CPU/CUDA random state
- Inference-priority preemption and automatic resume
- Checksum-verified generation manifests and atomic promotion
- Immediate episodic memory and verified-experience capture
- Optional local teacher/verifier ingestion from LM Studio, Ollama, llama.cpp, LocalAI, Jan, KoboldCpp, and compatible APIs
- English-first, switchable continual-training targets with persistent automatic/manual control
- Trusted custom JSONL import with an embedded template, automatic shadow updates, and manual queue/retry
- Fully dark owner-drawn Win32 tabs, buttons, selectors, edits, progress, and status surfaces
- Optional external-model LoRA and FFN-delta tools retained under the advanced skill workflow
- One capability-aware Task surface for chat, code, repositories, MCP tools, OpenAI-compatible image generation, and speech generation
- Compiler/test-backed repository trajectories with bounded history and rollback
- Live application-data size, file count, growth delta, category totals, generation/run counts, and largest-file reporting

## Internal model profiles

| Profile | Parameters | Context | Intended use |
|---|---:|---:|---|
| Micro | 22,527,360 | 1,024 byte tokens | Fast architecture and continual-learning experiments |
| Standard | 49,962,496 | 1,536 byte tokens | Better local capacity at moderate training cost |
| Large local | 93,586,560 | 2,048 byte tokens | Highest included capacity for the RTX 5080 class |

The stored float32 weights are larger than the executable. Approximate raw weight sizes are 90 MB, 200 MB, and 374 MB respectively, before optimizer checkpoints. The application executable contains the architecture and bootstrap data, not a hidden imported model.

## Build and run on Windows

Requirements:

- Windows 10 or newer
- Visual Studio 2022 or 2026 with **Desktop development with C++**
- CMake 3.30 or newer
- 64-bit Python 3.12 recommended for the private PyTorch runtime
- Git for repository-agent work

From PowerShell in the extracted project directory, build, validate, and launch in one command:

```powershell
.\BUILD_AND_RUN.cmd clean
```

`BUILD_AND_RUN.cmd` detects Visual Studio 2022 or 2026 and launches the generated Release executable. It closes an older running ThreadSpire instance before replacing it. Later incremental rebuilds can use `.\BUILD_AND_RUN.cmd` without `clean`. The lower-level `build.cmd` remains available when building without launching.

Application state is stored under:

```text
%LOCALAPPDATA%\ThreadSpire
```

## Automatic internal-model lifecycle

Launch the application. No manual setup sequence is required. **Automatic setup and recovery** is enabled by default and performs the private-runtime installation, dependency verification, model initialization, exact checkpoint resume, bootstrap evaluation/promotion, and internal-host startup. If a stage fails, it reports the concrete reason and retries with bounded backoff. If the host later exits, it is restarted automatically.

When bootstrap is incomplete, ThreadSpire opens directly on **Connections**. The page shows the current phase, overall bootstrap progress, verified target tokens, elapsed time, measured ETA, and worker heartbeat. A training worker that stops reporting for 15 minutes is terminated and resumed from its exact checkpoint. The ETA is derived from measured completed steps rather than a fabricated fixed duration.

Existing partial generations are preserved. Micro bootstrap now targets 12,000 cumulative steps by default (18,000 for Standard and 25,000 for Large) instead of stopping at the first undertrained checkpoint. Repair selects the strongest loss-gated descendant, decays its learning rate across cumulative progress, checkpoints exact state, and keeps the active generation untouched until held-out loss and generation canaries both pass.

The manual controls are collapsed by default behind **Show advanced recovery controls** and remain available only for deliberate recovery and diagnostics. **Stop** disables automatic recovery so the host does not immediately restart against the user's instruction.

The internal artifacts are stored at:

```text
%LOCALAPPDATA%\ThreadSpire\models\internal
├── active.json
├── generations
│   ├── g000000
│   └── g000001
└── checkpoints
```

Live lifecycle state is written atomically to:

```text
%LOCALAPPDATA%\ThreadSpire\state\internal-model-progress.json
```

Each generation records:

- origin: `random-initialized-local-model`
- `external_base_weights: false`
- parameter count
- cumulative training steps and verified target tokens
- bootstrap target and completion state
- validation loss
- training scope
- SafeTensors checksum
- parent generation

## Internal model versus connected local LLM

The AI source selector is authoritative:

- **ThreadSpire internal model** — uses only ThreadSpire's active local generation.
- **Connected local LLM** — sends the request to a selected LM Studio, Ollama, llama.cpp, or compatible server.
- **Automatic** — may select any ready source according to routing policy.

A connected model can be marked as a teacher or verifier. Teacher output becomes candidate data only; it never becomes the internal inference model directly. Unverified generated examples remain quarantined. Compiler/test-confirmed results, explicit user corrections, and accepted answers can become verified internal training evidence.

## Continuous learning

ThreadSpire learns at three speeds:

1. **Immediate context and memory** — the most recent conversation turns are passed directly to inference, while accepted durable context is retrievable without changing weights.
2. **Internal skill updates** — verified examples train only the selected internal skill expert or, for bootstrap, the whole model.
3. **Optional external-model LoRA** — retained as an advanced workflow for imported models, not used by the internal source.

The active generation is never trained in place. Training starts from the active parent, writes resumable shadow state, evaluates held-out anchors, and atomically advances `active.json` only when the candidate passes. A stale or failed candidate cannot overwrite the active model.

Foreground inference has priority. Training checkpoints and yields when an inference lease appears, then resumes from the exact saved optimizer/data/RNG state.

For the ThreadSpire internal model, each newly admitted independently verified episode is enough to queue a scoped shadow-weight update. Multiple examples for the same skill coalesce behind the active job instead of launching duplicate trainers. The automatic teacher loop waits for that queue to drain, then continues while the machine is idle.

The **Learning** page exposes the actual pipeline rather than only the evidence collector: pending evidence per skill, queued weight updates, the active candidate, trainer phase and step, loss, verified-token count, elapsed time, measured ETA, progress-file path, and the live trainer-log tail.

## Internal-output quality boundary

The Micro model is intentionally small and begins undertrained. ThreadSpire therefore applies exact byte n-gram blocking during generation and a conservative structural gate before an internal draft reaches the UI. A repetitive or structurally corrupt draft is retried once with greedy decoding. If it still fails, the draft is discarded as `low_confidence`; Automatic source mode can move the request to another eligible connected local model.

This gate blocks obvious loops and corruption. It does not prove factual correctness. A connected mature local LLM remains the stronger interactive source and teacher while the private internal generation accumulates verified training.

## Bootstrap corpus

`resources/bootstrap.jsonl` contains 3,596 unique training records spanning:

- English conversation and instruction following
- ordinary profanity without irrelevant refusal
- grammar correction and paraphrasing
- exact bounded reasoning
- deterministic runtime identity
- multilingual text-to-English translation
- C++23 declarations and functions
- ownership, lifetime, RAII, concepts, ranges, expected, and jthread
- compiler diagnostics, undefined behavior, and repair
- architecture, CMake, tests, and long-horizon trajectories

`resources/bootstrap_anchors.jsonl` contains 320 message-disjoint held-out records. Anchor messages are not copied into the training file.

This corpus is enough to validate the architecture and begin narrow learning. It is **not enough to make a randomly initialized 22.5M-parameter model equal a mature pretrained model**. Continued verified experience and optional local-teacher distillation are the intended growth path.

## Interface

1. **Task** — choose Ask/write, Code help, Repository task, Generate image, Generate speech, or Use tools; then select a compatible source/model and run it from the same prompt/output surface.
2. **Connections** — local-server discovery, capability declarations, MCP, internal-model lifecycle/progress, and advanced recovery controls.
3. **Learning** — persistent target selection, automatic/manual verified evidence, trusted JSONL import, shadow updates, and live training status.
4. **Skills** — internal skill generations, optional LoRA candidates, promotion, and rollback.
5. **Run Details** — advanced build/test commands and progress for repository tasks started from Task.
6. **Storage & Logs** — live disk accounting plus backend, compiler, training, and trajectory logs.

Manual endpoints declare comma-separated capabilities such as `chat`, `code`, `repository`, `tools`, `image`, `speech`, or `audio`. The Task selector filters models by those capabilities. Image requests use `/v1/images/generations`; speech requests use `/v1/audio/speech`. Generated files are saved under `%LOCALAPPDATA%\ThreadSpire\outputs` and can be opened directly from Task.

## Storage limits

The **Storage & Logs** page refreshes in the background every five seconds and reports exact bytes and file counts without blocking the UI. Automatic retention applies only to recoverable ThreadSpire-owned artifacts:

- active/internal repair history retains the active lineage and recent candidates (normally four protected/current generations)
- repository trajectories retain at most 20 runs or 2 GiB
- generated images and generated audio each retain at most 100 recent files or 2 GiB
- append-only backend logs rotate at 16 MiB and retain one previous file

The private Python/PyTorch runtime is shown separately because it is large but normally stable; it is not repeatedly copied for each model generation.

## Validation

Platform-neutral validation commands:

```bash
cmake --preset linux-ninja
cmake --build --preset linux-release --parallel
ctest --preset linux-release
python3 -m py_compile resources/model_worker.py resources/internal_model.py
python3 tests/internal_model_smoke.py --source-root .
```

The Python smoke test creates a tiny random model, preempts training, resumes from its exact checkpoint, evaluates the candidate, promotes it, and verifies that no external base weights are present.

See [CUSTOM_TRAINING.md](CUSTOM_TRAINING.md), [ARCHITECTURE.md](ARCHITECTURE.md), [BUILDING.md](BUILDING.md), [docs/LEARNING_PIPELINE.md](docs/LEARNING_PIPELINE.md), and [docs/MODULE_ABI.md](docs/MODULE_ABI.md).
