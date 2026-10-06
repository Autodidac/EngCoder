# EngCoder

EngCoder is a native Windows workbench for local AI. Its Codex-like hierarchy keeps the active conversation and composer dominant, leaves routing and run details inspectable, and keeps current and completed work visible without making the user hunt through pages. Chat, writing, coding, project work, tools, images, audio, files, streaming, cancellation, and per-reply review share the same task surface. The application can use its built-in engine, compatible AI servers on the same computer, or both when automatic backup is selected.

## The important distinction

EngCoder has two different built-in answer engines, and the interface reports them separately:

1. **Native foundation** — available immediately after the bundled helper starts. It has **0 B of neural weights**. It can answer deterministic greetings, retrieve close matches from the bundled curriculum, and recall exact user-approved replies. It does not call LM Studio, Ollama, or another AI server, and it is not neural model training.
2. **Trained neural model** — optional. Its numeric weights are created locally from random initialization by the explicit training capability pack. A trained candidate is never used until its independent checks pass and it is atomically promoted.

This is why a fresh installation can answer `hi` or `hello` while Activity correctly reports 0 B of neural weights. Chat labels such answers **EngCoder — native foundation** instead of presenting them as a trained model.

Connected AI applications remain separate. Locking one exact AI contacts only that AI. Automatic backup is allowed only by the default automatic selection.

## What is bundled and what is learned

The Training Library shows six evidence sources independently:

- **Bundled curriculum:** 4,247 English and C++23 source records shipped with the executable. These are not user learning history, and they are not copied wholesale into every neural training run.
- **Your learning:** explicit approved replies, corrections, and rejected drafts. Repeated approval of the same prompt/reply does not add weight.
- **Live Auto Training:** cleaned live conversations reviewed by the authorized local Qwen and Nemotron pair. Accepted, revised, needs-evidence, retry-pending, rejected, privacy-excluded, and measured-gap decisions remain inspectable.
- **Automatic teacher attempts:** candidate examples proposed by connected local AI servers and checked independently. Accepted, rejected, and failed attempts remain inspectable.
- **Generated foundation curriculum:** licensed/user-owned long-form sources transformed by Qwen and Nemotron, independently cross-reviewed, and split by document section before examples are generated.
- **Held-out checks:** 320 message-disjoint examples used only to evaluate candidates. They never train weights.

Emoji and provider branding are removed at the training boundary. Error cards, private reasoning, corrupt drafts, and deterministic calculator results cannot become positive training data. Permanent personal memory is stored separately and never baked into weights.

Before a neural job starts, EngCoder keeps three different artifacts:

1. the **append-only evidence pool**, which preserves accepted, corrected, rejected, superseded, and retry-pending records for inspection;
2. a **compact canonical curriculum**, which collapses exact duplicates and paraphrases into authoritative behavior representatives, then caps repeated identical answer targets under a supervised-token budget; and
3. an **immutable candidate snapshot**, which contains the newest eligible delta plus a small coverage-balanced replay set.

Held-out anchors remain evaluation-only, and any exact normalized prompt overlap is removed from the training selection. They are never copied into the gradient dataset. This separation lets EngCoder remember why a decision was made without repeatedly training on every saved phrase.

## Learning and retraining

EngCoder keeps four learning processes separate:

- **Immediate approved memory** makes an accepted or corrected answer retrievable without retraining.
- **Live Auto Training** cleans each successful live interaction, lets the authorized loopback Qwen and Nemotron models accept, revise, reject, or hold it for evidence, and automatically feeds only independently accepted reusable behavior into bounded weight batches. After three independently reviewed reusable conversation failures, Qwen repairs measured gaps one at a time and Nemotron verifies the replacements; the failed answers remain negative evidence.
- **Automatic idle learning** separately gathers and verifies seed-curriculum practice examples while the app is idle.
- **Neural weight retraining** is automatically queued when a verified microbatch fills. **Compile data and retrain model** can force a smaller pending batch. Training is serialized, resumable, checkpointed, and preempted by Chat.

**Build English Foundation** is a fifth, explicit one-time curriculum action. Qwen 27B maps long-form English and creates difficult lessons, Nemotron 4B creates fast variants, and each verifies the other's output. It accepts only public-domain, permissively licensed, or user-owned material. The teachers provide evidence only: EngCoder trains its own token-level parameters with the separate Internal PyTorch Training pack.

When an authorized teacher is installed in a loopback LM Studio server but not loaded, background curriculum work may ask that server to load the exact Qwen or Nemotron model on demand with a bounded role-specific context request. EngCoder never downloads a teacher, unloads or reconfigures an already-loaded model, or treats an unloaded model as available for foreground Chat. A failed load or unavailable server remains a visible retry instead of silently changing the machine.

Initial neural setup uses adaptive safety ceilings of 24,000, 36,000, or 50,000 steps for the Small, Medium, or Large profile. The trainer checks the full held-out and identity/privacy gate set at optimizer-aligned corpus boundaries and stops at the first passing boundary; reaching a ceiling without passing preserves the working model. Teachers never verify themselves, duplicate jobs coalesce, and rejected candidates cannot replace the working model.

The curriculum compiler selects at most one authoritative record per behavior cluster, caps repeated identical answer targets while preserving competency coverage, gives hard corrections precedence, and prefers independently verified, concise records. Human diversity gates count distinct behaviors, not four phrasings of one behavior. Historical `curriculum_weight` values are stripped: one selected record produces one gradient sequence rather than silently multiplying itself. Incremental jobs train the fresh delta with bounded replay instead of replaying the entire accumulated ledger.

Curriculum minimization does **not** by itself make a fixed architecture's weight file smaller. Parameter count, numeric precision, quantization, pruning, and expert design determine tensor size. The minimal curriculum instead limits what the model is asked to encode, reduces redundant gradient pressure and memorization risk, and keeps retraining bounded.

The visible counts are intentionally different. **User-taught replies** counts durable approved or corrected examples. **Neural-weight jobs** counts coalesced topic updates, so three teachings may legitimately be stored behind one queued job. With no optional trainer installed, the teachings still work through verified memory while neural weights remain exactly 0 B.

## Native base and optional packs

The base application is C++23 and does not require Python or PyTorch. The compact native answer host is a verified part of the portable release and runs directly from that release payload; startup does not copy it into application data or silently install a capability pack. Python, PyTorch, Transformers/PEFT, image engines, and audio engines belong only to explicit capability packs. EngCoder never installs an optional package automatically. Installing or inspecting a user-selected pack never changes the selected AI, starts a task, or interrupts Chat.

If work reaches a missing capability, EngCoder keeps the job and evidence intact and reports the exact pack, missing files, and explicit install action. Internal neural training, for example, names **Internal PyTorch Training** and checks for `runtime/Scripts/python.exe`, `backend/model_worker.py`, `backend/internal_model.py`, `backend/curriculum_guard.py`, and `backend/curriculum_compiler.py`. Native answers remain available while that work is blocked.

The native installed base—including the bundled compact native inference helper and required license notices, but excluding user-installed model weights and heavyweight capability packs—must remain at or below 100 MiB. Activity reports the native executable, neural weights, curriculum/learning data, optional software packs, outputs/cache, total disk use, free disk, and EngCoder-owned RAM separately.

## Unified interface

- **Workspace** uses a slim session and routing header above a dominant conversation. Per-reply review sits with the selected reply, the composer stays below it, and a right-hand run-details rail explains the selected source without squeezing the task surface.
- **Settings** groups AI connections, permanent memory and learning controls, and optional capabilities. Missing software is reported as a requirement, not installed as a side effect.
- **Activity** retains current and completed operations, outputs, logs, storage/RAM cards, growth, largest files/directories, pack projections, quotas, and safe cleanup.

An always-visible activity card sits below every page and wraps rather than truncates important state. It reports active replies, repository work, media generation, discovery, tool connections, built-in setup/training, verified-example creation, candidate retraining, add-on changes, cleanup, storage/RAM measurement, queued learning, retry waits, and the last completed result. Short operations remain readable after they finish; detailed history stays on Activity.

The default window is large enough to show a normal answer, review controls, and composer together, then clamps to the available work area on smaller displays. Window layout is DPI-aware. EngCoder vendors the backend-neutral EpochGui layout library, uses its splitter and rectangle primitives, and keeps Win32 ownership of windows, events, painting, text, and commands. Live resize events are coalesced, unchanged bounds are served from an EpochGui rectangle cache, hidden pages are skipped, and native child-window movement is committed in one batch per parent. Drag completion performs one exact layout and full dark-tree redraw. Rounded cards are painted by the clipped page background, so decorative surfaces cannot repaint over selectors, labels, or buttons after Send, Retry, source changes, or resize.

## C++23 modules

CMake builds the native code through `FILE_SET CXX_MODULES`:

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

Windows and third-party headers remain in module global fragments or implementation units. Native consumers and tests use `import`.

## Build and run on Windows

Requirements:

- Windows 10 or newer
- Visual Studio 2022 with Desktop development with C++
- CMake 4.4 or newer
- Git for optional project work

From PowerShell:

```powershell
cmake --fresh --preset windows-vs2022
cmake --build --preset windows-vs2022-release --clean-first -- /m:1
ctest --preset windows-vs2022-release
```

Or build, test, and launch with:

```powershell
.\build-and-run.cmd clean
```

The Release executable is created at:

```text
out\build\windows-vs2022\bin\Release\EngCoder.exe
```

Application data is stored under:

```text
%LOCALAPPDATA%\EngCoder
```

Legacy `%LOCALAPPDATA%\ThreadSpire` data is never recreated automatically. The old name remains only in published C++ module/ABI identifiers and explicit migration tools.

## Verification

The test suite covers module builds, unified routing, loaded, unloaded, missing, failed, and cancelled teacher endpoints, multimodal providers, package rollback and selection isolation, persistent/resumable training, atomic promotion and degraded-host reporting, source-separated learning records, storage accounting, footprint limits, repository isolation, responsive-layout contracts, and safe reset-path validation.

Useful documents:

- [Release history](changelog.md)
- [Start here](start-here.md)
- [Architecture](architecture.md)
- [Building](building.md)
- [Custom training data](custom-training.md)
- [Learning pipeline](docs/learning-pipeline.md)
- [Live Auto Training design](docs/live-auto-training.md)
- [Autonomous curriculum and training design](docs/autonomous-curriculum-and-training.md)
- [Minimal curriculum compiler](docs/minimal-curriculum-compiler.md)
- [Module boundaries](docs/module-abi.md)
- [Current implementation queue](docs/unified-workbench-queue.md)
- [Session verification report](docs/session-report-2026-08-26.md)
- [UI and activity verification report](docs/session-report-2026-08-28.md)
- [Live Auto Training verification report](docs/session-report-2026-08-29.md)
- [Live Auto queue repair verification report](docs/session-report-2026-08-29-0.7.1.md)
- [0.7.2 retained-review and EpochGui verification report](docs/session-report-2026-08-30-0.7.2.md)
- [0.8.0 autonomous curriculum and training report](docs/session-report-2026-08-30-0.8.0.md)
- [0.9.0 measured-gap autonomy and durable-training report](docs/session-report-2026-09-05-0.9.0.md)
- [0.9.1 minimal-curriculum validation report](docs/session-report-2026-09-05-0.9.1.md)
- [0.9.2 task-first workbench release report](docs/session-report-2026-10-02-0.9.2.md)

## License

EngCoder is available under the [MIT License](license.txt).
