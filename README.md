# EngCoder

EngCoder is a local task workbench: describe one outcome and run it through a compatible local AI or tool. Text, code help, optional repository work, optional tools, image generation, and speech generation share one **Chat** surface. Chat always shows which AI is handling requests and whether the built-in AI is ready, starting, or learning automatically.

EngCoder can also run and continuously improve a **locally initialized built-in AI**. The native base application does not require Python or PyTorch. When the optional built-in AI files are available, EngCoder installs them transactionally, checks them, creates or resumes the model, completes the bounded learning target, and starts it automatically while yielding to active tasks.

The internal model does not load Qwen, Llama, OpenAI, LM Studio, Ollama, or Hugging Face model weights. Its transformer weights are created from random initialization on the user's machine, trained by the private PyTorch worker, evaluated as shadow generations, and promoted only when the candidate passes the configured gates.

Connected local LLM servers remain available as optional bridges, teachers, verifiers, and external-model LoRA targets. They are not the internal model.

## What is included

- One C++23 Win32 GUI: `EngCoder.exe`
- Dark native Windows interface, including a dark title bar and dark controls
- Eleven named C++23 modules built through CMake `FILE_SET CXX_MODULES`
- Native base executable with no Python/PyTorch runtime dependency and a 100 MiB installed-size ceiling
- Transactional optional AI packages with automatic setup and backup-only repair controls
- Live phase, overall progress, verified-token count, elapsed time, measured ETA, heartbeat, and exact failure reporting
- Optional custom decoder-only transformer supplied by the internal-training pack
- Deterministic UTF-8 byte tokenizer owned by EngCoder
- Training-only emoji removal and provider-persona normalization, with exact cleaned text visible before approval
- Per-layer modular skill experts for:
  - English core
  - C++23 core
  - C++23 debugging
  - planning
- Random local initialization with no external base checkpoint
- 4,247 unique bootstrap training records
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
| Small | 22,527,360 | 1,024 byte tokens | Fastest setup and narrow continual learning |
| Medium | 49,962,496 | 1,536 byte tokens | Balanced local capacity and training cost |
| Large | 93,586,560 | 2,048 byte tokens | Highest included capacity for the RTX 5080 class |

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

`BUILD_AND_RUN.cmd` detects Visual Studio 2022 or 2026 and launches the generated Release executable. It closes an older running EngCoder instance before replacing it. Later incremental rebuilds can use `.\BUILD_AND_RUN.cmd` without `clean`. The lower-level `build.cmd` remains available when building without launching.

Application state is stored under:

```text
%LOCALAPPDATA%\ThreadSpire
```

The legacy directory name is intentionally retained so EngCoder can reuse existing approved models, chats, memory, queues, and learning history. It is a compatibility path, not the product name.

## Automatic built-in AI setup

Launch immediately starts Chat, discovers compatible AI apps, and coordinates the built-in AI in the background. A complete optional setup payload supplied beside the application or in EngCoder's setup inbox is discovered automatically. Its files are staged, checked, committed atomically, and rolled back on cancellation or failure. The file transaction never changes the chosen AI or interrupts a task; the idle-safe coordinator performs later setup steps separately.

After the optional files are ready, EngCoder automatically checks them, creates or resumes the built-in AI, and starts it after all quality checks pass. Training is a durable serialized queue with `queued`, `preparing`, `training`, `evaluating`, `promoting`, `rejected`, `completed`, and `recoverable-failed` states. Duplicate jobs coalesce, interrupted work resumes from an atomic checkpoint, and anything the user is doing has priority.

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

The two AI selectors are authoritative:

- **Use AIs from** chooses the allowed group: **Anywhere — automatic backup (default)**, **Other AI apps only**, or **Built-in EngCoder only**.
- **Choose AI** can leave EngCoder free to choose inside that group or lock one exact AI.

Automatic backup occurs only with the default Anywhere group and no exact AI selected. Choosing a specific AI contacts that AI once; a failure is shown cleanly and no other model is contacted. Choosing **Built-in EngCoder only** lets EngCoder choose among its built-in lanes without ever contacting an outside model. Chat always shows connected-AI availability and built-in training/readiness without changing tabs. Setup, recovery, learning, and startup yield to active work and never change these selectors.

A connected model can be marked as a teacher or verifier. Teacher output becomes candidate data only; it never becomes the internal inference model directly. Unverified generated examples remain quarantined. Compiler/test-confirmed results, explicit user corrections, and accepted answers can become verified internal training evidence.

## Continuous learning

EngCoder learns at three speeds:

1. **Immediate context and memory** — recent turns are passed directly to inference. Explicit personal facts and preferences live in an editable permanent user-memory store that is never weight training.
2. **Internal skill updates** — verified examples may train only `english-core`, `english-foundation`, `cpp23-core`, `cpp23-debugging`, or `planning`; all other subjects remain retrieval memory.
3. **Optional external-model LoRA** — retained as an advanced workflow for imported models, not used by the internal source.

Plain-language learning and memory use the same composer and do not contact an AI provider. Examples: `Remember that I prefer dark interfaces`, `What do you remember about me?`, `Forget dark interfaces`, `Teach EngCoder: when I say "hello", answer "Hello!"`, and `Correct that to "the answer you want"`. EngCoder prints an exact local receipt showing the stored user message, expected reply or memory item, cleanup changes, immediate-recall state, shadow-training eligibility, and `AI model contacted: NO`.

A hard correction makes the replacement answer authoritative immediately, preserves the rejected draft only as negative audit evidence, and removes every stale positive version of that prompt from later C++ and Python retrieval/training loads. Repeated approval does not create extra weight. Error cards and deterministic calculations cannot be selected as training answers. Per-reply **Copy**, **Edit**, **Retry**, **Accept + learn**, **Reject**, and **Learn?** controls remain the manual backup.

Connected teachers can generate diverse examples in parallel with up to four workers per teacher. A different model verifies each attempt; accepted, rejected, and failed attempts are all retained in `teacher-attempts.jsonl` and visible in the training library. The internal model never teaches or verifies itself.

The active generation is never trained in place. Training starts from the active parent, writes resumable shadow state, evaluates held-out anchors, and atomically advances `active.json` only when the candidate passes. A stale or failed candidate cannot overwrite the active model.

Foreground inference has priority. Training checkpoints and yields when an inference lease appears, then resumes from the exact saved optimizer/data/RNG state.

For the EngCoder internal model, each newly admitted independently verified episode can queue a scoped shadow-weight update when learning is enabled. Multiple examples for the same skill coalesce behind the active job instead of launching duplicate trainers. Package selection remains isolated from this queue.

The **Learning** page exposes the actual pipeline rather than only the evidence collector: pending evidence per skill, queued weight updates, the active candidate, trainer phase and step, loss, verified-token count, elapsed time, measured ETA, progress-file path, and the live trainer-log tail.

Automatic improvement is a persistent, bounded session—not an unlimited background switch. A fresh state starts with a visible 12-hour maximum; shorter 1, 4, or 8-hour budgets are available. Pause and restart preserve the exact remaining time, End remains ended after reopening, Chat preempts learning, and all transitions retain approved evidence and safe checkpoints without replacing the working model. Manual example-source controls are hidden behind **Show backup example controls**; the default page keeps session state, permanent memory, current activity, and the complete searchable Training Library visible.

## Internal-output quality boundary

The Small model is intentionally compact and begins undertrained. EngCoder therefore applies exact byte n-gram blocking during generation and a conservative structural gate before an internal draft reaches the UI. A repetitive or structurally corrupt draft is retried once with greedy decoding. If it still fails, the draft is discarded as `low_confidence`; Automatic source mode can move the request to another eligible connected local model.

Before neural drafting, the local host performs only independently recomputable operations such as case conversion, bounded arithmetic, exact-format copying, and grounded extraction. It may also return an accepted curated answer when the local prompt match is exact or high-confidence. These results still pass the same live gate; unmatched malformed neural text remains `low_confidence`.

This gate blocks obvious loops and corruption. It does not prove factual correctness. A connected mature local LLM remains the stronger interactive source and teacher while the private internal generation accumulates verified training.

## Bootstrap corpus

`resources/bootstrap.jsonl` contains 4,247 unique training records spanning:

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

1. **Workspace** — one persistent conversation and composer for writing, questions, supporting-language code, repository work, image generation, audio/music generation, tools, files, streaming, Stop, and per-reply review. EngCoder infers the action automatically; the compact Action selector beside Send is an override.
2. **Settings** — secondary pages for **AI & Tools**, **Memory & Learning**, and **Capabilities**. Automatic readiness stays primary; manual addresses, repair controls, training inspection, permanent memory, and optional add-ons remain available without becoming separate top-level applications.
3. **Activity** — eight live storage/RAM cards, exact model and pack accounting, protected cleanup preview/execution, outputs, and backend/compiler/training/trajectory logs.

Backup-only AI connection entries can declare what an AI can do, such as `chat`, `code`, `repository`, `tools`, `image`, `speech`, or `audio`. The Workspace Action selector filters models by those capabilities when used; Automatic infers the required capability from the message. Image requests use `/v1/images/generations`; speech requests use `/v1/audio/speech`. Generated files are saved under `%LOCALAPPDATA%\ThreadSpire\outputs` and can be opened directly from Chat.

## Exact optional add-on audit

**Settings → Capabilities → Show exact contents** performs an explicit, cancellation-aware audit of the selected installed add-on without following directory links. It enumerates every file and directory, measures actual bytes, distinguishes model weights from native runtimes, Python code, dependency metadata/licenses, and other data, and attributes files through installed Python `METADATA` and `RECORD` manifests when present. Required add-ons are compared only at matching paths and reported as redundant only after a byte-for-byte match.

The report also shows each add-on's purpose, declared origin/license, loading condition, required add-ons, and whole-pack safe-removal rule. Catalog projections are not presented as installed content: an uninstalled add-on always reports **0 installed files and 0 model weights**. Selecting, installing, or auditing an add-on never starts training, changes the selected AI, or interrupts Chat.
## Storage limits

**Activity** keeps eight dark live cards in view: **Current Model**, **All Model Versions**, **Accepted Training Data**, **Installed Packs**, **Outputs / Cache**, **Total Usage** (including growth since the previous sample), **Free Disk**, and **Live RAM**. It also reports exact active-model contents, EngCoder-owned helper memory, largest files/directories, projected pack download/install sizes, category quotas, and dry-run cleanup without cluttering Chat.

Cleanup is restricted to no-follow EngCoder-owned logs, unpromoted candidates, checkpoints, outputs, and caches. Active model lineage and paths outside the application root are protected. The base installed tree excludes models and optional packs and must remain at or below 100 MiB; large PyTorch dependencies belong in an explicit pack and are reported separately.

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
