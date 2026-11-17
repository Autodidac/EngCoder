# EngCoder

EngCoder is a local-first English and C++23 assistant with a shared Windows desktop and browser interface. Architect routes requests and chooses tools. KeyMaker provides language, reasoning, and optional capabilities for C++23, documentation, and planning.

[Windows downloads](https://epoch.adamrushford.chatgpt.site/projects/engcoder#releases) · [Build from source](#build) · [Model setup](docs/MODEL_LIBRARY.md) · [Mission cache](docs/FUTURE_MISSIONS.md)

This guide covers the 0.7.5 development build. See the downloads page for the latest packaged release.

## Get started

1. Extract the Windows download and run `bin/EngCoder.exe`, or [build from source](#build).
2. In **EngCoder Lab → Models**, choose an existing local model or configure a local inference provider. If the GGUF files are already on disk, use **Use existing GGUF…** on the model card and paste the full path to a file or shard.
3. Open **Run info** on a reply to see which model answered, which tools ran, and what EngCoder verified.

See the [release history](CHANGELOG.md) for changes by version and [validation](VALIDATION.md) for test results.

## Engine inference endpoints

EngCoder serves its UI and engine-facing APIs from the same fixed runtime on port `14321`. The interfaces are intentionally separate so game/engine code can choose the amount of EngCoder behavior it needs:

- `http://127.0.0.1:14321/ui/` — EngCoder Web/Desktop UI.
- `GET http://127.0.0.1:14321/v1/models` — OpenAI-style model discovery. `engcoder-foundation` is a stable alias for the currently selected Foundation model.
- `POST http://127.0.0.1:14321/v1/chat/completions` — direct Foundation inference with OpenAI Chat Completions request/response shapes. This bypasses the agent loop.
- `POST http://127.0.0.1:14321/v1/responses` — direct Foundation inference with basic Responses-style input/output. This also bypasses the agent loop.
- `POST http://127.0.0.1:14321/api/tasks` — full EngCoder agent execution: Architect routing, KeyMaker, addons, tools, memory, approvals, verification and session persistence. Poll `/api/sessions/{session_id}/events?after=<sequence>` for progress/results.

`stream=true` on `/v1/chat/completions` emits OpenAI-compatible SSE. `/v1/responses` remains deliberately non-streaming and rejects `stream=true`; the full agent API also exposes incremental model/task events through the session event endpoint. LAN access to every API remains disabled until **Serve remotely** is enabled in EngCoder Lab.

MCP is deliberately not advertised as an inference endpoint. EngCoder can call configured MCP integrations as tools, but this build does not expose EngCoder itself as an MCP server at `/mcp`.

## Architecture

**Architect** is the System 1 controller. It handles routing, retrieval, capability selection, escalation, and stopping. The native runtime enforces tool policy and verifies results. **KeyMaker** is EngCoder's local model and training family. The Foundation handles English and reasoning, and the addons specialize in tasks such as C++23. A configured external model can fill the Foundation role while keeping its weights separate.

```text
User
  |
  v
Architect
  |  route / retrieve / choose capability / verify / stop
  v
KeyMaker Foundation ----------------------------------+
  |                                                    |
  +--> C++23 addon                                     |
  +--> Documentation addon                             |
  +--> Planning addon                                  |
  +--> Tool-use addon                                  |
  +--> Context reranker                                |
  |                                                    |
  +--> deterministic C++ tools + policy ---------------+
  |
  v
Answer / verified action

Learning:
verified evidence -> immediate memory
                 -> candidate training -> evaluation -> promote/reject -> rollback

Physical model placement:
NVMe -> system RAM warm cache -> GPU VRAM hot set
                            pinned: Architect + KeyMaker Foundation
                            pageable: capability addons
```

EngCoder now supports both forms without conflating them: internal KeyMaker capability modularity remains lane/component based, while `qwenflash://` foundation profiles can use a true sparse/MoE GGUF runtime with a bounded VRAM expert cache. Architect, tools, memory, verification, addons, and context policy remain above both model implementations.

### Related Epoch terminology

In Epoch's AI design, **DAVID** (Dynamic Agent Verification and Intelligence Development) is the umbrella framework for learning and verification. **Architect 1** handles fast routing and orchestration, the role most directly shared with EngCoder. **ELA** (Experiential Learning Architecture) is a planned predictive component that would help Architect 1 choose actions. **ADAM** (Adaptive Dynamics and Associative Memory) is the neuromorphic research role. One proposed use is to give background characters cheaper behaviors when a scene nears its processing budget, while keeping those characters in the world.

EngCoder is a separate application. Its model training and evaluation work overlaps with Epoch's **KeyMaker Trainer**, the planned small-model trainer for ELA, ADAM, and **NEO** (Neural Evolution Operator). NEO is Epoch's planned English and C++ model role. EngCoder uses KeyMaker more broadly for its Foundation, addons, and training. DAVID, ELA, ADAM, and NEO describe Epoch's design rather than installed EngCoder components.

## Default local model target

The `desktop-16gb` scratch profile targets a single 16 GiB GPU:

- ~359M total parameters in the current internal generation layout.
- ~246M shared/foundation parameters.
- ~113M parameters across modular learned lanes.
- Architect and KeyMaker Foundation remain pinned for foreground inference.
- Domain addons remain warm in system RAM and page into VRAM on demand.
- Default EngCoder VRAM budget: 13.5 GiB.
- Default maximum simultaneously hot addons: 3.

The generation artifact remains rollback-safe while the runtime exposes components independently and the worker physically pages addon FFNs rather than uploading the entire model first.

## Runtime topology

```text
EngCoder.exe / EngCoderCLI.exe
        |
        +-- RuntimeHost (in process)
             +-- Architect controller/harness
             +-- KeyMaker Foundation
             +-- AddonRegistry
             +-- ModelRegistry
             +-- ModelResidencyManager
             +-- InternalModelHost
             +-- LearningService
             +-- TrainingService
             +-- deterministic tools/review policy
             +-- SQLite sessions/events
             +-- checkpoints/workspace safety

Serving/UI:
  EngCoder.exe       -> owns RuntimeHost + HTTP server + Desktop app window
  Browser            -> opens the same served Web frontend
  Serve remotely     -> optionally exposes that same runtime/UI to the trusted LAN
  EngCoderServer.exe -> headless form of the same runtime + HTTP server
```

EngCoder serves one canonical `projects/web` application. The Windows desktop executable owns the native runtime, serves the page locally, and opens that page as the desktop UI; there is no second browser-side agent or duplicate local-server mode. Normal browser use opens the same local URL. **Serve remotely** is an explicit opt-in that exposes the same runtime/page to the LAN rather than creating another backend. `--native-ui` remains a diagnostic ImGui fallback.

## Model loading and provider recovery

EngCoder has three intentionally separate model-loading paths:

1. **Internal KeyMaker weights:** NVMe -> RAM -> VRAM residency managed by EngCoder.
2. **Native downloadable runtimes:** `qwenflash://...` profiles are downloaded/imported, loaded, unloaded, configured, and cached by EngCoder itself. The RTX 5080 / 64 GB recommendation remains 81,920 context / 1 slot with automatic EngCoder context compaction. Runtime tuning has three modes: **Recommended** preserves the known-good Qwen policy, **Automatic** derives hardware settings, and **Manual** exposes GPU offload %, exact GPU layers, KV placement/types, batches, mmap/lazy loading, expert-cache budgets and other Qwen controls.

   **Expert caching is now capability-qualified.** `Auto` is the default: legacy/unpatched binaries run the stable Qwen baseline with the MoE cache disabled, while an EngCoder runtime built by `runtime/qwen-flash/install-runtime.*` advertises cache support only after applying the Qwen3.8 Q2 padded `ffn_down_exps` compatibility patch. `Off` always uses the stable baseline; `On` is an explicit manual force for experimentation. If another advanced runtime switch fails, Recommended/Automatic first retry the same context with a minimal llama.cpp-compatible launch before reducing context. Model weights remain in the normal Hugging Face/`LLAMA_CACHE` store and are reused across runtime upgrades.

3. **External local provider models:** LM Studio/Bionic/OpenAI-compatible providers keep their own lifecycle; when LM Studio exposes its native model-management API, EngCoder can recover an unloaded selected model automatically.

If the provider requires authentication, apply its API token from **EngCoder Lab > Runtime**. The token is held by the native process only and is not persisted by the Web UI. Headless/CLI runs can continue to use environment configuration such as `LM_API_TOKEN` / EngCoder model-server token variables. HTTP 401 responses are surfaced as an authentication problem instead of a generic transport failure.

Set `ENGCODER_MODEL_AUTO_LOAD=0` only when provider model lifecycle must be managed manually.

## Training and continual learning

EngCoder creates and trains its own random-initialized KeyMaker weights. The included PyTorch worker is a numerical backend controlled by the native application.

- Foreground inference has priority over background training.
- Training checkpoints and relinquishes CUDA at a safe point when inference requests priority.
- The same job resumes from its checkpoint afterward.
- Verified responses become retrievable memory immediately.
- Neural weights change only through candidate training/evaluation/promotion.
- Promotion is atomic and rollback-safe.
- External local models may teach, verify, or provide fallback inference; their weights are not copied into KeyMaker.

Recommended training order: **KeyMaker Foundation -> Architect -> C++ addon -> other addons -> continual/background optimization**.

## Unified interface

The shared Desktop/Web frontend preserves the full chat-first BotFace interaction model while EngCoder owns orchestration natively:

- GPT-style composer, chat history, edit/regenerate/copy/verify actions, attachments, and voice controls.
- Per-message **Run info** with Architect model/routing time, KeyMaker inference, addon/tool timing and counts, memory retrieval, output estimates, and an optional persisted runtime timeline.
- **Session Settings** remains a compact user-facing dialog for session instructions, temperature/output limits, context/history, voice, Run info, appearance, and chat utilities. It does not expose server topology or endpoint selection.
- **EngCoder Lab** is a separate engineering surface for runtime/provider status, **Serve remotely**, Architect/KeyMaker/addon assignment, GPU residency, training, and learning/memory.
- **Model Library** lives inside EngCoder Lab and adds native Qwen Flash download/import, runtime installation, load/unload, profile configuration, cache controls, and persistent **Use as KeyMaker** selection without replacing the existing chat interface.
- The shared Web app always talks same-origin to the EngCoder runtime that served it. Models are discovered automatically rather than requiring model IDs or runtime URLs to be typed into normal settings.
- Session instructions are appended below EngCoder's native runtime contract rather than replacing the deterministic policy layer.
- Temperature, output token limit, and history controls are carried through the native task API and applied to model requests.
- Six appearance presets with Desktop/Web parity: **EngCoder Dark, EngCoder Light, Windows System, Windows Dark, Windows Light, Matrix Green**.

The diagnostic ImGui fallback uses modern Windows UI/font candidates, SDL high-pixel-density windows, Per-Monitor V2 DPI awareness, and live display-scale refresh for mixed-DPI/multi-4K setups.

### Remote-serving security

Remote serving is **off by default**. Enabling it exposes the same EngCoder runtime and tool surface to other devices on the local network; use it only on a trusted LAN. The Serve remotely switch and provider-credential updates can only be changed from a loopback/local EngCoder page.

### Candidate self-iteration

1. Prepare a separate checkout with a configured CMake build and tests. Launch EngCoder against that checkout using the [candidate setup guide](docs/SELF_ITERATION.md).
2. In **EngCoder Lab → Runtime**, enable **Use candidate self-iteration for the next send** and give it one specific coding task.
3. Review the exact before-and-after patch in chat and use **Approve once** for each edit you want applied. If the file changes while approval is pending, review a fresh patch.

EngCoder checkpoints approved edits and runs the build and tests. A failing check can trigger up to two repair attempts, with fresh approval for each edit. **Run info** shows the checks and any switch to a configured stronger model. Review the passing candidate diff before bringing it into your main checkout.

To follow a candidate from inside Codex, open the loopback UI served by `EngCoderServer.exe` in Codex's browser preview. Use a separate data directory for each checkout. The [validation record](VALIDATION.md) identifies the checks run against the isolated Windows candidate and the work still awaiting integration.

## Build

Windows:

```powershell
.\auto.bat
```

Root helpers are also available: `build.bat` / `build.sh` (build only), `check.bat` / `check.sh` (build + tests), `run.bat` / `run.sh` (build + run), and `clean.bat` / `clean.sh` (remove generated build output). All delegate to the same canonical build flow.

Linux:

```bash
./auto.sh
```

`auto.bat` builds the server, GUI, CLI, and tests and refuses to launch EngCoder when CTest fails.

See `docs/BUILD_WINDOWS.md` and `docs/BUILD_LINUX.md`.

## Canonical documentation

```text
docs/ARCHITECTURE.md      Architect / KeyMaker / addons / runtime boundaries
docs/MODEL_ABI.md         model/component compatibility contract
docs/MODEL_RESIDENCY.md   NVMe/RAM/VRAM placement and eviction
docs/MODEL_LIBRARY.md     native downloadable models / Qwen Flash profiles
docs/TRAINING.md          local training lifecycle
docs/LEARNING_POLICY.md   immediate memory vs weight learning
docs/PROTOCOL.md          local/remote served API and events
docs/UI.md                unified Desktop/Web behavior and themes
AGENTS.md                  repository rules for coding agents
```
### Engineering mission register

Use the **Missions** item in the main chat footer to review unfinished objectives from earlier releases. The register lives in `docs/FUTURE_MISSIONS.md`; synchronize the read-only Web/Desktop mission viewer using `python scripts/sync_missions.py`. This is a source-backed planning view, not a claim that mission execution or tracking is automatic.

Record the tested scope and the remaining acceptance work in the mission entry as each repair is verified. Keep benchmark and run evidence in [Validation](VALIDATION.md).

### Math formatting and local models (0.6.9 maintenance)

The chat renderer uses offline MathML for common TeX expressions, while the right-side Output view preserves the raw model text. See [Math and model-hosting guide](docs/MATH_AND_NATIVE_MODELS_0.6.9.md) for supported syntax, GGUF import/download and current hosting limits. The existing native library holds **one GGUF active at a time**. Concurrent small/middle models still use the configured external OpenAI-compatible inference provider; native multi-model hosting is a tracked, unverified mission, not a released feature.
