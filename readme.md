# EngCoder

EngCoder is a native Windows workbench for local AI. Chat, writing, coding, project work, tools, images, audio, files, streaming, cancellation, and per-reply review share one interface. The application can use its built-in engine, compatible AI servers on the same computer, or both when automatic backup is selected.

## The important distinction

EngCoder has two different built-in answer engines, and the interface reports them separately:

1. **Native foundation** — available immediately after the bundled helper starts. It has **0 B of neural weights**. It can answer deterministic greetings, retrieve close matches from the bundled curriculum, and recall exact user-approved replies. It does not call LM Studio, Ollama, or another AI server, and it is not neural model training.
2. **Trained neural model** — optional. Its numeric weights are created locally from random initialization by the explicit training capability pack. A trained candidate is never used until its independent checks pass and it is atomically promoted.

This is why a fresh installation can answer `hi` or `hello` while Activity correctly reports 0 B of neural weights. Chat labels such answers **EngCoder — native foundation** instead of presenting them as a trained model.

Connected AI applications remain separate. Locking one exact AI contacts only that AI. Automatic backup is allowed only by the default automatic selection.

## What is bundled and what is learned

The Training Library shows five sources independently:

- **Bundled curriculum:** 4,247 English and C++23 examples shipped with the executable. These are not user learning history.
- **Your learning:** explicit approved replies, corrections, and rejected drafts. Repeated approval of the same prompt/reply does not add weight.
- **Live Auto Training:** cleaned live conversations reviewed by the authorized local Qwen and Nemotron pair. Accepted, revised, needs-evidence, retry-pending, rejected, and privacy-excluded decisions remain inspectable.
- **Automatic teacher attempts:** candidate examples proposed by connected local AI servers and checked independently. Accepted, rejected, and failed attempts remain inspectable.
- **Held-out checks:** 320 message-disjoint examples used only to evaluate candidates. They never train weights.

Emoji and provider branding are removed at the training boundary. Error cards, private reasoning, corrupt drafts, and deterministic calculator results cannot become positive training data. Permanent personal memory is stored separately and never baked into weights.

## Learning and retraining

EngCoder keeps four learning processes separate:

- **Immediate approved memory** makes an accepted or corrected answer retrievable without retraining.
- **Live Auto Training** cleans each successful live interaction, lets the authorized loopback Qwen and Nemotron models accept, revise, reject, or hold it for evidence, and automatically feeds only independently accepted reusable behavior into bounded weight batches.
- **Automatic idle learning** separately gathers and verifies seed-curriculum practice examples while the app is idle.
- **Neural weight retraining** is automatically queued when a verified microbatch fills. **Compile data and retrain model** can force a smaller pending batch. Training is serialized, resumable, checkpointed, and preempted by Chat.

Initial neural setup uses adaptive targets of 24,000, 36,000, or 50,000 steps for the Small, Medium, or Large profile. Each run stops at its bound with diagnostics. Teachers never verify themselves, duplicate jobs coalesce, and rejected candidates cannot replace the working model.

The visible counts are intentionally different. **User-taught replies** counts durable approved or corrected examples. **Neural-weight jobs** counts coalesced topic updates, so three teachings may legitimately be stored behind one queued job. With no optional trainer installed, the teachings still work through verified memory while neural weights remain exactly 0 B.

## Native base and optional packs

The base application is C++23 and does not require Python or PyTorch. Python, PyTorch, Transformers/PEFT, image engines, and audio engines belong only to explicit capability packs. Installing or inspecting a pack never changes the selected AI, starts a task, or interrupts Chat.

The native installed base, excluding models and optional packs, must remain at or below 100 MiB. Activity reports the native executable, neural weights, curriculum/learning data, optional software packs, outputs/cache, total disk use, free disk, and EngCoder-owned RAM separately.

## Unified interface

- **Chat** contains the conversation, composer, task override, AI access group, exact AI selector, file attachments, Stop, per-reply controls, and the separate **Live Auto Training** checkbox.
- **AI & Tools** contains connected-server discovery and backup connection/repair controls.
- **Memory & Learning** contains permanent memory, automatic example collection, explicit neural retraining, live job state, and the complete searchable Training Library.
- **Capabilities** shows what is ready and audits every optional pack without following links outside its root.
- **Activity** shows outputs, logs, eight storage/RAM cards, growth, largest files/directories, pack projections, quotas, and safe cleanup.

An always-visible two-line activity strip sits below every page. It reports active replies, repository work, media generation, discovery, tool connections, built-in setup/training, verified-example creation, test-model retraining, add-on changes, cleanup, storage/RAM measurement, queued learning, retry waits, and ready state without requiring a tab change.

Window layout is DPI-aware. EngCoder vendors the backend-neutral EpochGui layout library, uses its splitter and rectangle primitives, and keeps Win32 ownership of windows, events, painting, text, and commands. Live resize events are coalesced to 30 Hz, unchanged bounds are served from an EpochGui rectangle cache, hidden pages are skipped, and native child-window movement is committed in one batch per parent. Drag completion performs one exact layout and full dark-tree redraw. Rounded cards are painted by the clipped page background, so decorative surfaces cannot repaint over selectors, labels, or buttons after Send, Retry, source changes, or resize.

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

Application data remains under the legacy compatibility path:

```text
%LOCALAPPDATA%\EngCoder
```

The directory name is retained for safe upgrades; it is not the product name.

## Verification

The test suite covers module builds, unified routing, multimodal providers, package rollback and selection isolation, persistent/resumable training, atomic promotion, failed-host recovery, source-separated learning records, storage accounting, footprint limits, repository isolation, and safe reset-path validation.

Useful documents:

- [Start here](start-here.md)
- [Architecture](architecture.md)
- [Building](building.md)
- [Custom training data](custom-training.md)
- [Learning pipeline](docs/learning-pipeline.md)
- [Live Auto Training design](docs/live-auto-training.md)
- [Module boundaries](docs/module-abi.md)
- [Current implementation queue](docs/unified-workbench-queue.md)
- [Session verification report](docs/session-report-2026-08-26.md)
- [UI and activity verification report](docs/session-report-2026-08-28.md)
- [Live Auto Training verification report](docs/session-report-2026-08-29.md)
- [Live Auto queue repair verification report](docs/session-report-2026-08-29-0.7.1.md)
- [0.7.2 retained-review and EpochGui verification report](docs/session-report-2026-08-30-0.7.2.md)

## License

EngCoder is available under the [MIT License](license.txt).
