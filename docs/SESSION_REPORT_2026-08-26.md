# EngCoder unattended improvement session — 2026-08-26

## Initial repository state

- Permanent worktree: `C:\Users\iammi\.codex\worktrees\c2e6\ThreadSpire`
- Branch: `codex/threadspire-unified-workbench`
- Initial commit: `8d82dca` (`Fix failed reply retry and exact learned recall`)
- Initial Git status: clean
- Repository-local `AGENTS.md`: not present
- Other retained checkout: the original detached Downloads checkout at `ad94ad4`; it was not modified.

## Baseline build and tests

Official commands from `BUILDING.md`:

```powershell
cmake --fresh --preset windows-vs2022
cmake --build --preset windows-vs2022-release --clean-first -- /m:1
ctest --preset windows-vs2022-release
```

Baseline result on Visual Studio 2022 / MSVC 19.44.35227.0: clean Release build passed. CTest passed 4/4 (`threadspire_ai_tests`, `threadspire_workbench_tests`, `threadspire_base_footprint`, and `threadspire_curriculum_guard_smoke`).

## Problems discovered

### Arithmetic is routed and learned as conversation

`TaskAnalyzer` has no calculation task. A prompt such as `what's 1 + 1?` falls through to the short-conversation path. Accepting that reply can therefore create an `english-conversation` memory instead of executing and verifying an arithmetic operation. The active 22.5M-parameter internal model has English, C++23, debugging, and planning experts, but no mathematics expert.

Planned correction: a strict local arithmetic parser/evaluator, a truthful deterministic-calculation route that contacts no model, and a learning-admission rule that prevents calculator outputs from entering retrieval memory or weights.

### Verified local arithmetic implemented

- Added a bounded native arithmetic parser for `+`, `-`, `*`, `/`, `^`, unary signs, parentheses, decimals, and scientific notation.
- Added the `calculation` task class and a deterministic router stage. Arithmetic works with every AI disabled and never sends the prompt to an internal or connected model.
- Added clean chat labeling (`ENGCODER — VERIFIED CALCULATION`), removed fake model-speed reporting, and disabled Accept/Reject learning controls for calculated results.
- Local arithmetic does not stop automatic model setup or background teacher generation.
- Added a classifier-level hard boundary: calculator results are rejected from retrieval memory and model-weight training even if submitted through another code path.
- Added regressions for `what's 1 + 1?`, precedence, parentheses, right-associative exponentiation, division by zero, `What's up?` remaining conversation, execution with all models disabled, and learning rejection.

### Installed-size terminology hides the real dependency split

The installed internal PyTorch pack occupies 4,301.61 MiB; 4,188.04 MiB is `torch`, predominantly CUDA/cuDNN libraries. The active model weights are 85.95 MiB and the base executable is 4.80 MiB. Current built-in inference still launches the pack's Python `model_worker.py`, so the documented compact-native-inference boundary is not yet realized for the operational internal model.

### Model generation retention is expensive

Six full F32 generations occupy 515.74 MiB. The active generation is `g000011`; later candidates remain unpromoted. Safe cleanup must retain the active generation and a deliberate rollback policy rather than treating every generation as the current model.

## Verification record

This report is updated as changes are implemented. Claims about other platforms remain unverified until their configure/build/test paths are actually exercised.

- Incremental Visual Studio 2022 Release build after the arithmetic implementation: passed.
- Release CTest after the arithmetic implementation: 4/4 passed, including the expanded core suite and the 100 MiB base-footprint check.

## Work completed after the user handoff timestamp

### Storage, model, and RAM transparency

- Replaced repeated secondary directory walks with a single-pass classifier during the five-second storage sample. The UI no longer rescans the 17,000-file private runtime several times per refresh.
- Added exact accounting for active weights, every stored model generation, inactive candidate/rollback generations, the internal PyTorch pack, PyTorch itself, compiled CPU/GPU libraries, PyTorch program files, and private Python/support files.
- Parsed the promoted generation's real manifest and config through the existing JSON parser. The live report now shows the exact parameter count, completed steps, verified target-token count, context length, layer count, training-data focus, and learned areas.
- Changed the installed-pack card to say explicitly that the 4.20 GiB is training software, not model weights or learned knowledge. The active-model card now shows 85.95 MiB of weights and 22.5M parameters; all-model storage shows six retained versions separately.
- Preserved and visually verified EngCoder-owned live RAM reporting, including app RAM, helper-process RAM, private committed memory, current activity, growth, and the explicit exclusion of LM Studio and other unrelated AI applications.
- Brought the legacy storage declaration into parity with the named `threadspire.storage` module so stale declarations cannot silently disagree with module consumers.
- Added a synthetic exact-byte regression fixture covering Windows-style private-runtime paths, PyTorch library/code separation, active and inactive generations, manifest metadata, cards, and file totals.

### Autonomous visual QA and interface corrections

- Launched the built Release executable under a DPI-aware QA harness, captured Chat and Files & Activity at the minimum supported 980x680 logical size (1470x1020 at the active 150% display scale) and at 2100x1250, inspected the images, and closed only the spawned QA process.
- Removed the tab-strip overflow that created navigation arrows and hid the first tab after selecting Files & Activity. All five primary tabs now remain visible at the minimum size.
- Reclaimed a full row of conversation height at the minimum size by fitting per-reply controls into one responsive toolbar and reducing the compact prompt ceiling. The message area, prompt, learning controls, and bottom actions remain distinct and usable.
- Removed the storage report's horizontal scrollbar, enabled wrapping, and increased its share of the Files & Activity page while reserving usable space for the live log.
- Corrected the contradictory startup text. A trained model whose answer service is launching is now reported as `MODEL READY — answer service starting automatically`; it is no longer simultaneously called ready and not ready.
- Visually verified the dark card treatment, tab visibility, storage cards, model facts, live RAM, and startup wording at both tested sizes.

### Post-handoff verification

- Visual Studio 2022 Release build after the storage and first visual-QA pass: passed.
- Release CTest after the storage and first visual-QA pass: 4/4 passed.
- Final Visual Studio 2022 Release rebuild after the compact-toolbar polish: passed. Final Release CTest: 4/4 passed, including the base-footprint and curriculum-guard checks.

### Safe model-history compaction

- Audited the existing post-training cleanup and found that it removed entire old generation directories, which discarded the audit evidence along with redundant weights.
- Replaced that behavior with transactional weight compaction. EngCoder protects the active model, the just-completed generation, and the newest prior verified generation for rollback/repair. Older redundant weights and their matching checkpoints are removed only after an atomic manifest update is published.
- Preserved each compacted generation's manifest, configuration, metrics, provenance, and retention facts. Interrupted compaction restores the temporary weight unless the audit manifest was already committed.
- Added exact storage counters for full model-weight copies and audit-only generation records. The Files & Activity card and report now explain the retention policy in plain language instead of treating every historical record as another full model.
- Extended tests for retained audit records, exact generation counts, protected weights, and the absence of whole-generation deletion.
- Ran an isolated five-generation retention simulation outside live AppData. Three protected weight copies survived, two redundant copies were compacted, exactly 201 fixture bytes were reclaimed, every audit file remained, and a second run was idempotent.
- Repeated visual QA on the built Release executable at the minimum supported size and a wide size. The Chat and Files & Activity pages remained dark, readable, non-overlapping, and retained all five primary tabs. The live installation still truthfully shows six full copies because no live AppData mutation was performed; automatic compaction takes effect after a future completed learning pass.

Verification for this checkpoint: Python bytecode compilation passed, `git diff --check` passed, the Visual Studio 2022 Release build passed, and Release CTest passed 4/4.

### Compact native inference

- Replaced the normal built-in answer host with a standalone C++23 executable. It reads the active generation's manifest, configuration, and F32 safetensors directly; validates the active pointer, safe leaf paths, tensor shapes, format, and SHA-256; and implements the exact RMSNorm, RoPE attention, SwiGLU, per-skill expert, tied-embedding, KV-cache, and sampling path used by the trained model.
- Preserved the established loopback provider contract: health, JSON completion, streaming completion, cancellation-compatible shutdown, usage, generation provenance, and quality metadata.
- Ported verified greeting/reversible-answer retrieval and the structural/English quality gate. Invalid UTF-8, control bytes, repetition loops, malformed vocabulary, and unusable drafts are withheld instead of reaching Chat.
- Packaged the host as the explicit compact-native-inference capability pack. The app installs that bundled pack transactionally on demand and starts it before checking the separate Python/PyTorch learning pack. Normal built-in answers no longer load Python or PyTorch.
- The Release native host is 267,264 bytes. Its imported libraries are Windows system libraries plus the Visual C++ runtime; it has no Python or PyTorch import. EngCoder.exe is 5,073,920 bytes, remaining far below the 100 MiB base budget.
- Added a deterministic native engine contract test for matrix multiplication, RMS normalization, and strict UTF-8 validation, plus source-contract coverage that normal inference launches the transactional native pack and does not launch Python.
- Exercised the actual local HTTP server against active generation g000011: health passed, JSON returned the verified Hello! response, SSE ended with [DONE], and /shutdown exited cleanly. A free-form neural smoke generated at about 171 byte tokens/s and its malformed draft was correctly rejected by the native quality gate.

Verification for this checkpoint: Visual Studio 2022 Release build passed with /W4 /WX; Release CTest passed 5/5; git diff --check passed. The active model was read for smoke testing, but live AppData was not modified.
### Safe cleanup controls and runtime wording

- Added a dry-run `Preview cleanup` action and an explicitly confirmed `Clean safely` action to Files & Activity. Both use the existing quota-aware cleanup engine in a background operation and refuse to overlap a live storage scan.
- The UI now reports the exact projected or reclaimed bytes and item count. A preview states that nothing was deleted; execution preserves active work, promoted models, locks, and protected rollback copies.
- Corrected the remaining stale storage description: the large PyTorch capability pack runs only while learning, while normal built-in answers use the compact native engine. Installed packs are described as software separate from model weights.
- Repeated minimum-size and wide visual QA. The five tabs, cleanup controls, eight storage/RAM cards, report, and log panel remained visible and non-overlapping in the dark interface.
- Verified after QA that no EngCoder, ThreadSpire, Python, or native-host process was left running. The inspected model-server log ended in a successful `/shutdown` request.

Verification for this checkpoint: Visual Studio 2022 Release build passed; Release CTest passed 5/5; `git diff --check` passed.
### Plain-language capabilities and optional add-ons

- Replaced the remaining jargon-heavy Capabilities controls with `Set up built-in capabilities`, `Area to improve`, `Improve selected area`, and `Undo last area update`. The live capability report now describes lightweight/full updates in plain language and maps the legacy internal URI to `EngCoder built-in` before display.
- Added an advanced optional-add-on picker for fine-tuning, image generation, and audio generation. It shows projected download/install size, dependency status, a prepared-source folder, and installed size.
- Add-on installation runs in its own cancellation-aware background operation through the existing transactional pack manager. Choosing or adding one neither starts a task or host, requests training, changes the selected AI, nor cancels active Chat work. Previous installed copies survive cancellation or validation failure.
- Corrected the last stale `Connections` tab reference and updated README/BUILDING terminology, storage-card count, pack source contract, and locations.
- Visually inspected the real Capabilities page at 1470x1020 (980x680 logical at 150% DPI) and 2100x1250. All five tabs, plain-language update controls, add-on selector/path/actions/status, and scrollable report fit without overlap. No EngCoder-owned process remained after shutdown.

Verification for this checkpoint: Visual Studio 2022 Release build passed; Release CTest passed 5/5.
### Native serving during background learning

- A real minimized-GUI startup test exposed a lifecycle-order bug that source tests had missed: an active shadow-learning job returned before the promoted-model serving branch, so Chat could remain on connected AI while claiming the compact answer service was starting. The native pack was never published.
- Moved the promoted-model branch ahead of shadow-learning backpressure. The last promoted model now starts through the native pack while a separate candidate learns, satisfying the serving/promotion safety boundary.
- Rebuilt and launched the actual Release GUI against the live pre-reset state. EngCoder transactionally installed the compact native pack, published its readiness file, and answered `/health` as `engcoder-native-v1` for active generation `g000011`. Closing the GUI shut down the native host; no EngCoder-owned process remained.
- Ran the full optional-pack `internal_model_smoke.py` and `internal_server_smoke.py` suites with the installed PyTorch runtime. Persistent checkpoints/resume, English filtering, repair lineage, promotion, active and isolated shadow servers, JSON/SSE, and shutdown all passed.

This live start test intentionally added only the small compact-native pack to the legacy AppData tree; the authorized fresh reset remains deferred until the final clean build, CTest, smoke, and footprint pass.

### Final release verification and authorized fresh reset

- Configured from scratch with the Visual Studio 2022 generator using CMake 4.4.2 (newer than the required 3.30) and MSVC 19.44.35227.0 with Windows SDK 10.0.26100.
- Completed a clean Release build, installed it into a fresh staging directory, and verified the staged base footprint at 5,384,192 bytes (5.13 MiB), safely below the 100 MiB limit. Models and optional capability packs remain outside that base measurement.
- Release CTest passed 5/5: module/core behavior, unified workbench routing and persistence, native inference contract, base footprint, and curriculum guard. Debug build and Debug CTest also passed 5/5.
- The optional learning-pack smoke suites passed with the previously installed private runtime: checkpoint/resume, English filtering, repair lineage, atomic promotion, active and isolated shadow servers, JSON/SSE, and clean shutdown.
- The first manual footprint invocation used the obsolete `cmake/check_footprint.cmake` path and failed before doing work. The corrected `tools/check_base_footprint.cmake` invocation passed at 5.13 MiB; no check was weakened or skipped.

Only after those gates passed, the explicitly authorized reset was performed for exactly `C:\Users\iammi\AppData\Local\ThreadSpire`:

- The reset tool resolved and validated that exact path, refused links escaping the root, checked ThreadSpire-owned processes, and wrote `C:\Users\iammi\AppData\Local\EngCoder-reset-20260826-043239-manifest.json` beside the directory before deletion.
- It deleted 5,068,451,150 bytes across 18,058 files, including the legacy private Python/PyTorch runtime, models, training data, settings, queues, and failed generations. The manifest records `DeletionCompleted: true`, no followed linked entries, and no deletion error.
- The original detached Downloads checkout remains untouched at `ad94ad4`. The obsolete `threadspire-native` worktree is absent; the persistent implementation worktree is the only project worktree besides the original Downloads checkout.

### Clean first-run validation after reset

- Launched the verified release from the clean staging install and closed it normally. It recreated only 3,311,642 bytes (3.16 MiB) in AppData: five curated/accepted-data files plus small configuration, chat, state, and control records.
- No internal PyTorch training pack, compact-native model pack, promoted model, cache, candidate, output, or project process was silently recreated. This preserves the explicit on-demand capability-pack boundary; connected local AI remains usable while built-in learning software is absent.
- Repeated autonomous visual QA at the minimum supported 980x680 logical size (1470x1020 at 150% DPI) and at 2100x1250. The dark Chat and Files & Activity pages remained readable, all five tabs stayed visible, controls did not overlap, and the clean state truthfully reported 0 B of models/packs, 3.16 MiB total AppData, and about 31 MiB of idle EngCoder RAM.
- Confirmed separately that the launched process was the verified Release executable and its real title was `EngCoder — local evolving intelligence`; the screenshot capture compositor briefly exposed an unrelated host title in its border.
- Re-ran Release CTest after the reset and visual pass: 5/5 passed. No EngCoder, ThreadSpire, Python, or native-host process remained afterward.

### Automatic native foundation and Capabilities redesign

- Corrected the clean-install lifecycle regression discovered after the reset. EngCoder now transactionally installs and starts the bundled compact native inference pack as soon as its native source is available, even when no promoted neural generation and no Python/PyTorch learning pack exist.
- Added a zero-weight native foundation mode. It serves the 4,247 bundled verified English/C++ examples, exact user-approved replies, and safe deterministic responses immediately. Unknown requests fail cleanly; they are never filled with an unverified draft.
- Approved exact replies are read from persistent verified memory on every request, so they work without restarting the host or waiting for weight training. A later rejection suppresses an earlier accepted exact reply; a supplied correction replaces it.
- Any missing, malformed, or incompatible promoted model pointer, manifest, tensor file, or tensor shape now falls back to the verified native foundation. The serving process no longer fails merely because an optional neural generation is absent or damaged.
- Replaced the legacy manual Capabilities console with live status cards for the built-in engine and automatic improvement, a separate optional-add-ons card, and a plain-language capability report. Manual skill names, queue buttons, rollback controls, and raw source paths remain hidden recovery hooks rather than primary controls.
- Removed decorative header strips and accent rails from cards. Kept rounded dark surfaces, clear borders, responsive two-column layout, and wrapped status text. Autonomous DPI-aware screenshots verified Chat and Capabilities at 980x680 logical and 1400x833 logical equivalents without white surfaces, overlap, or resize distortion.
- Exercised the real post-reset application: the compact native pack installed automatically, `/health` reported `engcoder-native-v1` with generation `foundation`, `hello` returned `Hello!`, no promoted weights existed, and the 4.2 GiB PyTorch pack was not installed.
- Extended regression coverage for clean zero-weight foundation startup, bundled C++/English retrieval, immediate approved-memory retrieval, rejection invalidation, clean unknown-request failure, automatic lifecycle ordering, no-trim card rendering, and the redesigned Capabilities surface.
- Versioned the compact native inference pack as v2. Startup now detects a stale installed helper and replaces it through the transactional pack manager, preserving rollback safety; a source-only deployment can still use an already installed helper when no bundled replacement is available.
- Removed the last stale Epoch identity from EngCoder's autonomous repository profile and added a regression guard so coding prompts consistently identify as EngCoder.

Verification for this checkpoint: Visual Studio 2022 Release build passed with `/W4 /WX`; Release CTest passed 5/5; the real first-start loopback smoke passed; minimum/wide Chat and Capabilities visual QA passed; and `git diff --check` passed.

Final post-v2 gate: a clean Visual Studio 2022 Release rebuild and Release CTest passed 5/5; Debug build and Debug CTest passed 5/5. The fresh staged EngCoder.exe is 5,123,072 bytes (4.88 MiB); the optional compact native host is 286,208 bytes, for a 5,409,280-byte staged total. The base executable passed the 100 MiB footprint check.

### Unified composer capability routing

- Audited the current surface against the unified-workbench brief and recorded the remaining work in `docs/UNIFIED_WORKBENCH_QUEUE.md`.
- Made the composer action automatic by default. Natural requests now classify and dispatch text/writing, supporting-language code help, repository build/test work, image generation, audio/music generation, and tools without requiring a top-level mode change.
- Moved the plain-language `Action` override beside Send. Manual selection remains authoritative; choosing a specific action does not silently fall through to a different class of work.
- Kept image, audio, text, coding, project, and tool requests inside the same conversation and existing unified provider contract. Repository requests ask for a folder only when one is actually needed.
- Expanded task analysis and routing beyond C++ to common supporting languages, while retaining the stricter C++23 capability path where appropriate. Image and audio/music now have first-class task kinds and capability requirements.
- Added regression coverage for natural image/audio prompts, repository build/test intent, supporting-language debugging, concise composer controls, and automatic action dispatch.
- Reworked the responsive composer geometry so the Action selector remains visible at minimum size without crowding the AI controls. Corrected combo item sizing and shortened the empty reply selector after autonomous minimum/wide visual QA.

Verification for this checkpoint: clean Visual Studio 2022 Release build passed; Release CTest passed 5/5; Debug build and Debug CTest passed 5/5; the base-footprint test passed; minimum and wide dark-interface screenshots were captured and inspected; and `git diff --check` passed.
### Three-destination workbench navigation

- Reduced the top-level application shell to `Workspace`, `Settings`, and `Activity`. Existing AI connection, learning, capability, storage, and log functions remain available; none became a separate application or was removed.
- Added a secondary Settings strip for `AI & Tools`, `Memory & Learning`, and `Capabilities`. Programmatic repair/setup navigation selects the correct primary and secondary destinations instead of opening a hidden legacy page.
- Kept the unified conversation/composer permanently under Workspace and consolidated live storage, RAM, cleanup, and logs under Activity.
- Eliminated the Win32 tab control's bright overflow tail with DPI-aware dark owner-drawn fill surfaces, prevented minimum-width scroll arrows, and forced selection redraws so labels do not disappear during resize or page changes.
- Shortened and resized the densest learning controls (`Improve`, `Custom topic`, `Examples to review`, `Find examples`, `Improve now`, `Stop learning`, and `Example template`) so the page no longer overlaps or truncates at the tested sizes.
- Updated the README and strict UI regression contract for the new information architecture. The next queue item is plain-language conversational teach/remember/forget with exact storage previews.

Verification for this checkpoint: a clean Visual Studio 2022 Release build passed; Release CTest passed 5/5; a clean Debug build and Debug CTest passed 5/5; native inference contract, curriculum guard, and base-footprint smoke tests all passed; EngCoder.exe measured 5,129,216 bytes (4.89 MiB), within the 100 MiB budget; automated minimum/wide screenshots of Workspace, all three Settings pages, and Activity were captured and inspected; and `git diff --check` passed.

### Conversational memory, teaching, and correction checkpoint

Since the user requested a complete audit of learning behavior and exact visibility:

- Removed silent prompt-to-memory capture. EngCoder now changes permanent memory or learned behavior only through explicit conversational commands or the existing per-reply backup controls.
- Added local `remember`, `forget`, memory-list, `teach`, and last/selected-reply correction commands inside the unified composer. Every command prints the exact affected text, whether it is immediately available, whether it may enter a future shadow candidate, whether cleanup changed it, and confirms that no AI model was contacted.
- Kept permanent user memory entirely separate from model weights. Secret-like content, empty commands, ambiguous deletion, and bulk deletion from Chat are blocked without changing storage.
- Made a hard correction immediately authoritative across exact and semantic recall, including after restart. The rejected answer remains only as negative audit evidence; accepted and diverse-human counts are not inflated.
- Updated the optional training loaders so only the newest correction for a prompt is batched and every obsolete positive answer is excluded. Eligible corrections now enter the serialized shadow-learning queue through the same transactional service as other verified evidence.
- Kept error cards, calculations, and local control receipts out of reply learning. Duplicate lessons and corrections add no copy or extra weight.
- Made the Training Library mirror the actual trainer: hard corrections appear as approved replacements, the rejected draft is shown in full but explicitly excluded, and any older approved answer is labeled superseded audit history rather than trainable data.

Verification for this checkpoint: Visual Studio 2022 Release build passed; Release CTest passed 5/5; `git diff --check` passed; Python bytecode compilation passed for both optional training loaders and their smoke test. The full PyTorch smoke was attempted through the base Python and correctly stopped at `ModuleNotFoundError: torch`; the authorized reset removed the 4.2 GiB private trainer and no internal PyTorch training pack is currently installed. No dependency was installed implicitly.


Post-verification visual pass: launched the real Release executable at 150% DPI, resized between the 980x680 logical minimum and 1400x833, and inspected DPI-aware captures. Workspace, Settings, Activity, transcript, reply controls, composer, and sidebar remained dark, readable, and non-overlapping with no stretched resize artifacts. An initial capture incorrectly clipped Activity because the PowerShell QA process was DPI-unaware; the application layout was not changed for that harness error. The first interactive capture created a QA-only chat containing two memory-list receipts; after process, path, and link validation, that exact generated chat was removed and the previously active user chat was restored. No existing user conversation or memory item was deleted.
### Bounded automatic-improvement session and responsive Learning page

- Added a persistent improvement-session state machine with running, paused, ended, and time-limit-reached phases. Fresh state starts one bounded 12-hour session; 1, 4, 8, and 12-hour budgets are available, and larger requests clamp to 12 hours.
- Pause persists the exact remaining duration, resume continues from that remainder, End survives restart, and the deadline cannot silently restart. The atomic state file is `state/improvement-session.ini`.
- Gated both queued and active learning through the session state. Foreground Chat keeps priority, active work exits through the existing safe checkpoint path, admitted evidence is retained, and the last promoted model keeps serving while a shadow candidate trains.
- Redesigned the default Memory & Learning page around the live session, permanent user memory, current activity, and the full searchable Training Library. Manual target, file, and source controls remain available behind `Show backup example controls` instead of crowding the primary page.
- Added a live plain-language capability boundary: verified memory, optional internal PyTorch weight training, optional Transformers/PEFT fine-tuning, and separate image/audio engines are reported independently. Selecting or installing a pack does not start training, change the selected AI, or interrupt Chat.
- Added regressions for fresh-session startup, atomic persistence, exact paused remainder, restart behavior, resume, the 12-hour clamp, durable End, worker gating, and the visible safety contract.
- Launched the real Release executable at the 980x680 logical minimum under 150% DPI and at 2100x1250. The dark page preserved its structure without white panes, overlap, or distorted controls; long activity and Training Library records wrap instead of forcing horizontal scrolling. The QA harness closed its own processes, and no EngCoder or native-host process remained.

Verification for this checkpoint: Visual Studio 2022 Release build passed with `/W4 /WX`; Release CTest passed 5/5; minimum/wide visual QA passed; and `git diff --check` passed. The optional 4.2 GiB PyTorch trainer remains uninstalled after the authorized reset, so no dependency or weight-training run was triggered implicitly.

### Exact optional add-on audit

- Extended capability-pack descriptors and transactional manifests with purpose, origin, license, loading condition, dependencies, and safe-removal rules.
- Added an explicit cancellation-aware installed-tree auditor. It never follows directory links; enumerates every file/directory and byte; separates model weights, native runtimes, Python program/bytecode, dependency metadata/licenses, and other data; reads Python `METADATA`/`RECORD` ownership; and labels redundancy only after exact same-path byte comparison against required installed packs.
- Added **Show exact contents** to Capabilities. Catalog projections remain visibly separate from actual installed contents; an uninstalled add-on truthfully reports 0 installed files and 0 model weights. Audit and installation remain background operations isolated from Chat, AI selection, and training.
- The `Apache-2.0` license in the regression output belongs only to the synthetic `peft 0.14.0` fixture used to prove installed metadata parsing. No Transformers/PEFT or PyTorch pack is currently installed, and the application does not claim otherwise.
- Added a transactional synthetic-pack regression covering exact file counts, weight-byte accounting, dependency version/origin/license attribution, RECORD ownership, byte-confirmed duplication, and the zero-content uninstalled case.
- Repeated autonomous DPI-aware visual QA at 150% through minimum → wide → minimum resizing. Both tab rows remained dark and labeled, rounded cards retained shape, add-on controls remained visible, and no stale/stretch pixels or overlap appeared.
Verification for this checkpoint: clean Visual Studio 2022 Release and Debug builds passed; Release and Debug CTest each passed 5/5; the 100 MiB base-footprint test passed in both configurations; minimum → wide → minimum visual QA passed; and no optional dependency or model pack was installed.

### Correction: independent idle learning and explicit weight training

The earlier timed improvement-session checkpoint above was superseded after the product requirement was clarified. Twelve hours is a hardware compute ceiling for a complete small-model training cycle, not a countdown, a session lifetime, or a gate on learning.

- Removed the improvement-session state file, countdown, duration selector, Pause, End, and every worker/teacher dependency on that state.
- Restored automatic idle learning as an independent continuous curator of verified practice examples. Disabling it stops only new automatic examples; it does not discard queued evidence or pause explicit weight work.
- Added a separate `Compile data + train model` action. It coalesces every topic with pending verified, weight-eligible evidence into the existing persistent serialized shadow-training queue. Chat keeps priority and the last promoted model keeps serving.
- Kept controlled software self-improvement explicitly separate from both data curation and weight training. Learning controls never edit application source or promote repository changes.
- Fixed a Debug resize crash caused by a dynamic `std::clamp` call whose computed maximum could fall below its minimum after Settings reduced the available client height. Child rectangles are now clamped to valid parent bounds, zero-sized layouts are skipped, and every dynamic storage split normalizes its bounds before clamping.

Verification for this correction: the Debug app target relinked successfully; Debug CTest passed 5/5; and the DPI-aware real executable survived Settings > Memory & Learning at 1470×1020 and 2100×1250 with the explicit training button visible, no timer controls, no white surface, and no invalid-bounds termination.
### Unified specialists and isolated repository self-iteration

- Kept conversation, coding, repository work, images, audio, and tools on the existing unified task/provider contract. Coding Agent and repository work remain execution profiles rather than duplicate provider interfaces.
- Removed direct patch application from the selected repository. EngCoder now pins its exact Git HEAD, creates a private no-hardlink clone under the run audit folder, and performs model patching, compiler runs, tests, repairs, and candidate commits only inside that clone.
- Removed hard-reset rollback. Failure or cancellation records that the selected repository was unchanged and retains the private candidate for audit/protected cleanup.
- Sealed every build/test-passing change as an immutable candidate commit plus atomic `candidate.json` manifest. Empty diffs cannot be presented as successful repository work.
- Added a per-result **Promote verified change** review path. Promotion revalidates the candidate commit, original source HEAD, and clean source status, then publishes only a unique `engcoder/candidate-…` branch reference. It does not check out, merge, or modify working files. Dirty or stale repositories block promotion without partial changes.
- Added repository-aware Retry, Reject, Copy, and review details. Retry reruns the original repository task through isolation; Reject creates no branch and no positive weight evidence; saved chats retain exact source, staging, commit, commands, checks, and promotion provenance.
- Removed automatic storage of compiler/test-passing patches before review. Repository evidence may reach verified learning only after explicit promotion approval.
- Added a real temporary-Git regression proving successful branch-only promotion, unchanged source HEAD/files, dirty-source rejection, and stale-source rejection. Added source contracts forbidding hard-reset rollback and pre-approval trajectory learning.

Final verification for this checkpoint: a fresh Visual Studio 2022 configure used bundled CMake 3.31.6, satisfying the CMake 3.30+ requirement. Debug and Release compiled with `/W4 /WX`; CTest passed 5/5 in both configurations, including the real Git isolation test, unified multimodal routing, persistent training recovery, atomic model promotion, native inference, curriculum guard, and base footprint. DPI-aware `SetWindowPos` + `PrintWindow` captures of Workspace and Memory & Learning at 1470×1020 and 2100×1250 stayed dark, aligned, and free of resize distortion or invalid-bounds termination. The final staged package contains two files totaling 5,579,776 bytes (5.32 MiB); the base excluding the optional compact-native helper is 5,293,568 bytes (5.05 MiB), below the 100 MiB limit. The current post-reset AppData is 3,616,528 bytes (3.45 MiB), and no EngCoder-owned process was left running.
