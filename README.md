# ThreadSpire

ThreadSpire is a local task workbench: describe one outcome and run it through a compatible local AI or tool. Text, code help, optional repository work, optional tools, image generation, and speech generation share one **Chat** surface. Chat always shows which AI is handling requests and whether the built-in AI is ready, starting, or learning automatically.

ThreadSpire can also run and continuously improve a **locally initialized built-in AI**. The native base application does not require Python or PyTorch. When the optional built-in AI files are available, ThreadSpire installs them transactionally, checks them, creates or resumes the model, completes the bounded learning target, and starts it automatically while yielding to active tasks.

The internal model does not load Qwen, Llama, OpenAI, LM Studio, Ollama, or Hugging Face model weights. Its transformer weights are created from random initialization on the user's machine, trained by the private PyTorch worker, evaluated as shadow generations, and promoted only when the candidate passes the configured gates.

Connected local LLM servers remain available as optional bridges, teachers, verifiers, and external-model LoRA targets. They are not the internal model.

## What is included

- One C++23 Win32 GUI: `ThreadSpire.exe`
- Dark native Windows interface, including a dark title bar and dark controls
- Eleven named C++23 modules built through CMake `FILE_SET CXX_MODULES`
- Native base executable with no Python/PyTorch runtime dependency and a 100 MiB installed-size ceiling
- Transactional optional AI packages with automatic setup and backup-only repair controls
- Live phase, overall progress, verified-token count, elapsed time, measured ETA, heartbeat, and exact failure reporting
- Optional custom decoder-only transformer supplied by the internal-training pack
- Deterministic UTF-8 byte tokenizer owned by ThreadSpire
- Per-layer modular skill experts for:
  - English core
  - C++23 core
  - C++23 debugging
  - planning
- Random local initialization with no external base checkpoint
- 3,632 unique bootstrap training records
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
- One capability-aware Chat surface for text, code, optional repositories, MCP tools, OpenAI-compatible image generation, and speech generation
- Compiler/test-backed repository trajectories with bounded history and rollback
- Six live storage cards, growth deltas, largest-file/directory reporting, pack-size projections, quotas, and safe cleanup

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
- Git for repository-agent work

Python is not required to build, install, or run the base workbench. It is required only by capability packs that explicitly declare it.

From PowerShell in the extracted project directory, build, validate, and launch in one command:

```powershell
.\BUILD_AND_RUN.cmd clean
```

`BUILD_AND_RUN.cmd` detects Visual Studio 2022 or 2026 and launches the generated Release executable. It closes an older running ThreadSpire instance before replacing it. Later incremental rebuilds can use `.\BUILD_AND_RUN.cmd` without `clean`. The lower-level `build.cmd` remains available when building without launching.

Application state is stored under:

```text
%LOCALAPPDATA%\ThreadSpire
```

## Automatic built-in AI setup

Launch immediately starts Chat, discovers compatible AI apps, and coordinates the built-in AI in the background. A complete optional setup payload supplied beside the application or in ThreadSpire's setup inbox is discovered automatically. Its files are staged, checked, committed atomically, and rolled back on cancellation or failure. The file transaction never changes the chosen AI or interrupts a task; the idle-safe coordinator performs later setup steps separately.

After the optional files are ready, ThreadSpire automatically checks them, creates or resumes the built-in AI, and starts it after all quality checks pass. Training is a durable serialized queue with `queued`, `preparing`, `training`, `evaluating`, `promoting`, `rejected`, `completed`, and `recoverable-failed` states. Duplicate jobs coalesce, interrupted work resumes from an atomic checkpoint, and anything the user is doing has priority.

New automatic setups choose the smallest profile and its 24,000-step target. Existing Medium and Large models retain their 36,000- and 50,000-step targets. Each target stops boundedly at its cap with diagnostics. Curated seeds retain provenance, normalized records are deduplicated, validation is deterministic and disjoint, and an independent verifier—not the teacher model—controls admission and promotion.

A shadow candidate is served by its own isolated host. The working host and `active.json` remain untouched until checksum, health, inference-canary, deterministic-validation, and regression checks all pass and a compare-and-swap promotion commits the candidate.

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

- **Built-in AI first (safe backup)** — tries a fully approved ThreadSpire model first and automatically uses a connected AI if the live answer is rejected.
- **Another AI app only** — uses a selected compatible AI app running on the computer.
- **Automatic** — uses the fastest ready AI that fits the task.

At launch, an unavailable built-in preference falls back to Automatic instead of hiding another working AI app. Chat shows both connected-AI availability and built-in-AI progress without switching tabs. Automatic setup, recovery, learning, and startup yield to active work; training never changes the selected model, and a rejected built-in draft never reaches the conversation.

A connected model can be marked as a teacher or verifier. Teacher output becomes candidate data only; it never becomes the internal inference model directly. Unverified generated examples remain quarantined. Compiler/test-confirmed results, explicit user corrections, and accepted answers can become verified internal training evidence.

## Continuous learning

ThreadSpire learns at three speeds:

1. **Immediate context and memory** — the most recent conversation turns are passed directly to inference, while accepted durable context is retrievable without changing weights.
2. **Internal skill updates** — verified examples train only the selected internal skill expert or, for bootstrap, the whole model.
3. **Optional external-model LoRA** — retained as an advanced workflow for imported models, not used by the internal source.

The active generation is never trained in place. Training starts from the active parent, writes resumable shadow state, evaluates held-out anchors, and atomically advances `active.json` only when the candidate passes. A stale or failed candidate cannot overwrite the active model.

Foreground inference has priority. Training checkpoints and yields when an inference lease appears, then resumes from the exact saved optimizer/data/RNG state.

For the ThreadSpire internal model, each newly admitted independently verified episode can queue a scoped shadow-weight update when learning is enabled. Multiple examples for the same skill coalesce behind the active job instead of launching duplicate trainers. Package selection remains isolated from this queue.

The **Learning** page exposes the actual pipeline rather than only the evidence collector: pending evidence per skill, queued weight updates, the active candidate, trainer phase and step, loss, verified-token count, elapsed time, measured ETA, progress-file path, and the live trainer-log tail.

## Internal-output quality boundary

The Micro model is intentionally small and begins undertrained. ThreadSpire therefore applies exact byte n-gram blocking during generation and a conservative structural gate before an internal draft reaches the UI. A repetitive or structurally corrupt draft is retried once with greedy decoding. If it still fails, the draft is discarded as `low_confidence`; Automatic source mode can move the request to another eligible connected local model.

Before neural drafting, the local host performs only independently recomputable operations such as case conversion, bounded arithmetic, exact-format copying, and grounded extraction. It may also return an accepted curated answer when the local prompt match is exact or high-confidence. These results still pass the same live gate; unmatched malformed neural text remains `low_confidence`.

This gate blocks obvious loops and corruption. It does not prove factual correctness. A connected mature local LLM remains the stronger interactive source and teacher while the private internal generation accumulates verified training.

## Bootstrap corpus

`resources/bootstrap.jsonl` contains 4,239 unique training records spanning:

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

1. **Chat** — choose Ask/write, Code help, optional Repository task, Generate image, Generate speech, or Use tools; then select a compatible source/model and run it from the same prompt/output surface. Ordinary use does not require a repository.
2. **AI Setup** — a plain automatic readiness view; manual addresses, optional tools, file replacement, checks, rebuilding, and saved-list editing stay hidden under **Backup and repair options**.
3. **Learning** — automatic by default, with target choice, trusted-file import, and recovery controls available as backup.
4. **Skills** — internal skill generations, optional LoRA candidates, promotion, and rollback.
5. **Storage & Logs** — the six live storage cards, detailed disk accounting, and backend/compiler/training/trajectory logs.

Backup-only AI connection entries can declare what an AI can do, such as `chat`, `code`, `repository`, `tools`, `image`, `speech`, or `audio`. The Chat task selector filters models by those capabilities. Image requests use `/v1/images/generations`; speech requests use `/v1/audio/speech`. Generated files are saved under `%LOCALAPPDATA%\ThreadSpire\outputs` and can be opened directly from Chat.

## Storage limits

**Storage & Logs** keeps six dark live cards in view: **Current Model**, **Accepted Training Data**, **Installed Packs**, **Outputs / Cache**, **Total Usage** (including growth since the previous sample), and **Free Disk**. It also reports largest files/directories, projected pack download/install sizes, and category quotas without cluttering Chat.

Cleanup is restricted to no-follow ThreadSpire-owned logs, unpromoted candidates, checkpoints, outputs, and caches. Active model lineage and paths outside the application root are protected. The base installed tree excludes models and optional packs and must remain at or below 100 MiB; large PyTorch dependencies belong in an explicit pack and are reported separately.

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
