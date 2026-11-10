# EngCoder 0.7.4

**Async UI 1 maintenance:** The right-side inspector opens without waiting for a model-loading API call or a selected assistant message; its expensive content and layout are deferred. Model output/event bursts update in batches and yield for browser paint. This is a UI responsiveness improvement, **not** a second native inference worker, GPU concurrency guarantee, or a change to Qwen loading and offload.

**Qwen Live:** The right-side view keeps the last generation visible after completion and through later requests that have not yet produced text. The next provider fragment replaces it. See `VALIDATION.md` and `CHANGELOG.md` for coverage.

**0.7.1 release:** Default Qwen GPU layer-offload setting is now 50% for new profiles and Reset/Default controls. The existing 0–100% scale and cap, manually configured percentages, and Qwen Live 2 behavior are unchanged. CMake, vcpkg, Web UI and training-worker version identifiers are synchronized. See [`docs/DEVELOPMENT_0.7.1.md`](docs/DEVELOPMENT_0.7.1.md).

**Current maintenance:** Strict advisory handoff validation and difficulty-gated small-model delegation prevent broken 9B code/reasoning responses from entering KeyMaker context. Basic C++23 examples go straight to KeyMaker; substantial refactors may use small → middle → KeyMaker. See [Three-model quickstart](docs/SUBAGENTS_QUICKSTART.md).

**Previous maintenance:** Three-model default handoffs now reuse a configured small external model for *bounded specialist checklists* when a skill-specific model is unassigned, pass successful advisory output to an independent middle reviewer, and reserve final coding/math/complex responses for KeyMaker. The runtime shows completed stages separately from attempted/fallback ones. Web/native GPU settings use the current safe layer-budget scale without historical conversion text. The **Assign small + middle** control preserves the KeyMaker selection. See [Three-model quickstart](docs/SUBAGENTS_QUICKSTART.md) for desktop, served, LM Studio setup, model roles and failure behavior.

**0.6.8 layered agents + safety maintenance:** Assign one or more small LM Studio specialists, an optional different middle reviewer, and internal Qwen as KeyMaker Foundation. The middle reviewer critiques bounded specialist evidence before Qwen's final decision on non-simple tasks; it has no tools. The reviewer and model-specific thinking controls live under **EngCoder Lab → Models → Architect + KeyMaker**; defaults preserve the existing single-foundation behavior until another model is selected. Simple-responder failure before the first output fragment retries on Qwen once. New checkpoints are bound to their exact workspace path; saved training evidence is rechecked against bans before weight-training ingestion. Optional remote mode requires `ENGCODER_REMOTE_API_TOKEN` (32+ chars) and bearer authorization, with loopback the recommended default. The generated-text-only panel, footer, and user-tuned Qwen GPU profile are unchanged. See [`docs/DEVELOPMENT_0.6.8.md`](docs/DEVELOPMENT_0.6.8.md).

**0.6.7 routing/UI correction:** Bare greetings (`hi`, `hey`, `hello`, `yo`) are classified as simple. To use an external lightweight responder, select one under **EngCoder Lab → Models → Architect + KeyMaker → Simple response model** and keep **Automatic model + Architect routing** enabled. With no model assigned/discovered, the Foundation Qwen answers by design; this fallback is now explained in the selector. The capability-addon switch is independent of simple-response routing. The empty attachment-count pill is hidden unless attachments exist. A user-selected Qwen GPU offload setting of 99% is retained; no silent defaults reset or GPU scale change was made.

**Limitations:** no unverified GGUF or LM Studio MTP pairing is advertised; user-configured local GGUF draft pairing requires runtime compatibility testing. Token-fragment streams are not tokenizer IDs. The shadow controller is *not* a proven JEPA architecture, and the process tool is *not* an OS sandbox. The Windows source builds and automated suites are validated in `VALIDATION.md`; live RTX 5080 generation, model reload, and large-model interactive responsiveness still require hardware acceptance. See [`docs/DEVELOPMENT_0.6.7.md`](docs/DEVELOPMENT_0.6.7.md) and [`docs/FUTURE_MISSIONS.md`](docs/FUTURE_MISSIONS.md).

EngCoder is a local-first C++23 AI application built around **Architect**, a fast orchestration/controller layer, and **KeyMaker**, the local learned-model and training family. The resident **KeyMaker Foundation** supplies English language/reasoning; C++23, documentation, planning, tool use, reranking, and future skills are modular KeyMaker addons loaded only when needed.

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

## Build

Windows:

```powershell
.\auto.bat

Root helpers are also available: `build.bat` / `build.sh` (build only), `check.bat` / `check.sh` (build + tests), `run.bat` / `run.sh` (build + run), and `clean.bat` / `clean.sh` (remove generated build output). All delegate to the same canonical build flow.
```

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

### Math formatting and local models (0.6.9 maintenance)

The chat renderer uses offline MathML for common TeX expressions, while the right-side Output view preserves the raw model text. See [Math and model-hosting guide](docs/MATH_AND_NATIVE_MODELS_0.6.9.md) for supported syntax, GGUF import/download and current hosting limits. The existing native library holds **one GGUF active at a time**. Concurrent small/middle models still use the configured external OpenAI-compatible inference provider; native multi-model hosting is a tracked, unverified mission, not a released feature.
