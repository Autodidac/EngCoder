## 0.7.4 — Windows maintenance release (2026-11-10)

- Updated runtime activity reporting and clarified which verified memories are eligible for training.
- Packaged the portable Windows x64 build and matching source with SHA-256 checksums.

## 0.7.1 Async UI 1 — responsive model loading and generation (2026-10-09)

- Correct the fresh-profile Web GPU input to the documented 50% default; saved user profiles remain untouched.
- EngCoder Lab no longer uses a modal backdrop that blocked every toolbar/sidebar click during model loading. It retains its styling, closes on an outside click, and allows Qwen Live to appear above it.
- Right-side Output / Qwen Live / Run info drawer becomes visible immediately, even if no chat run is selected and native Qwen is still loading. Defer expensive layout and panel rendering until the next animation frame.
- Qwen Live updates coalesce within a browser frame; opening an external-client monitor no longer forces a synchronous panel measurement during each poll.
- Batch task-event generation updates: render/persist once per poll rather than once per token fragment; for large event backlogs yield every 128 events so clicks, Stop, and painting can run.
- Preserve all original events, terminal handling, partial conversation recovery, Qwen Live 3 idle text retention, the GPU scale/default, and the 25-mission register. Build marker: `Async UI 1`.
- **Acceptance not yet verified on Windows:** live Edge responsiveness while a 79 GB GGUF loads and Qwen generates; resource pressure and OS paging can still stall the *whole computer*, independently of UI scheduling.

## 0.7.1 Qwen Live 3 hotfix — retained output during preparation (2026-10-09)

- Keep the previous Qwen generation visible while another request prepares. A new request's start event is **not** a generated fragment; only actual text, reasoning, or tool-argument output replaces the previous display.
- Preserve the last completed output if an attempted request fails or finishes without generating any fragments. After completion, display `Idle · last output retained` and allow reading without closing the Qwen Live tab.
- Retain the same behavior when the Web client reconnects or refreshes from the bounded native snapshot. Distinguish the hotfix on screen with the `Qwen Live 3` build suffix.
- Extend native Catch2 and Web regression coverage for start-without-deltas, empty completion, first-fragment replacement, and recovery snapshots. GPU percentages, UI layout, routes, and missions are unchanged.
- Windows/MSVC and live external MCP tests remain required; this is not a claim of full live-hardware validation.

## 0.7.1 — GPU default, version consistency, and regression review (2026-10-09)

- Set the *new-model/default/reset* Qwen GPU layer offload to 50% in C++ model/config defaults, native GUI, Web UI and JSON fallback; preserve the existing percentage scale and effective-layer ceiling. Existing saved profiles are not overwritten, including custom 99% offload settings.
- Synchronize CMake, vcpkg manifest, Web UI display and training worker/MCP-client version metadata to 0.7.1.
- Keep previous Qwen Live 2 idle-output retention, authentication, multi-model handoff and UI behavior. Add a targeted release regression for consistency and GPU default fallback.
- Known limits: Windows/MSVC, full Catch2 suite, LM Studio/Qwen live testing, and OS-level sandbox remain subject to validation; see FUTURE_MISSIONS.md.

## 0.6.9 Qwen live output integration — 2026-10-08

- Observe provider-emitted Qwen Next Flash fragments at the native-model boundary, not only from EngCoder chat tasks. Calls through `/api/tasks`, `/v1/chat/completions` (streaming and non-streaming), and `/v1/responses` now share a bounded Qwen-output journal. External MCP/tool clients must use EngCoder's `/v1` URL for this observation; direct calls to standalone llama-server or LM Studio cannot be intercepted.
- Add a separate right-side **Qwen live** tab. Automatically open it for new external Qwen activity when no EngCoder chat task is running. Preserve the normal chat Output and Run info tabs, themes and model/GPU settings.
- Capture provider text, provider-exposed reasoning and generated tool arguments as emitted fragments; omit telemetry JSON. Fragments are not tokenizer IDs. Keep the journal limited to 2048 events / 512 KiB; the Web pane retains 96 KiB of output per active invocation.
- Guard the host-wide read-only output endpoint with existing remote bearer authentication. The host's remote API token grants administrative access to output from *other* authenticated clients on that same EngCoder instance; use a trusted host/network.
- Add native journal and Web integration regressions. Live Windows Qwen/MCP verification remains outstanding.

## 0.6.9 maintenance — validated specialist handoffs (2026-10-08)

- Route basic C++23 show-only snippets directly to KeyMaker; reserve small → middle → KeyMaker for substantial refactoring, multi-file work and complex engineering. Math and sensitive requests remain foundation-only.
- Require short JSON `handoff` arrays from the small preparer and middle reviewer. Reject raw code, reasoning-only text, bad format, unauthorized tool calls, token-limit truncation, and oversized notes before prompt assembly.
- Prevent failed small-model preparations from being automatically retried by the second handoff path; record failure reason without forwarding untrusted text. KeyMaker receives the complete original user prompt.
- Add native classifier/contract/integration tests and update the model hierarchy guide. Native Windows and real provider checks remain required.

## 0.6.9 hotfix — native-served math asset (2026-10-08)

- Fix `/ui/js/math.js` being omitted from the native server's explicit static asset routes (HTTP 404). Chat math rendering now loads in both the Windows desktop shell and served Web clients.
- Disable browser caching of served UI files so stale versions do not survive a source replacement and refresh.
- Add a regression test checking every JavaScript asset referenced by `index.html` against the native route list, not just running math parsing in isolation.
- Keep saved messages, Copy, and the right-panel Output as original unmodified model text. No model or GPU settings changed.

## 0.6.9 maintenance — math display and local-hosting documentation (2026-10-08)

- Display common TeX math expressions in chat using browser-native, offline MathML. Original source remains unchanged for Copy, persistence, and the raw Output pane. Fenced code is not converted.
- Shorten GGUF filesystem paths in the normal response-model label only; Run info and underlying routing retain the full model identity.
- Add a native model library setup guide explaining the current register/download/import/load path, the single-active-model limit, and why external small/middle models are OpenAI-compatible HTTP providers rather than MCP.
- Add the native multi-host pool as the 25th open engineering mission, without changing Qwen's established GPU settings or claiming simultaneous native sidecars before compatibility/resource verification.
- See `docs/MATH_AND_NATIVE_MODELS_0.6.9.md` and `VALIDATION.md`.

## 0.6.9 subagent usability maintenance

- Respect the capability-addon toggle for preparatory handoffs and reviews without disabling simple-response routing.
- Fixed unassigned specialist execution gate; a configured small external model may now serve as a limited checklist-only specialist without a separate addon assignment. Reuse its result rather than requesting duplicate small checklists.
- Run info distinguishes completed specialist and middle review from attempted/failed stages. Final response model remains separate from Architect controller attempts.
- Corrected inline LM Studio setup instructions for desktop/served clients; safe **Assign small + middle** preserves KeyMaker; removed historical GPU-percent comparisons from live model controls.
- Added one C++ integration test for specialist fallback ordering and a three-model quickstart; preserved the existing missions. Live Windows/LM Studio testing remains outstanding.

## 0.6.9 — routing diagnostics and quantitative handoff hotfix

- Exclude symbolic, modular, percentage, and quantitative math from the 9B preparatory-model stage; KeyMaker retains final authority.
- Bound Architect controller responses to 256 tokens and disallow tool calls for routing decisions.
- Report the reason for a model-based Architect fallback in Run info, without changing the clean generated-output view.
- Preserve the configured three-model roles and previous 0.6.9 hotfixes.

## 0.6.9 test hotfix — mathematical request routing

- Correct System 1 Architect classification for percentage expressions such as `What is 12% of 180?`. Numeric `%` and `-` operators and common written arithmetic phrases now escalate to the KeyMaker foundation instead of an unreliable small responder.
- Extend native regression cases for symbolic/worded arithmetic, percentages, round-trip speed and ordinary nonmathematical definitions. Preserve all 0.6.9 model roles, appearance and mission tracking.

## 0.6.9 — three-model routing, Retry identity, theme parity and Run info

- Preserve the six desktop/Web themes through one native appearance setting without overwriting client-local font and message-style preferences.
- Restore Run, Models & routing, Timing, Tools & context, and runtime timeline in a separate **Run info** tab; keep the default Output tab generated-text-only.
- Retry with the original responding model **and** its role, recorded from native events, including restored Web transcripts. An external foundation is no longer misclassified as the small responder.
- Keep the KeyMaker foundation lane immutable while subagents work. Distinct small and middle external models are enforced by the native API and Web choices; independently assigned internal/native addons may not switch or evict the foundation.
- Display actual per-response small-preflight, middle-reviewer and final-response model identities instead of showing today's global selection as historical execution.
- Retain conservative math/coding/tool routing, bounded handoffs, existing GPU settings, composer, Missions footer and responsive Chats behavior.
- Offline tests validate event roles, role-aware Retry, Web wiring, core invariants, and existing regressions. Windows builds, GPU memory pressure, actual three-model residency, and live inference still require target-machine acceptance.

## 0.6.8 maintenance — MSVC warning and Chats drawer

- Replace deprecated Windows `getenv` with RAII-owned `_dupenv_s` storage (C4996); retain POSIX `std::getenv` on other platforms.
- Preserve manually opened Chats across transcript updates, outside clicks, and panel toggles. Only an actual viewport narrowing below the existing auto-collapse threshold closes it automatically.
- Remove the 860px Chats-button lockout. On narrow windows the existing Chats panel overlays the main chat rather than shrinking it below its minimum width; the Chats button remains usable to close it.
- Keep drawer button state and `aria-expanded` synchronized; add regression checks for responsive transitions and remote-token source handling.

## 0.6.8 — Layered Agents, Review and Safety (source release)

- Optional three-tier chain: bounded specialist addon consultations → independent external middle reviewer (no tools) → Qwen KeyMaker foundation. Enabled only for suitable non-simple work with explicit model assignments; failure falls back to Qwen validating available evidence. No unverified model capability assumptions.
- Lab selectors for reviewer model, review depth Off/Automatic/Deep, KeyMaker analysis Auto/Fast/Deep (prompt-level guidance, **not** an unsupported llama.cpp flag). Config retained locally.
- Automatic configured specialist fallback for coding/planning when the controller cannot match a trigger; logs decisions and errors to native event store. Keeps raw generated-output-only right panel.
- Simple external responder failure before generating a single fragment escalates to foundation once. After any generated fragment, fails without duplicate reply.
- Checkpoint capture/restore validates and binds snapshot to original file; refuses traversal and wrong-file restores. Handle-relative Windows junction/TOCTOU hardening remains pending.
- Training input loader rechecks learning-ban hashes, including verified evidence already saved. Native ban/append operations serialized. In-flight training revocation and durable fsync remain pending.
- Remote API/control access requires a configured environment bearer secret when enabled, and browser same-origin validation. Local loopback use unchanged. Recommend TLS reverse proxy for any LAN usage.
- New C++ regression cases: hierarchy ordering, reviewer failure/disable, small model fallback behavior, checkpoint wrong-target denial. Existing Web smoke tests retained.
- Known unverified: Windows full CTest, live multi-model and MTP qualification, actual OS process confinement, Windows reparse-point tests, producer-specific reasoning flags, real tokenizer token IDs and durable server transcript UX. Carry all missions forward in `docs/FUTURE_MISSIONS.md`.

## 0.6.7 generated-output panel hotfix — 2026-10-07

- Display only model-emitted text/reasoning/tool-argument delta characters in the right panel. No event names, model configuration JSON, controller decisions, or telemetry wrappers; the native session event log is unchanged.
- Preserve the event resume cursor for hidden events; migrate old stored Web transcript outputs so earlier diagnostic JSON cannot reappear after refresh.
- Update the right panel in place to preserve scroll position and follow new fragments only while at the bottom. Keep the existing 24 unfinished missions and Qwen GPU configuration unchanged.

## 0.6.7 — 2026-10-07 — routing + composer correction

- Finish the interrupted 0.6.6 UI changes as a complete source release. Exact greetings (`hi`, `hey`, `hello`, `yo`, including punctuation and whitespace) route as **simple** when policy allows; complex and coding requests still escalate.
- Separate final-response automatic routing from capability-addon enablement in the Web request, native request, model-tool selection and routing preflight. Disabling addons does not disable a configured small responder.
- Explain disabled/missing external simple-model assignments in the model selector; an unconfigured small responder intentionally falls back to Qwen. External model IDs must come from discovery; native and internal model IDs are excluded.
- Hide the zero-attachment composer counter with an explicit `hidden` CSS rule. Keep all 24 open missions and all 0.6.6 security/compatibility limitations intact.
- Keep the user-saved Qwen GPU offload setting (including 99%). The previous Qwen-only GPU scale and 95% default for new profiles are unchanged. 99% already resolves to the rounded maximum `-ngl 29` on the 48-layer hint.
- Add C++ routing intent + agent tests and Web UI contract tests, bump desktop/Web/native/package versions to 0.6.7, extend the developer guide.

## 0.6.6 validation hotfix — 2026-10-07

- Fix CTest `runtime model validation rejects unsafe scheduling values` failing because legacy same-repository MTP detection was unreachable behind the missing-draft check.
- Restore the specific diagnostic for self-referential MTP repositories while keeping strict local-GGUF-only validation.
- Add cases for unrelated remote repositories, missing draft models, incorrect extensions, unsupported draft types, and valid local draft profile parsing.

# EngCoder Changelog

## 0.6.9 — Catch2 routing/handoff regression hotfix (2026-10-08)

- Restore the explicit `controller.decision` event with `source=session-core-only` when the user disables automatic routing. The no-addon fast path must not bypass session audit and persisted route status.
- Do not enter the Architect/addon decision path for an unforced request with automatic routing disabled. Forced addons still take the explicit route.
- Make safety-marker checks in untrusted small/middle handoff notes case-insensitive; capitalizing `Ignore Previous`, `Thinking Process`, etc. can no longer bypass the rejection policy.
- Extend handoff tests with mixed-case unsafe instructions, oversized notes/payloads, and allowed bounded notes.
- Preserve native model assignments, GPU settings, UI, chat persistence and all 25 unresolved missions unchanged.


## 0.6.6 — model routing, observability, and refresh recovery

- Expose a bounded, escaped **Raw output** stream in the right-hand activity panel: native text/reasoning fragments (when exposed), tool-argument deltas, selected model, tool status, and controller/subagent events. These are fragments, not necessarily raw tokenizer IDs.
- Checkpoint Web conversations during generation and before pagehide; resume polling the **same** native session/task event cursor after refresh rather than resubmitting. Native SQLite task events remain authoritative. Browser transcript persistence is still subject to localStorage quota.
- Introduce conservative **simple / complex / coding** intent classification, with configured external small responder for *simple text only*. Coding and ambiguity use the Foundation; small responders have no tool authority. Model assignments are observable in task events.
- Introduce **trainable shadow routing stub** (numeric-feature online multiclass learner with persistent examples, deterministic policy remains authoritative). This is not a production JEPA/JEV architecture or a trained correctness judge.
- Route explicitly selected LM Studio/external models to specialized addons at task time. Bounded preflight subagent consultations and model-initiated addon calls are advisory only, with no process or file authority. Invalid/no-model addon selections are excluded.
- Qualify MTP UI for an independent local GGUF draft and native draft support; remove the self-referential HF draft repository choice and unsupported n-gram switch. Reject missing, non-GGUF, or main-self draft paths *before stopping the running model*. Arbitrary LM Studio models are not cross-process MTP sidecars.
- Raise version to 0.6.6, include unit tests, add the development guide and future-mission register. Windows GGUF/backend compatibility and C++ dependency-linked builds remain to be checked on the target machine.

# Changelog

## 0.6.5 settings/runtime/learning safety follow-up

- Qwen GPU layer offload default is now **36%**. Untouched built-in 70%/90% presets migrate to 36%; Manual/custom profiles are preserved. The configured percentage, calculated `-ngl`, and loaded effective runtime are shown separately so stale 70% display state cannot masquerade as the active launch.
- The Web model form now binds to the active/first registered model on first load and autosaves every runtime setting, fixing the path where sliders changed visually but never updated the registered model profile.
- Loaded models expose **Reload / Apply settings**; secondary lifecycle actions live under a compact menu with **Unload**, **Clear temp cache**, and the explicitly destructive **Delete downloaded model…** action. No runtime-setting edit triggers an automatic reload.
- **Clear temp cache** never removes `.gguf` model weights or external/local model files. Downloaded-weight deletion is a separate confirmed operation and preserves the model profile.
- Every native-model setting in the shared Web/Desktop settings surface has an individual **Default** action. **Reset all settings…** is separate, explicitly confirmed, and does not delete model weights. Native ImGui mirrors per-setting defaults and full reset.
- Learning history now supports **Remove from learning** and confirmed **Remove + Ban**. A ban removes matching stored evidence and blocks the same prompt or response content at the central `LearningService::record` ingestion gate until explicitly unbanned; training reads the rewritten evidence file, so banned evidence cannot leak into later internal training.

## 0.6.5 runtime/session reliability follow-up

- Qwen Flash startup is now single-flight: a prompt submitted while the model is still loading waits on that startup instead of stopping and relaunching llama-server.
- Failed, stopped, or empty assistant turns expose Retry immediately. Approval-gated tools render **Approve once / Deny** as soon as the review event arrives, so a suspended `process.run` no longer looks like a dead-end error.
- Clipboard paste is text-first so stale image/file formats cannot replace text being pasted into the composer.
- Removed the duplicated expandable composer control block; the compact quick bar now opens the existing Models/Session Settings surfaces.
- Default Qwen GPU offload is 36% for desktop headroom; untouched 90%/70% recommended profiles migrate to 36%, while custom/manual profiles are preserved. Runtime profile edits can be saved while a model is loaded and apply on the next explicit load without forcing a restart.

### Local GGUF override repair
- Restored direct local-model override semantics: a non-empty Local GGUF path now applies to the next Load immediately instead of only taking effect after a profile save.
- Re-enabled Load when the default/cache source is incomplete so a local override can actually bypass it.
- Normalizes quoted Windows paths and any split shard back to `00001`, validates every shard, rejects zero-byte/incomplete split sets, and rejects `mmproj-*.gguf` as the main model.
- Native GUI and embedded Web UI now use the same override behavior.

- Qwen model cache reuse now probes the configured cache, Hugging Face cache environment roots, the standard per-user Hugging Face cache, and legacy Qwen/llama cache roots before downloading. A complete existing GGUF is launched directly with `-m`, preventing source-tree/runtime upgrades from redownloading the same model.
## 0.6.5 - 2026-10-06

- Added engine-facing inference on the same fixed `14321` runtime: `GET /v1/models`, `POST /v1/chat/completions`, and `POST /v1/responses`. The stable `engcoder-foundation` alias resolves to the selected Foundation model.
- Kept full agentic inference separate at `POST /api/tasks` plus session events so callers can choose direct low-overhead model inference or Architect/KeyMaker/addon/tool/memory execution.
- EngCoder Lab > Runtime now shows the UI, OpenAI-compatible inference, Responses, agent-task, and MCP exposure status explicitly. MCP remains an inbound tool integration capability, not an advertised inference route.

- Desktop/Web now uses dedicated loopback port **14321** by default instead of probing the old 8787 range; an explicit `ENGCODER_SERVER_PORT` override is still honored for standalone use.
- Closing the Windows shared Desktop/Web window now shuts down its embedded HTTP runtime reliably even if Edge keeps a background browser process alive; the dedicated Edge process tree is owned by EngCoder and cleaned up on exit.
- Replaced the Web/Desktop `process.run` approval dead-end with a real in-task approval continuation: the exact reviewed tool call is suspended, shown with arguments/reason, and resumes only after **Approve once**; **Deny** feeds a tool denial back to KeyMaker so it can choose a safer alternative.
- Added `POST /api/tasks/{task_id}/approval` with task/call matching so stale approval clicks cannot authorize a different tool call.
- Repository/base-directory discovery now strongly prefers `workspace.tree/read/search`; `workspace.tree` reports the actual workspace root, and read-only executable discovery (`where`/`which`) no longer trips the generic process approval path.
- Fixed the Catch2 chained-comparison assertion failure in `test_native_model_manager.cpp` and removed the Qwen Flash VRAM parser C4456 shadowing warning.
- Fixed Runtime Default model routing so an already-loaded EngCoder-native Qwen Flash model becomes the automatic System 1/Foundation candidate instead of falling through to the external LM Studio/Bionic provider.
- AgentRuntime now adopts the concrete model assigned by provider preparation before inference; telemetry and actual inference can no longer disagree on `auto` versus `qwenflash://...`.
- Removed phantom managed models: unavailable `engcoder://` controller/foundation/addon URIs are no longer registered as external models simply because configuration names them.
- Architect no longer pays a failed internal-model startup attempt on every request when no trained Architect controller exists; deterministic routing is used immediately.
- Internal addon lanes are enabled only when their trained component is actually active; native Qwen addon routes remain startable on demand.
- Loaded native-model state is reflected in discovery/architecture APIs and both desktop/web model selectors without requiring “Use as KeyMaker”; that button now remains an explicit persistent pin.
- Rewrote TaskScheduler for readability and clean shutdown; active tasks are cancelled before worker threads join.
- Reduced idle web event-poll overhead with adaptive 100–400 ms polling while retaining fast updates during active output.
- Removed a redundant model-registry request/scan from Web/Desktop model probing; the architecture snapshot now supplies the registry used by the selectors.
- Split settings persistence from conversation persistence so slider/input changes no longer JSON-serialize the entire chat history on every input event.
- Lab telemetry polling now pauses while the page is hidden and refreshes only the active Training/GPU/Learning tab instead of polling all subsystems continuously.
- Stale saved `engcoder://`, `internal://`, `threadspire://`, and `qwenflash://` UI selections are discarded when the native registry/discovery no longer exposes them, preventing old browser state from resurrecting nonexistent Architect/addon models.
- Added regression coverage for provider-assigned model adoption, phantom managed models, and untrained internal addons.
- Cleaned strict compiler warnings in touched runtime code rather than suppressing them.
- Added root-level Windows/Linux `build`, `check`, `run`, and `clean` wrappers around the canonical `auto` build flow; build logic remains centralized instead of being duplicated across scripts.

## 0.6.4 - 2026-10-06

### Qwen3.8 Q2 runtime compatibility and capability-safe acceleration

- Isolated the native Qwen startup crash below Architect/System 1/training: the bare Qwen3.8 server loads successfully, while every failing integrated command enabled the experimental MoE expert-cache policy. This release stops assuming that optimization is valid for a model/runtime pair until it proves itself.
- Added an EngCoder-owned compatibility patch for a known Qwen3.8 Q2 layout issue: physically padded routed `ffn_down_exps` tensors. The patch preserves a padded GGUF expert-down width when present and zero-pads the activation before the down projection. This is treated as a prerequisite/candidate fix, not proof that every Q2 cache implementation is safe.
- Runtime manifests now advertise `qwen38-q2-padded-down-v1`. In Auto mode, patched runtimes may probe expert caching once; EngCoder persists success/failure per model + runtime recipe. A failed cache probe that is recovered by the same-context no-cache launch is remembered and skipped on future launches. Older QFR/EngCoder binaries remain stable no-cache backends.
- Added explicit expert-cache modes: **Auto / Off / On**. Auto is the default; Off is the stable baseline; On is a deliberate manual force.
- Restored Hugging Face `-hf owner/repo:quant` launch semantics for catalog models, matching the model-loading path that was proven end-to-end. Completed model shards are reused from `LLAMA_CACHE` and are not downloaded again.
- Added same-context recovery before context reduction: first disable expert cache, then use a minimal llama.cpp compatibility command that keeps EngCoder ownership and the requested context while removing optional backend switches. Only after that does the bounded context ladder run.
- The Qwen runtime installer applies and verifies the compatibility patch on the pinned backend before building, and records the exact runtime recipe/feature manifest. Web UI distinguishes a compatibility baseline from the current patched runtime and offers **Upgrade Qwen runtime** instead of treating any llama-server binary as current.
- Added Qwen acceleration/runtime qualification status to the shared Web/Desktop Model Library and exposed the actual full/minimal launch profile used at runtime.
- Preserved Recommended / Automatic / Manual GPU tuning, GPU offload percentage, exact-layer control, KV placement, context compaction and the existing BotFace/Desktop interface.
- Kept EngCoder's training/agent systems above the Qwen execution layer: no standalone QFR application architecture is imported into the product.
- Child-process diagnostics retain output tails and Windows access-violation decoding; Windows environment access remains warning-clean via `_dupenv_s`.

## 0.6.3 - 2026-10-06

- Fixes the native Qwen Flash Windows startup crash reported as exit `-1073741819` / `0xC0000005` by repairing legacy EngCoder runtime copies from the exact pinned standalone QFR build when that known-good build is present.
- Replaces recursive/arbitrary QFR backend discovery with the deterministic pinned path `build-167742d9dfa0/bin/Release/llama-server(.exe)`.
- Adds a verified runtime manifest (`runtime.json`) carrying the pinned backend commit and `qfr-vcpkg-2026.07.29` build recipe; verified EngCoder runtimes are preferred, legacy unverified runtimes remain compatibility-only.
- Makes the EngCoder Qwen runtime installer reproduce the proven QFR build recipe by using the same pinned vcpkg baseline/toolchain and the official verification-only `cuda` manifest dependency on Windows and Linux.
- When a Hugging Face GGUF is already fully cached, launches the first local GGUF shard directly with `-m` instead of re-entering Hugging Face repository resolution on every startup. The UD-Q2_K_XL three-shard set is validated as complete before direct launch.
- Replaces the ineffective 0.6.2 retry (which kept 81,920 context) with an actual recovery ladder: 77,824 hardware-aware, 65,536 diagnostic, then the original proven 32,768 QFR compatibility profile. The active recovered context is reported back to EngCoder's context manager.
- Decodes Windows access-violation exits in startup diagnostics so `0xC0000005` is explicit instead of appearing only as a signed decimal code.
- Keeps the 0.6.2 Recommended / Automatic / Manual tuning modes, GPU-offload controls, BotFace UI parity, and `_dupenv_s` warning cleanup unchanged.

## 0.6.2 - 2026-10-06

- Restores **Recommended** Qwen Flash startup to the exact proven standalone QwenFlashRuntime policy instead of forcing additional 0.6.x KV/prompt-cache/context-shift/reasoning switches.
- Adds three explicit native tuning modes: **Recommended**, **Automatic**, and **Manual**.
- Adds hardware-aware Automatic tuning from detected system RAM, NVIDIA VRAM and logical CPU threads.
- Adds Manual GPU offload percentage (0-100%), model-layer hint mapping, and exact `-ngl` override; exact layer/all values take precedence over the percentage.
- Keeps sparse MoE expert residency independent from ordinary layer offload through the existing GPU expert-cache and pinned-host budgets.
- Adds a conservative one-shot recovery profile for Recommended/Automatic startup failures while never silently changing a Manual profile.
- Startup failures now retain the exact llama-server command and process exit/error tail for diagnosis.
- Replaces direct Windows `getenv` usage in the Qwen runtime with `_dupenv_s` behind a cross-platform environment helper, eliminating MSVC C4996 without disabling CRT safety warnings.
- Fixes Hugging Face cache reporting to count unique matching GGUF payloads rather than blob/snapshot aliases.
- Extends the existing BotFace Web/Windows desktop and SDL/ImGui fallback configuration surfaces with the same populated tuning controls; no chat/composer layout was replaced.
- Bumps native runtime-model manifests to revision/format v3 while automatically migrating prior built-in profiles.

## 0.6.1 - 2026-10-06

- Stabilized native Qwen Flash startup on 64 GiB / 16 GiB systems.
- Changed the recommended 5080 profile to 81,920 total context, 1 slot, 4,096 reserve, 97% -> 72% EngCoder compaction (~75.5K active tokens before compaction).
- Automatically migrates the untouched 0.6.0 built-in 98,304-token profile while preserving user-customized profiles.
- Adds one automatic startup retry with an 81,920/1-slot CPU-pinned-KV safety profile when a heavier configured launch exits before health readiness.
- Reports the actual active context/slot profile back to the agent harness after a safety fallback, keeping compaction/output budgeting correct.
- Preserves explicit cancellation: stopping a model load never triggers the safety retry.
- Runtime failures now retain the useful tail and exit code instead of surfacing only `llama_server: initializing ...`.
- Corrected Hugging Face cache accounting so the UI reports unique matching GGUF payload size rather than double/triple-counting blobs/snapshots.

## 0.6.0 - 2026-10-05

### Native Model Library and Qwen Flash System 1 runtime

- Added `qwenflash://` as a first-class EngCoder model/runtime type rather than an LM Studio/OpenAI-provider alias.
- Added native model catalog, Hugging Face/local-GGUF registration, download/install state, load/unload, cache removal, and runtime provisioning.
- Integrated the proven pinned Qwen Flash sparse/MoE backend without modifying the standalone QwenFlashRuntime; EngCoder can adopt a copy of an existing built backend and reuse an explicitly shared Hugging Face cache.
- Qwen Flash can serve as the resident KeyMaker/System 1 Foundation while retaining the existing Architect, addon, MCP/tool, episodic-memory, anti-looping, verification, workspace, and session harness.
- Added populated RTX 5080 / 64 GB defaults: 98,304 total context, one slot, 8,192 reserve, 90%/70% automatic compaction policy, 12/12 CPU threads, 1024/256 batch sizing, 6,144 MiB GPU expert cache, 12,288 MiB pinned-host expert budget, q4_0 CPU-pinned KV, FlashAttention, mmap/lazy loading, and phase-aware workspace.
- Added context presets through the model's 262,144 native window and visible effective-context-per-slot calculation/warnings.
- Added explicit EngCoder-owned automatic context compaction before each foundation turn; exact older history remains persisted/searchable and compaction emits runtime telemetry.
- Added native-context output-budget protection: oversized requested completions are capped to the actual remaining per-slot window after compaction/tool definitions and emit `context.output_limit_adjusted` instead of failing the backend.
- Added optional Qwen draft-MTP/speculative controls without enabling experimental speculation by default.
- Added Model Library UI actions for configure/download/load/use-as-KeyMaker/unload/clear-cache while keeping LM Studio/Bionic and internal KeyMaker lifecycle independent.
- Foundation/KeyMaker selection is now native EngCoder state (`models/selection.json`), so a Qwen Flash selection survives refresh/restart and is shared by Web, Windows desktop app mode, CLI/server execution, and the SDL/ImGui fallback instead of living only in browser storage.
- Preserved the existing BotFace-derived Web/Desktop chat surface; the SDL/ImGui client exposes the same native model capabilities through frontend-appropriate controls backed by the same RuntimeHost APIs rather than duplicating model logic.
- Retained the existing Windows/Linux clean-build-test-run entrypoints (`auto.bat` / `auto.sh`) and added platform-specific pinned Qwen runtime installers behind the shared native model manager.
- Runtime model manifest format advanced to `engcoder.runtime-model.v2`.

## 0.5.3 - 2026-09-20

### Explicit model assignment and quieter run presentation

- Every response now surfaces the assigned KeyMaker model directly in the collapsed run line instead of requiring the user to infer it from residency/debug events.
- External `auto` assignment is resolved during provider preparation and persisted as the concrete selected model whenever the provider exposes one.
- Model-response telemetry carries the provider-assigned model explicitly, with the requested model retained separately for diagnosis.
- Addon telemetry records the model assigned to each activated/advisory capability, so Foundation, Architect, and addon model ownership are all visible for a run.
- Architect telemetry now distinguishes model attempted, model accepted for routing, and deterministic/session routing, so a fallback is not presented as if the Architect model made the decision.
- Replaced the noisy inline glyph/event timeline with a compact run summary: Model, Route, Architect, Tools, Addons, and Status.
- The full timestamped runtime event timeline remains available under **Run info** for diagnostics, without decorative per-event markers.
- Advanced product metadata and packaging to **EngCoder 0.5.3**.

## 0.5.2 - 2026-09-20

### Per-message run telemetry and chat/session lifecycle

- Replaced the old Stream/Activity/Answer drawer with **Run info** for the selected assistant response. The answer is no longer duplicated in the side panel.
- Added Architect model, route source, selected capability, confidence, and measured Architect duration.
- Added KeyMaker model/turn count/inference duration, time-to-first-token, output-token estimate/TPS, addon time, memory retrieval count, and tool call/completion/failure/block/approval counts with measured execution time.
- Runtime events now expose persisted timestamps; model/controller/addon/tool events expose measured durations where available.
- Run telemetry and its optional runtime timeline are stored on the assistant message inside the owning chat, restored with that chat, and removed with it.
- Clearing or deleting a Web chat now deletes its associated native SQLite runtime session (messages + events) and rotates the cleared chat to a fresh runtime session.
- Advanced the full product version to **EngCoder 0.5.2** across native build metadata, Web UI, protocol, training worker, documentation, and release packaging.

## 0.5.1 - 2026-09-20

### Unified local Web app and remote serving

- Simplified EngCoder to one native runtime serving one canonical Web interface. Desktop opens that same served page; browser use points at the same runtime rather than selecting or spawning a second backend.
- Removed the Current EngCoder / Local Server / Custom endpoint selector from normal Session Settings. The page now talks same-origin to the runtime that served it.
- Added a **Serve remotely** control under EngCoder Lab > Runtime. LAN access is disabled by default and can only be enabled or disabled from a loopback/local EngCoder page.
- Provider credential changes are also local-only. Remote serving is intended for trusted LANs because enabled remote clients can use EngCoder runtime capabilities.

### UI and screen-real-estate cleanup

- Header identity is now **EngCoder Studio v0.5.1**; the old `Architect · local model runtime` subtitle was removed.
- Dictation/Talk controls now derive normal, hover, active, and listening states from the selected theme palette instead of retaining dark-theme button colors.
- Reworked the shared Desktop/Web shell around a fixed compact app header instead of the small floating centered title card.
- Increased useful chat width on large and 4K displays while keeping the composer, messages, activity, and dialogs visually bounded and readable.
- Tightened Session Settings and EngCoder Lab layouts so informative controls remain available without oversized cards or wasted vertical space.
- Fixed message/error wrapping for CRLF, embedded NULs, long provider errors, code blocks, and narrow message regions.
- Reduced duplicate provider-failure text in activity and the composer error surface.
- Rebalanced Matrix Green into a normal near-black EngCoder theme with green signal/accent colors. Normal prose uses the system UI font; monospace is reserved for code and telemetry.

### Provider connection recovery

- Fixed external model auto-selection so the sentinel `auto` is resolved to a concrete LM Studio/Bionic model key before load-state checks and inference; an installed but unloaded selected model can now be loaded automatically on the first prompt.
- Propagated task cancellation through `ModelRequest` into libcurl. **Stop** now aborts the active provider HTTP stream instead of only stopping EngCoder's UI/task polling while the local model keeps generating.
- Auto transport now issues exactly one Chat Completions generation. Responses transport remains explicit-only, preventing a late Chat transport/parser failure from replaying the same prompt as a second generation.
- Added model-provider status and token controls under EngCoder Lab > Runtime instead of exposing model-server credentials in ordinary chat settings.
- A provider token can be applied or cleared at runtime without restarting EngCoder; it is retained only in the native process and is never stored in browser local storage.
- LM Studio HTTP 401 failures now produce a compact actionable message directing the user to EngCoder Lab > Runtime instead of repeating the complete transport failure throughout the chat.
- Added provider regression coverage for applying a token at runtime while retaining automatic model reload after provider eviction.

## 0.5.0 - 2026-09-20

### BotFace Web UI restoration

- Restored the chat-first BotFace-derived Web experience as the canonical shared Desktop/Web frontend instead of letting model/training controls consume Session Settings.
- Split **Session Settings** from a new **EngCoder Lab** surface. Session Settings now covers runtime selection, session instructions, temperature/output limits, history/context, voice, activity, appearance, and chat utilities.
- EngCoder Lab now owns Architect/KeyMaker/addon assignments, GPU residency, KeyMaker training, and verified learning/memory.
- Preserved chat history, editing/regeneration, attachments, voice, activity drawer, copy/verify actions, themes, and local persistence.
- Kept the browser as a presentation/control layer only; Architect and KeyMaker remain owned by the native C++ runtime.

### Session-control parity

- Added native task fields for session instructions, temperature, maximum output tokens, history inclusion, and history-turn limits.
- Session instructions augment the native EngCoder system contract rather than replacing it.
- Added provider `max_tokens` / Responses API `max_output_tokens` propagation.
- The Web `Stream responses` toggle now controls live text painting while still consuming runtime events and presenting the final answer.
- Custom runtime URL input is hidden unless Custom runtime mode is selected.

## 0.4.3 - 2026-09-19

### LM Studio reload lifetime fix

- Fixed a real use-after-lifetime bug in `LmStudioProvider::ensure_model_loaded()` discovered by the Windows reload regression test.
- The provider previously saved a pointer to an element of a temporary `nlohmann::json` array returned by `value("models", ...)`; after the loop ended that pointer was dangling and MSVC surfaced it as `[json.exception.type_error.306] cannot use value() with null`.
- Reworked model-state inspection to iterate the persistent `models` array in the parsed response and retain only boolean state (`installed` / `already_loaded`), so no JSON element pointer escapes its lifetime.
- Hardened `loaded_instances` inspection: absent, null, or non-array values are treated as not loaded instead of raising an exception.
- Preserved the explicit `/api/v1/models/load` recovery path so a selected LM Studio model is reloaded after TTL/manual eviction before inference.
- The user's 0.4.2 Windows run built all four targets successfully and passed 15/16 tests; only the reload regression test failed, directly exposing this runtime bug. The build correctly refused to launch EngCoder after the failed test.

## 0.4.2 - 2026-09-19

### Windows Catch2/MSVC test-build fix

- Fixed `test_lm_studio_provider.cpp` failing to compile under MSVC/Catch2 because a ternary expression was passed directly to `INFO(...)`.
- Diagnostic strings are now computed before `INFO`, preserving failure context without relying on compiler-sensitive macro parsing.
- Preserved the 0.4.1 loopback proxy bypass, RAII test-server shutdown, 30-second per-test timeout, and production LM Studio reload behavior.
- The user's 0.4.1 Windows run confirmed `EngCoderServer.exe`, `EngCoder.exe`, and `EngCoderCLI.exe` build successfully; only `engcoder_tests` was blocked by this portability error.

## 0.4.1 - 2026-09-19

### Windows test-hang fix

- Fixed the LM Studio unload/reload regression test hanging indefinitely on Windows when its local HTTP request failed before normal server shutdown.
- Added unconditional RAII shutdown for the in-process test server and removed Catch2 assertions from HTTP worker threads.
- Loopback model traffic to `127.0.0.1`, `localhost`, and `::1` now explicitly bypasses system/environment proxies in libcurl.
- Added a 30-second hard timeout to every Catch2-discovered CTest case so a future deadlock fails the build instead of wedging `auto.bat`.
- Preserved the production reload behavior: an installed but unloaded LM Studio model is reloaded before inference when automatic model loading is enabled.

## 0.4.0 - 2026-09-19

### Unified product architecture

- Standardized current naming on **EngCoder**, **Architect**, **KeyMaker Foundation**, and **KeyMaker Addons**.
- Public internal model IDs are now `engcoder://architect` and `engcoder://keymaker`; older generation identifiers are translated privately at compatibility boundaries.
- Kept Architect as the low-latency routing/orchestration layer and KeyMaker as the local learned-model/training family.
- Preserved modular C++23, documentation, planning, tool-use, and reranking addons.

### Desktop/Web parity

- Made the shared `projects/web` interface the normal Windows desktop surface as well as the browser interface.
- Desktop `EngCoder.exe` owns the native runtime and embedded loopback adapter; a separately launched AI server is not required.
- The desktop app-mode browser profile is now unique per EngCoder process so an existing Edge process cannot steal/reuse the owned UI lifetime.
- Kept `--native-ui` as a diagnostic ImGui fallback rather than a second product UI.

### Appearance and multi-monitor support

- Preserved EngCoder Dark and EngCoder Light.
- Added Windows System, Windows Dark, Windows Light, and Matrix Green.
- Windows System follows the OS/browser color preference and updates live.
- Added better light/dark control-surface parity across chat, composer, header, activity, drawers, and settings.
- Native fallback now prefers Segoe UI Variable and Cascadia Code on Windows.
- Native fallback opts into Per-Monitor V2 DPI awareness before SDL initialization and retains live display-scale refresh for mixed-DPI/multi-4K setups.

### Provider model recovery

- Added external-model auto-load recovery. When LM Studio exposes its native v1 model-management API, EngCoder checks the selected model's load state before inference and reloads it after TTL/manual eviction when necessary.
- Added `ENGCODER_MODEL_AUTO_LOAD` (default enabled).
- Provider-managed endpoints that do not expose native load state continue without forced model management.


### Release hardening

- Renamed active native configuration from legacy expert terminology to `AddonConfig`/`addons` and removed the unused old expert registry from the build. Deprecated `expert.*` tool names remain compatibility aliases only.
- Fixed KeyMaker Foundation curriculum matching so ordinary `domain: english` bootstrap records are admitted when the selected scope is `keymaker-foundation`. A one-step micro candidate now executes instead of failing with an empty training set.
- Made the Windows desktop Edge profile persistent under the EngCoder app-data directory so chat/theme/settings survive desktop restarts while remaining isolated from the user's normal Edge profile.
- Added a single-instance desktop guard and source-tree Web-root discovery so directly launching a built `EngCoder.exe` can locate the shared UI from the standard build tree.
- Removed duplicate `/api/engcoder/*` route registrations and updated all active project/version metadata to 0.4.0.

### Existing 0.3 functionality retained

- Internal random-weight model initialization and candidate training.
- Foreground-inference training preemption/resume.
- NVMe -> RAM -> VRAM internal residency with pinned Architect/KeyMaker and pageable addons.
- Verified immediate memory separated from weight learning.
- Automatic model discovery/dropdowns, GPU residency UI, training controls, voice, attachments, chat history, answer verification, and compact activity telemetry.

### Runtime backend clarity and progress
- Renamed the Qwen runtime action to Install/Update Qwen Runtime Backend so it is clearly distinct from downloading or loading model weights.
- Added visible progress for model-weight downloads and runtime-backend provisioning in both Web and native GUI surfaces.
- Runtime provisioning now publishes phase/percentage/detail state while checking CUDA, preparing vcpkg, fetching backend source, configuring, building, and staging.
- Corrected malformed hunk counts in the Qwen3.8 Q2 compatibility patch that previously caused `git apply` to fail with `corrupt patch at line 28`.

## 0.6.5 composer context-bar completion
- Restored the `+` button as an actual context/action menu instead of a direct file-picker shortcut.
- Added source/text file attachment, folder attachment, clipboard-text context, drag/drop, and file-paste handling with explicit size/count limits.
- Added a compact quick bar for routing mode, KeyMaker visibility, history toggle, runtime/GPU access, and advanced session controls without replacing the existing composer row.
- Added explicit Auto / Foundation-only / C++23 / Planning / Documentation / Tool-use routing presets that map onto the existing Architect/addon controls.
- Image input remains explicitly unavailable on `/api/tasks` until the agent vision transport is implemented; EngCoder no longer implies that an image was delivered when it was not.

## 0.6.6 maintenance — 2026-10-07

- Fixed MSVC C4834 in built-in model-profile migration by consuming the `[[nodiscard]]` repair result. Startup now writes model manifests only when migration/normalization changes data.
- Model-profile writes now stage a unique, checked temporary file and replace the original through Windows `MoveFileExW` or POSIX atomic rename. The existing manifest remains intact if serialization or replacement fails; incomplete built-in manifests are retained for diagnosis rather than overwritten with defaults. Power-loss durability and concurrent-process file locking remain future missions.
- Corrected native model removal error handling; a failed filesystem deletion is no longer reported as successful, and the protected built-in profile reports `removed=false` explicitly.
- Added an unobtrusive **Missions** control in the main composer footer. The read-only modal shows every outstanding P0/P1/P2 item from a generated snapshot of `docs/FUTURE_MISSIONS.md`, with search and priority filtering. Update with `python scripts/sync_missions.py`; `--check` detects out-of-sync release artifacts.
- Brought forward unresolved 0.6.5 objectives covering cache operation races, checkpoint traversal, runtime fault injection, inference-chain acceptance, training preemption, and Windows composer tests.
- Extended native model-manager tests for no-op startup persistence and preservation of corrupted manifests. Real MSVC execution must still be run on target Windows hardware.

## 0.6.9 Run-info timing quick reference hotfix (2026-10-08)

- Added Total time and estimated generation tok/s immediately under State in Run info; the full Timing section is unchanged.
- The TPS marker is explicitly approximate, based on rendered text length, not actual llama.cpp tokenizer usage; no unsupported measured-speed claims.
- Persisted task-start and first-output timestamps so duration and speed displays survive a browser refresh without silently restarting the clock.
- No model, GPU, route, theme, or math-rendering configuration changes.


## 0.6.9 Qwen Live 2 — retained completed output (2026-10-09)

- Keep the **Qwen live** monitor visible while idle, even when clicking the main composer or chat; only explicit close/toggle closes it. Run info and normal Output retain their previous behavior.
- Show **Idle · last output retained** after the active Qwen generation completes, without clearing any generated fragments until the next generation starts.
- Maintain a bounded, in-memory **latest Qwen invocation** snapshot in the native provider journal, independent of its prunable event ring. The authenticated `/api/engcoder/qwen-output` endpoint includes this snapshot so delayed/served clients and refreshed pages can recover the most recent generation without a missing start event.
- Retain the 96 KiB display limit, 512 KiB/2048-event journal bounds, existing tool/MCP coverage, and the existing model/GPU configuration. Send full recovery output only on first load or after missed events, not on every 500 ms idle poll; preserve UTF-8 boundaries for long output.
- Add C++ and Web regression coverage for idle retention, new generation reset, completed refresh recovery, and overlapping requests. This is a source hotfix; Windows and live MCP operation must still be verified.
