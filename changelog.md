# Changelog

## 0.6.0 — 2026-08-29 resize and model-state clarification

- Removed synchronous full-child-tree repainting, redundant page visibility changes, hidden-page movement, and unchanged control moves from the live-resize path.
- Clarified that the immediately available native retrieval layer uses curated examples with 0 B neural weights and is not a trained LLM; only a promoted candidate weight artifact is a neural model.

## Unreleased

- Separated bundled curriculum, held-out checks, automatic teacher attempts, and user approvals/rejections in the Training Library so totals describe their actual purpose.
- Identified the zero-weight native foundation explicitly in Chat, Activity, and documentation; a 0 B neural-model history no longer implies hidden model training or server use.
- Reworded the explicit control as **Compile data and retrain model** and replaced internal training jargon with plain descriptions of the isolated test model.
- Batched Win32 child-window moves during resizing and removed full-tree redraws from every size event to prevent sluggish resizing, white flashes, and distorted intermediate frames.
- Renamed human-facing documentation and helper files to readable lowercase names, moved legacy material into dedicated folders, and refreshed cross-links and learning documentation.
- Changed the live product identity from the obsolete ThreadSpire application-data and build paths to EngCoder, including the executable target, compact native host, reset guard, capability-pack file contract, and current documentation. Legacy ThreadSpire names remain only where required by the published C++ module/ABI contract or historical migration records.
- Revalidated a true fresh start after the rename: clean Release and Debug builds passed all five `engcoder_*` tests, the exact EngCoder data root passed the no-follow reset guard, and both current and legacy application-data roots remain absent for first launch.

## 0.6.0

- Converted the native library, Win32 application, and tests to eleven named C++23 modules built with CMake `FILE_SET CXX_MODULES`.
- Unified interactive text, coding, repository, image, audio, and tool execution behind one streaming/cancellable provider contract; Coding Agent and Long Horizon are execution profiles.
- Removed Python scripts and private-runtime installation from the base executable; heavy engines are explicit transactional capability packs whose installation is isolated from model selection and training.
- Added a durable serialized training state machine with coalescing, atomic checkpoints, safe recovery, inference priority, and isolated candidate-host validation before atomic compare-and-swap promotion.
- Expanded storage reporting to six live cards, sample growth, largest files/directories, pack-size projections, quotas, and no-follow cleanup.
- Enforced a 100 MiB installed-base ceiling excluding models and packs, and added guarded exact-path AppData reset validation with a pre-deletion manifest.
- Added module, unified routing, multimodal, rollback, selection isolation, resumable training, failed-host promotion, storage, footprint, and reset regression coverage.
- Replaced the permissive three-canary English gate with six deterministic fluency, exact-instruction, grammar, definition, and conversational-context checks; old v1-ready manifests now resume bootstrap instead of serving corrupt prose.
- Extended the Small/Medium/Large English bootstrap targets to 24k/36k/50k cumulative steps and added curriculum weighting plus grounded conversational examples without materially increasing disk usage.
- Reworked the Task-page storage surface into six live cards for current model, accepted data, installed packs, outputs/cache, total growth, and free disk.
- Strengthened local teacher and independent-verifier prompts to require complete grammatical English and reject invented fragments.

## 0.5.8

- Bootstrap canary rejection now returns a repair-required result instead of being misreported as successful training followed by a failed host start.
- Automatic and manual repair continue the strongest checksum-verified rejected descendant, keep the full backbone trainable, and allocate a new generation rather than deterministically repeating or overwriting `g000001`.
- The native lifecycle re-inspects strict active-generation readiness before starting the host, even if a worker exits successfully.
- Manual host start now explains how to continue a saved repair candidate, and disabled automatic recovery retains the last concrete repair result on the Local AI dashboard.
- Added a CPU regression that rejects `g000001`, continues it into `g000002`, and proves the unready active generation is never served.

## 0.5.7

- Replaced the expert-only English bootstrap with a full shared-backbone update while retaining English-only training and held-out data.
- Added deterministic generation canaries and made readiness require trained status, full-backbone completion, held-out loss, and usable generated text.
- Refuses to start or serve an internal generation that has not passed the complete readiness gate; structurally corrupt short drafts are rejected more aggressively.
- Added a recoverable v5 lineage migration that quarantines partial-backbone models, candidates, jobs, expert registries, and stale control state while preserving verified episode memory and user configuration.
- Training-target/package selection now affects only the next pass and never cancels or immediately retriggers the pass already in flight.
- Coalesces verified evidence into eight-episode internal candidates, prioritizes exact candidate resumes, and preserves evidence collected while a candidate trains.
- Serializes teacher-data generation with new weight jobs without preempting an active job, and replaces five-second churn loops with bounded idle cadences.
- Added CPU lifecycle, streaming-server, migration, corruption, and native source-contract regression coverage.

## 0.5.6

- Fixed the automatic-recovery crash `AttributeError: 'str' object has no attribute 'is_file'` by preserving `pathlib.Path` values through the curriculum boundary.
- Removed the global `argparse` monkey patch and now applies the English-first gate explicitly only to the `train-internal` command.
- Stopped treating unrelated `--module` filesystem arguments as curriculum targets.
- Added a defensive path conversion inside the training-data loader so string paths cannot crash the worker.
- Mapped the UI target `english-foundation` to the model's actual `english-core` expert instead of attempting to train a nonexistent expert.
- Filters bootstrap examples, accumulated episodes, and held-out anchors to English while the foundation gate is closed; C++ records cannot participate in the English update or its evaluation.
- Persists `state/curriculum-state.json` from promoted English generations and opens the C++ gate only after the English evaluation and bootstrap target both pass.
- Added parser/type, all-C++ episode isolation, loader-level code rejection, alias, unrelated-command, and full training/promotion regression tests.

## 0.5.5

- Embedded and atomically deployed `curriculum_guard.py` beside `internal_model.py`.
- Backend verification now treats a missing curriculum guard as an incomplete installation and repairs it automatically.
- Pinned bootstrap tooling to `setuptools<82`, matching PyTorch 2.11 requirements.
- Added source-contract regression coverage for the missing-module startup failure.

## 0.5.4

- Rebuilt the Learning page around four independent concepts: target, evidence source, automatic generation, and shadow weight update.
- Changed the initial continual-learning target from `cpp23-core` to `english-foundation`; existing installations receive a one-time English-first curriculum migration without deleting checkpoints or evidence.
- Kept the target selector available while teacher generation runs. Target changes apply at a safe batch boundary instead of becoming locked behind the active pass.
- Persisted the automatic-training switch separately from the selected target. Disabling automation no longer forces or resets a skill.
- Removed automatic C++ specialization from the core-skill bootstrap action. C++23 targets remain available but require an explicit selection.
- Added target-specific built-in seed filtering and independent per-target/per-file cursors so English, C++, and custom datasets cannot consume each other's offsets.
- Added a complete trusted custom-JSONL workflow: target selection, custom skill ID, file browser, embedded template, whole-file trusted import, evidence review, automatic shadow updates, and manual queue/retry.
- Added **Stop current pass** and clarified the difference between stopping evidence generation and pausing resumable background weight training.
- Completed the dark Win32 theme across client surfaces, tabs, push buttons, combo boxes, edits, progress bars, status text, and owner-drawn focus/disabled states.
- Added regression contracts for English-first migration, persistent target/automation state, custom import, external-only teaching, and elimination of remaining white native-control islands.

## 0.5.2

- Fixed the MSVC GUI build failure in `edit_visible_line_capacity()` by explicitly normalizing Win32 `LONG` text metrics and edit-rectangle dimensions to `int` before calling `std::max`.
- Added a Windows UI source-contract regression test that rejects the ambiguous `LONG`/`int` expressions responsible for C2672.
- Preserved every 0.5.1 real-time learning, scroll-preservation, teacher-isolation, visible training, and output-quality change unchanged.

## 0.5.1

- Replaced per-token Win32 edit updates with a 40 ms UI flush timer and redraw batching. Conversation, Learning, and Logs views now retain the user's selection and first visible line; they follow new text only when already at the tail.
- Fixed the ghosted/overdrawn text seen during rapid live updates by suppressing redraw while edit contents change and forcing one complete repaint afterward.
- Made internal continual learning immediate: each newly admitted independently verified episode can queue a shadow internal-weight update, while duplicate same-skill requests coalesce into one trainer job.
- Exposed the live weight pipeline on Learning: queued updates, active skill/candidate/state, evidence threshold, trainer phase, step, percentage, loss, verified tokens, elapsed time, ETA, progress path, and live log tail.
- Prevented the internal model from acting as its own teacher or verifier. Automatic teacher passes now require an external connected local LLM, prefer a distinct external verifier, and wait for current weight work to drain before generating more evidence.
- Fixed trusted/reference-verified teacher records being admitted by C++ but ignored by the Python internal trainer. Those records now participate in internal weight updates.
- Added continuous idle scheduling: a successful teacher pass with trainable evidence schedules the next pass after the queued update completes instead of waiting ten minutes.
- Added exact byte n-gram blocking and a conservative structural quality gate for the tiny internal model. Repetitive or corrupt drafts are withheld, retried once greedily, then reported as `low_confidence` rather than displayed.
- Added automatic routing fallback after a rejected internal draft when another eligible local model is available. Internal-only mode reports the rejection explicitly instead of emitting garbage.
- Corrected lifecycle wording while bootstrap owns the GPU: the internal host is shown as intentionally paused for training and automatically resumed afterward, not falsely presented as an unexplained stopped host.
- Expanded C++ and Python regression coverage for scroll preservation, teacher isolation, immediate update visibility, trusted-evidence loading, no-repeat generation, and low-confidence rejection.

## 0.5.0

- Replaced the manual install/verify/create/bootstrap/start sequence with an automatic persistent lifecycle controller enabled by default.
- Added `build-and-run.cmd` so the replacement can be built, validated, and launched in one command without locating a preset-specific executable.
- Added automatic private-runtime repair, dependency verification, model initialization, exact bootstrap resume, promotion, host startup, unexpected-host restart, and indefinite bounded-backoff recovery.
- Fixed foreground bootstrap waiting forever behind stale `training.paused`, `inference.*`, or dead `training.lock` state. Foreground bootstrap now clears cooperative stale blockers and has a five-minute training-slot timeout with the blocking reason.
- Added atomic `internal-model-progress.json` heartbeats containing phase, step, overall target, verified tokens, loss, elapsed time, measured ETA, process ID, resumability, and exact errors.
- Added a native progress bar and visible lifecycle status. A worker silent for 15 minutes is cancelled and resumed from its exact checkpoint instead of appearing to run overnight.
- Opened directly on the Local AI progress dashboard whenever bootstrap is incomplete, instead of hiding an active first-run lifecycle behind the Workspace tab.
- Added persisted bootstrap targets and completion state to generation manifests while retaining compatibility with 0.4.x generations that lack those fields.
- Preserved and resumed partial existing generations; the controller calculates only the remaining bootstrap work instead of restarting from zero.
- Prevented incomplete bootstrap candidates from being trapped by the normal continual-learning regression gate; finite bootstrap candidates advance until the declared target, while post-bootstrap continual learning retains regression checks.
- Collapsed every manual setup/recovery control by default behind a single advanced toggle.
- Added a dark Win32 client area, dark controls, and immersive dark title bar.
- Made **Stop** explicitly disable automatic recovery so a deliberate stop is not immediately undone.
- Added C++ lifecycle-state/progress parsing tests, source contracts for automation and dark mode, Python syntax validation, stale-blocker smoke coverage, and a tiny full bootstrap/promotion smoke test.

## 0.4.2

- Removed the last user-facing references to the previous product name; internal-model headings and status text now identify ThreadSpire consistently.
- Added bounded recent-turn context to Assistant and Coding Agent requests so follow-up prompts can use the preceding conversation without requiring a weight update.
- Kept routing fast by analyzing only the current user request while sending recent turns to the selected model as inference context.
- Clearing the Workspace now clears both the visible transcript and the in-memory conversation context.
- Extended regression contracts for internal-model provenance, bounded context, and exact mid-accumulation checkpoint state.

## 0.4.1

- Preserved one expert route per training example instead of unioning every skill in a padded batch; English-only records no longer backpropagate through C++23 or planning experts because a neighboring sample uses them.
- Made mid-accumulation preemption exactly resumable by checkpointing and restoring pending parameter gradients together with model, optimizer, data cursor, order, and RNG state.
- Added a deterministic split-run versus uninterrupted-run smoke test that proves resumed weights are byte-for-byte identical after a preemption inside gradient accumulation.
- Corrected partial final accumulation steps before optimizer commit instead of silently discarding their gradients.
- Capped generated output to the configured model context so KV caches cannot grow beyond the internal model's declared context window.

## 0.4.0

- Replaced the default private inference path with a custom ThreadSpire decoder transformer initialized from random weights on the local machine.
- Added a deterministic UTF-8 byte tokenizer, RMSNorm, RoPE causal attention, tied embeddings, SwiGLU blocks, KV-cache generation, and per-layer English/C++23/debugging/planning experts.
- Added Micro (22.5M), Standard (50.0M), and Large Local (93.6M) internal profiles.
- Added explicit **Create random model** and **Create + bootstrap train** controls. Untrained random generations cannot serve requests.
- Added complete SafeTensors generations with local-origin provenance, `external_base_weights: false`, checksums, parent generations, metrics, and atomic active-pointer promotion.
- Added full-weight internal bootstrap and scoped skill training; imported Qwen or other pretrained weights are no longer the internal source.
- Added exact-resume checkpoints containing candidate weights, optimizer state, dataset order/cursor, and Python/CPU/CUDA RNG states.
- Added inference-priority preemption and candidate requeue/resume for internal training.
- Added 3,596 unique bootstrap records and 320 message-disjoint held-out anchors covering English, C++23, debugging, architecture, and translation-to-English.
- Added a CPU smoke test that initializes a tiny local model, preempts it, resumes it, evaluates it, promotes it, and verifies that no external base weights are present.
- Kept connected local LLMs as explicit bridges/teachers/verifiers and retained external-model LoRA/FFN workflows as optional advanced tools.
- Renamed the application and C++ namespace to ThreadSpire / `threadspire::ai`; only selected non-model user data is migrated from the prior application directory.

## 0.3.4

- Added one explicit, persisted **AI source** choice: Automatic, Connected local LLM, or ThreadSpire internal model. Workspace and Coding Agent mirror the same setting and cannot diverge.
- Every completed reply now identifies the actual source and model used in both the conversation heading and execution details.
- Rebuilt transcript formatting around CRLF-separated role headings, blank-line spacing, and turn separators so streamed replies no longer collapse into text such as `YouhiAssistant...`.
- Prevented one-line Win32 labels from wrapping into clipped fragments; long labels now stay on one line and use ellipsis when necessary.
- Reorganized Workspace, Local AI, Learning, and Coding Agent rows to avoid overlapping labels and controls at supported window sizes and DPI scales.
- Switched from a hard-coded oversized font to the DPI-aware Windows message font, with a Segoe UI fallback.
- Clarified all user-visible runtime terminology: connected local LLMs are separate from ThreadSpire internal models and their private PyTorch host.
- Migrated legacy built-in `Custom AI` display names to `ThreadSpire` labels without changing model IDs or artifacts.
- Fixed the duplicate saved-MCP reconnect call and made request-scheduling failures visible inside the conversation.
- Added regression contracts for source synchronization, label behavior, transcript formatting, DPI-aware font selection, and runtime labeling.

## 0.3.3

- Fixed the blank-tab regression introduced in 0.3.2: `WS_CLIPSIBLINGS` caused each page host to be clipped behind the overlapping tab control.
- Page hosts now use the tab control's actual display rectangle through `TabCtrl_AdjustRect` instead of hard-coded offsets.
- The active page is explicitly raised above the tab control and redrawn after layout and tab changes.
- Kept page-host notification forwarding so buttons, combo boxes, checkboxes, and other controls continue reaching `MainWindow`.
- Added source-contract checks for page visibility, tab display-area layout, and active-page z-order.

## 0.3.2

- Fixed the Win32 command-routing defect that made buttons, combo boxes, checkboxes, and other controls inside tab pages appear functional while their notifications never reached `MainWindow`.
- Added a page-host subclass that forwards command, notification, scroll, owner-draw, and control-color messages to the root window.
- Automatic teacher-data generation now begins after a usable local teacher is discovered; it no longer depends on a successful manual chat request.
- Added trusted seed ingestion, a persistent rotating seed cursor, and an eight-example default batch so the automatic pipeline can produce real LoRA-incubation evidence instead of repeating the first four prompts forever.
- LM Studio discovery now uses the native `/api/v1/models` endpoint and selects loaded LLM instances rather than treating every downloaded JIT-visible model as connected.
- Generic Qwen and DeepSeek models are recognized as coding-capable for the primary Coding Agent route.
- Added regression contracts for page-host command forwarding, automatic teacher scheduling, LM Studio loaded-model discovery, and rotating trusted-seed ingestion.

## 0.3.1

- Fixed the MSVC GUI build failure caused by two `learning_y` variables being declared in the same `layout_controls()` scope.
- Renamed the Workspace sidebar coordinate to `workspace_learning_y`; the Learning-tab coordinate remains mutable and independent.
- Added a source-contract regression test for the duplicate-layout-variable failure.

## 0.3.0

- Replaced the fragmented Assistant page with a unified Workspace containing connection status, mode, model selection, optional repository, streaming conversation, prompt, controls, route activity, MCP state, and learning state.
- Added Assistant/Coding Agent mode switching directly in Workspace while retaining the dedicated Coding Agent tab and every advanced subsystem.
- Added explicit local-model selection plus an Automatic mode that chooses the fastest capable model.
- Added automatic failover across eligible local endpoints when the first model fails before producing content.
- Added minimum-compute interactive routing so simple prompts do not invoke unnecessary planner or verifier lanes.
- Restricted MCP tool schemas to tasks that actually need tools, preserving normal streaming chat when an MCP server is connected.
- Added Stop, Clear, `Ctrl+Enter`, actionable in-conversation errors, and persistent conversation presentation.
- Broadened OpenAI-compatible and Ollama response parsing for local servers that return text, response, reasoning-content, or thinking fields.
- Made foreground inference cancel local teacher generation and pause background learning.
- Prevented automatic teacher-data generation from starting until a real local-model response has completed successfully.
- Reorganized Local AI, Learning, and Skill Modules controls into labeled, responsive sections without removing existing functionality.
- Persisted the preferred local model and refreshed model selectors after discovery, manual endpoint changes, registry edits, and private-host state changes.

## 0.2.0

- Made Assistant and Coding Agent the first two primary tabs.
- Removed the private-PyTorch-host requirement from normal inference and repository-agent execution.
- Added automatic localhost discovery for Ollama, LM Studio, llama.cpp/LocalAI, Jan, KoboldCpp, and compatible OpenAI APIs.
- Added manual OpenAI-compatible and Ollama model endpoints in the native GUI.
- Added streamed token delivery from local model servers.
- Added MCP Streamable HTTP connection, tool discovery, tool calling, and episode tool traces.
- Added local teacher/verifier piggyback generation from the trusted seed corpus.
- Added reference-verified evidence as a distinct LoRA-incubation path.
- Replaced hard-coded fast/coder/thinker/reviewer route IDs with capability- and telemetry-based selection.
- Preserved the private PyTorch host, LoRA workflow, modular FFN refinement, skill registry, long-horizon loop, and rollback system.
- Added protocol, provider, MCP, routing, and reference-verification regression tests.
- Preserved measured throughput, latency, reliability, credentials, and user-assigned roles across repeated discovery scans.
- Reconnects the last successful MCP endpoint automatically on the next launch.

## 0.1.1

- Fixed MSVC layout compilation by normalizing Win32 `LONG` rectangle dimensions to `int` before `std::max`.
- Replaced unavailable `Button_GetCheck` and `Button_SetCheck` helper macros with direct `BM_GETCHECK` and `BM_SETCHECK` messages.
- Added Windows UI source-contract regression checks.

## 0.1.0

- Added native Windows C++23 GUI with Chat, Models, Skill Modules, Learning, Long Horizon, and Logs pages.
- Added adaptive direct, cascade, hedged, and agentic multi-model routing.
- Added observed token-rate, reliability, and acceptance feedback.
- Added persistent indexed episodic/retrieval memory and restart-safe deduplication.
- Added knowledge classification for memory versus weight learning.
- Added candidate registry, active generation checks, atomic promotion, crash recovery, and rollback.
- Added preemptible LoRA skill incubation.
- Added direct bounded full-rank FFN refinement with optimizer/RNG checkpoints.
- Added checksum-verified, base-compatible, FFN-only modular skill packs.
- Added foreground-inference GPU priority and model-cache eviction.
- Added autonomous C++23 build/test/repair trajectories.
- Added 190 starter examples and 10 held-out anchors.
- Added Windows/POSIX platform separation and warning-clean C++23 core tests.
