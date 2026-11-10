## Candidate UI follow-up (2026-11-10)

- Web regression tests cover candidate replies with persisted flags or legacy activity provenance, ordinary retries while the candidate switch is armed, approval event ordering, and removal of the approval card at terminal completion. This follow-up changes the shared frontend and documentation only.

## 0.7.4 Windows release (2026-11-10)

- Windows x64 desktop, server, and CLI built in Debug and Release. CTest passed 65/65 in each configuration, including stale approval, create-only writes, repair escalation, and learning recall. All 16 Web smoke tests and 10 application JavaScript syntax checks passed.
- The served desktop interface ran a staged, fresh unsigned reservation repair against independent MSVC and CTest checks. The Qwen 3.5 9B model wrote the initial edit. CTest identified a remaining boundary failure and the native runtime handed the unchanged candidate and diagnostics to Qwen 3.8 27B. The stronger model's first revision still failed. Its next approved revision passed all three cases.
- The visible Edge preview showed the model handoff in Run info and one compiler-and-tests memory in EngCoder Lab. The tests and CMake inputs stayed byte-identical. The two local models used one provider slot sequentially. The run stored a scoped capacity rule as retrieval memory, with no weight update or source ingestion.

## C++ boundary lab (2026-11-10)

- Windows Debug and Release CLI and test builds passed, with 64/64 CTest cases in both configurations. The CLI ran the live reference lookup, C++23 compilation, repair verification, deduplicated training, and serialized inference.
- Four verified option sets remained after deduplicating 64 compiler-checked training cases. The 56-weight candidate occupied 228 bytes, below its 256-byte cap. It selected 16/16 correct repairs on the fresh split versus 4/16 for the zero-weight tie-break baseline. Exhaustive verification also passed 16/16. The fresh split changes syntax, values, context nouns and option positions within the four known families.
- On the identical 16 prompts with an A–D decoder grammar, local Qwen 3.5 9B scored 12/16 and Qwen 3.8 27B scored 16/16. Both returned all 16 choices. With plain prompts, the 27B also scored 16/16 and the 9B scored 10/16 with six responses stopped at the output limit.
- The CLI selected the strict upper bound for a new zero-based index example and rejected a wrong-size artifact. A verified local clamp choice made no teacher call. The same clamp with zero weights failed two probes, skipped a missing advertised model, then accepted a live 27B choice after independent verification.
- Scope: four known repair families, authored choices, and three fixed public C++ draft references. No unrestricted code generation, conversational English, or active Foundation or addon weight promotion was exercised.

## 0.7.3 Candidate self-iteration (2026-11-10)

- Windows VS2022 x64 Debug and Release builds passed. CTest passed 62/62 in each configuration. The new native test covers approved repair, denied edits, an out-of-candidate cache, and exhausting the two-repair limit.
- All 16 Web smoke tests and 10 JavaScript syntax checks passed. All 25 mission entries synchronized.
- A loopback smoke test drove the Release server with a scripted streaming provider. An approved exact edit caused a real MSVC compilation failure. The next model request received the diagnostic, a second approved edit fixed it, and the real rebuild and CTest passed. The served UI exposed the candidate control.
- Live KeyMaker inference, interactive desktop acceptance and Linux builds remain unverified for this release.
- The learning-boundary candidate applied eight exact approved patches through EngCoder self-iteration and passed 63 Release CTest cases and 16 Web smoke tests. A fresh loopback server using the candidate binary rejected completed candidate feedback, missing provenance and a mismatched ordinary reply. It accepted a matching ordinary reply as the sole verified evidence record. The patch provider was scripted.

## 0.7.1 Windows release verification (2026-10-09)

- **Passed:** Visual Studio 2022 x64 Release build and all 61 CTest cases.
- **Passed:** Visual Studio 2022 x64 Debug build and all 61 CTest cases.
- **Passed:** all 16 Web smoke tests and syntax checks for all 10 Web JavaScript files.
- **Passed:** isolated Release `EngCoderServer.exe` startup on loopback port 14329, `/health` reporting EngCoder 0.7.1, `/ui/` returning HTTP 200, and `/api/engcoder/qwen-output` returning the retained-output snapshot contract.
- **Fixed during release audit:** the new-profile Web GPU input still declared `value="95"` while every other 0.7.1 default and its label used 50%. It now starts at 50%, and the release-default smoke test protects both the label and input value. Saved profiles remain authoritative and are not rewritten.
- **Not claimed:** live GGUF generation, physical GPU-layer allocation, live LM Studio 9B/27B routing, remote-LAN operation, or interactive Edge responsiveness during a 79 GB cold model load. Those require available models/hardware and hands-on acceptance; the source retains those limitations below.

## 0.7.1 Async UI 1 checks (2026-10-09)

- `release_071_async_sidebar_live_smoke.cjs`: tests immediate drawer visibility while a model load is pending, non-modal Lab access, deferred/coalesced panel painting, and bounded event-loop yielding with terminal persistence.
- All Web smoke tests and JavaScript syntax checks must pass. Existing idle retention, MathML, retry and role-routing tests must remain green.
- **Windows acceptance pending:** start native model loading (cold, large GGUF), immediately toggle Output / Qwen Live / Run info, send/stream text with available loaded model or `/v1` client, confirm partial output appears and the sidebar remains clickable; repeat in served Edge client. Verify Stop and chat refresh while generating.

## 0.7.1 Qwen Live 3 idle-retention hotfix (2026-10-09)

- The right-panel Qwen Live view retains its last generated output after completion and through empty/failed request starts. A real provider fragment from a new request replaces the retained output. The status shows `Qwen preparing · last output retained` until replacement begins, then `Idle · last output retained` on completion.
- `projects/web/tests/qwen_live_idle_persistence_smoke.cjs` checks start/finish/next-delta and snapshot-recovery transitions; existing Web tests must remain green.
- Native `test_qwen_output_journal.cpp` now covers the same transitions. C++23 isolated journal checks can be run under GCC/Clang; the full Catch2/MSVC suite and a live MCP-hosted inference test remain outstanding.

## EngCoder 0.7.1 source-package verification (2026-10-09)

- **Passed in the supplied source package:** all 16 Web smoke tests (`node projects/web/tests/*smoke.cjs`, including the new release/default/scale contract check). The 25-mission Web regression passes.
- **Reviewed:** C++ model default (50%), native GUI default/reset (50%), Web form/default/reset (50%), 0–100% scale and 60%-layer ceiling unchanged. The 48-layer hint maps the new 50% default to `-ngl 14`; prior custom 99% still maps to 29.
- **Native regression added:** `0.7.1 keeps saved Qwen GPU tuning when the default changes` verifies a revision-9 saved 99% model survives two manager startups and its persisted manifest does not change. Also updated old migration tests for the new default.
- **Fixed regression drift:** An older Web smoke test hardcoded `0.6.9`; it now checks semantic version format, leaving version specificity to the 0.7.1 test.
- **Fixed:** GPU layer display fallback to 50% for malformed/NaN profile data; legitimate values and mapping remain unchanged.
- **Fixed:** Release version metadata previously disagreed (`0.6.8` vs `0.6.9`). Native CMake, vcpkg, browser and training worker now declare `0.7.1`.
- **Superseded by the Windows release verification above:** the package author did not have the full native toolchain; this repository pass subsequently completed both MSVC configurations and CTest. Actual Qwen GPU-layer allocation, remote clients, and live inference remain unverified.

## Qwen live provider output hotfix (2026-10-08)

- Web regression: `node projects/web/tests/qwen_live_provider_smoke.cjs` exercises event capture, separate Qwen live tab, source-route wiring and exclusion of unrelated output. Existing Web regression suite must remain passing.
- Native regression: `test_qwen_output_journal.cpp` checks streamed content, cursor semantics, bounded event retention and chunk splitting. Standalone C++23 concurrency test is run in the artifact environment.
- Missing here: linked Windows/MSVC CTest, actual Qwen Next Flash inference through `/v1`, and remote authenticated served-browser end-to-end checks. No claim of raw tokenizer IDs or observation of requests bypassing EngCoder.

## 0.6.9 Catch2 routing/handoff hotfix (2026-10-08)

- User's Windows CTest report: 52/54 passed. Failures were `agent runtime persists streamed text and terminal completion` and `handoffs reject unsafe fields and extra-long content`.
- Root causes found in source: the earlier fast-path gate could omit the `session-core-only` audit event, and the handoff validator used case-sensitive matching for the capitalized `Ignore previous` regression case.
- The code now emits a no-model `session-core-only` decision even when the normal controller path is deliberately skipped, and rejects unsafe handoff content without regard to ASCII letter case.
- Added coverage for mixed-case injections, per-item and total-length limits, and acceptable handoff notes.
- Offline verification: all 12 Web regression scripts, JS syntax and mission source synchronization. Native validator predicate compiles in strict C++23 mode in an isolated harness.
- **Not verified here:** full MSVC/CTest suite. Re-run `auto.bat` on Windows and confirm 54/54 before considering this hotfix certified.

## 0.6.9 advisory handoff gate hotfix (2026-10-08)

- **PASS:** GCC 14.2 and Clang C++23 standalone classifier checks: eight scenarios each (basic C++23 display, complex Vulkan refactor, architecture review, arithmetic/math bypass, greetings and simple definitions).
- **PASS:** all 12 Web JS smoke tests (including Run timing, raw output, right-panel Retry, math assets, served mode, and 25-mission sync).
- **ADDED, NOT EXECUTED HERE:** Catch2 `test_handoff_contract.cpp` validates JSON contract, reasoning-only/empty content, code leakage, tool calls, truncation and malformed input; `test_agent_runtime.cpp` verifies direct basic-code routing, one-time preflight, three-tier handoff and original-prompt forwarding.
- **BLOCKED:** native CMake configuration requires nlohmann_json development package, not installed in this container. Therefore no native Windows build, MSVC Catch2 suite, or live 9B/27B/Qwen validation is claimed. Run `auto.bat` and tests on target Windows system.
- **UNCHANGED:** Qwen GPU offload/profile, model host lifecycle, session storage, theme, raw-output-only pane, Run timing UI and canonical 25-mission backlog.

## 0.6.9 math HTTP-route hotfix

**Root cause:** the math renderer existed and the HTML referenced it, but the C++ native HTTP server omitted `math.js` from its explicit `/ui/js/*` allowlist. The request returned 404, and optional invocation silently left raw LaTeX in chat. Browser caching could also keep an old script after updating.

- `node projects/web/tests/served_static_assets_smoke.cjs`: all HTML script references must have native server routes; explicitly checks `math.js` and serving `Cache-Control: no-store`.
- `node projects/web/tests/release_070_math_smoke.cjs`: inline/display parser, fractions, boxed answer, literal fenced code, and DOM safety.
- Run the existing Web smoke suite. Offline checks do not certify live Windows/Edge behavior or the complete native MSVC build.
- **Windows acceptance:** start updated `auto.bat`, inspect `http://127.0.0.1:14321/ui/js/math.js` (HTTP 200), reload chat, then verify `\(1\)`, `\[5X+3=8\equiv1\pmod{7}\]`, and `\[\boxed{1}\]` render as math in the conversation. Copy and raw Output must still show original text.

## 0.6.9 subagent maintenance — source-only verification

- Updated native three-tier routing with fallback for **enabled but unassigned** specialist addons; added C++ integration case verifying small checklist, middle review, then large final answer.
- Existing JS regression checks and source/mission sync must pass before packaging; Windows/MSVC full build and real LM Studio 9B/27B + native Qwen run are **not** certified by Linux tests.
- Configured server URL is environment-driven and not the Web Session Settings endpoint.

## 0.6.9 screenshot routing hotfix — validation targets

- C++23 classifier regression: modular arithmetic, percentage arithmetic, mph/speed, exact fractions, and simple text.
- Web Run info: present Architect fallback reason, while Output remains generated text only.
- Full Windows 49+ CTest suite, LM Studio controller compatibility and live model availability **require Windows verification**.

## 0.6.9 hotfix — Architect numerical routing

- Reproduced the target CTest mismatch using the exact existing intent expectations: `What is 12% of 180?` was incorrectly classified as `simple` rather than `complex`.
- Updated the classifier and expanded the first native regression case for `%`, `-`, written math, round-trip speed, and nonmathematical simple definitions.
- **Offline passed:** the two unmodified native test case bodies in `test_task_intent.cpp`, using a minimal Catch2-compatible standalone macro runner under GCC and Clang with C++23 and `-Wall -Wextra -Wpedantic -Werror` (the real Catch2 package is unavailable here).
- **Offline passed:** eight Web UI smoke scripts and `scripts/sync_missions.py --check` with 24 missions preserved.
- **Pending Windows acceptance:** full project MSVC build and all 49 CTest cases using `auto.bat`. Never infer those outcomes from the offline runners.

## 0.6.9 model-role and served-mode regression

- `node projects/web/tests/release_069_model_roles_smoke.cjs` checks actual provider-reported final model vs. small/middle consultations, source protection from native subagents, and retry of both external foundations and small responders after settings change.
- All Web UI smoke tests must pass; right-panel Output and Run info remain separate; themes and saved session data remain served by the same native runtime.
- Windows acceptance: run `auto.bat`, all CTest cases, 3 loaded models with distinct assignments, native/served UI theme parity, regenerate an LM Studio response, and check per-response Run info after refresh. Resource preflight and OS process confinement are separate outstanding missions.

## 0.6.8 maintenance — platform warning and responsive Chats

- Regression tests: `node projects/web/tests/responsive_chats_smoke.cjs` covers manual open at 1100px and narrow widths, unrelated layout calls, outside-click persistence, shrink-to-close, and aria state.
- Windows/MSVC acceptance: `auto.bat` must build `engcoder_runtime` without C4996; Linux source branch uses `std::getenv`. Full CTest and Edge window-resize interaction require Windows verification.

# Validation — EngCoder 0.6.8

This release keeps the existing EngCoder/BotFace interfaces and agent/training architecture while repairing the native Qwen3.8 Flash execution boundary.

## Runtime findings encoded in this release

- The known-success baseline is the pinned llama-server loading `unsloth/Qwen3.8-Flash-Next-GGUF:UD-Q2_K_XL` through `-hf`; Architect/System 1 is not involved at the point where the failing process crashes.
- Legacy expert-cache binaries are therefore treated as compatibility baselines, not as proof that the Q2 cache path is valid.
- A Qwen-specific compatibility implementation documents Q2 layouts whose routed `ffn_down_exps` physical width is padded to the quantization block boundary and whose cache path requires matching activation padding. The exact published case is not proof that Unsloth UD-Q2_K_XL has the same file layout, so EngCoder treats this patch as a known prerequisite/candidate compatibility fix rather than the sole proven crash cause.
- `expert_cache_mode=auto` requires the patched runtime before a cache probe is even attempted, then persists the outcome per model/runtime recipe. Old runtimes automatically use the no-cache baseline; patched runtimes become trusted only after reaching `/health` with the cache enabled.
- Runtime recovery isolates optional acceleration before sacrificing context: cache-off at the same context, then a same-context minimal llama.cpp launch, then bounded context fallbacks.

## Offline release checks

- C++23 syntax compilation with `-Wall -Wextra -Wpedantic -Werror` for modified config/runtime-model/Qwen/native-model/process sources using the repository's local dependency stubs where external headers are unavailable.
- SDL/ImGui fallback syntax compilation with local UI stubs.
- `node --check` for every Web JavaScript file and a DOM-reference audit for shared Web/Desktop controls.
- `bash -n` for shell scripts, Python bytecode compilation, and JSON parsing.
- Runtime patch audit: both Qwen3.8 Q2 compatibility hunks are present; Windows/Linux installers run `git apply --check` before applying the patch to the pinned commit and record the runtime feature manifest.
- Package hygiene: no model weights, generated binaries, build trees, downloaded dependencies, vcpkg installed tree, or bytecode caches are included.

## Target-machine acceptance

The artifact environment cannot execute the Windows RTX 5080/CUDA runtime or perform the complete networked vcpkg/backend build. `auto.bat` remains the full EngCoder build/CTest acceptance step. For Q2 expert-cache acceleration, use **EngCoder Lab -> Model Library -> Upgrade/Install Qwen runtime** once; the model weights already in the Hugging Face cache are reused. Until that patched runtime is installed, Recommended/Automatic intentionally run the stable no-cache Qwen baseline rather than enabling the incompatible cache path.

## 0.6.6 routing/cleanup checks

- Native C++ routing/model/scheduler/server sources: strict C++23 syntax with `-Wall -Wextra -Wpedantic -Werror` using offline dependency stubs.
- New regression test sources: syntax checked with Catch2 stubs.
- Web JavaScript: `node --check` for all application scripts.
- Runtime Default semantics: loaded native model resolves to `qwenflash://...`; provider-assigned model becomes AgentRuntime's inference model.
- Managed-model availability: missing `engcoder://` controller/foundation/addon components no longer become phantom external models.
- Scheduler shutdown: active stop sources are cancelled before jthread teardown.
- Web polling: adaptive idle backoff caps at 400 ms; active-event polling returns to 100 ms.
- Model probing: one architecture snapshot supplies the registry; no duplicate `/api/engcoder/models` scan is issued by the shared BotFace frontend.
- Browser-state migration: stale unavailable managed-model selections are removed instead of recreated as custom options.
- Persistence/polling: ordinary settings changes do not serialize chat history, hidden Lab pages do not poll, and visible Lab polling targets only the active telemetry tab.
- Cross-platform entrypoints: `auto`, `build`, `check`, `run`, and `clean` wrappers are present for both Windows (`.bat`) and Unix (`.sh`); shell syntax and Windows CRLF wrapper formatting are validated.

## 0.6.5 approval / desktop lifecycle checks

- Approval continuation: HTTP tasks with `wait_for_approval` keep the exact reviewed tool call pending; approval is bound to task + call ID, and denial returns a tool-denied observation instead of terminating the run.
- Tool preference: repository/base-directory inspection is directed to `workspace.tree/read/search`; `where`/`which` executable discovery is classified read-only.
- Desktop lifecycle: the shared Windows Desktop/Web page registers an ephemeral per-launch token, reports page close, and the native owner stops the embedded server after a reload-safe grace period.
- Edge ownership: the app-mode Edge process tree is placed in a kill-on-close Job Object where supported, with a direct process fallback, so closing EngCoder does not leave its dedicated browser/runtime process behind.
- HTTP endpoint default: EngCoder now uses loopback port `14321`; the old 8787 probing range is removed from product defaults/build launchers.
- Static validation in the artifact environment: `node --check` passes for every Web JavaScript file, Unix wrappers pass `bash -n`, requested source invariants were audited, and no remaining Catch2 `CHECK`/`REQUIRE` logical-chain assertions were found. Windows/MSVC remains the authoritative final build/CTest target.

## Runtime backend UI/provisioning validation
- `node --check projects/web/js/state.js`
- `node --check projects/web/js/ui.js`
- `bash -n runtime/qwen-flash/install-runtime.sh`
- `git apply --numstat runtime/qwen-flash/patches/0001-qwen38-q2-padded-down.patch` parses successfully.
- Windows runtime progress is emitted to `<state-root>/runtime-install-qwen-flash.json` and surfaced through `/api/engcoder/model-library`.

### Composer plus-menu / quick-bar validation
- `node --check projects/web/js/*.js` passes after the composer integration.
- `projects/web/index.html` parses successfully with the standard HTML parser.
- The original send/stop/microphone controls and composer grid remain in place; additions are menu/quick-bar surfaces around the existing row.
- Folder import filters unsupported/binary files and limits context to 96 files, 512 KiB per file, 1.5 MiB total so chat persistence stays below typical localStorage quotas.
- The quick routing presets only manipulate existing `forcedAddon`, `autoRouting`, and `enableAddons` controls; they do not grant filesystem authority or bypass the native workspace sandbox.

## Settings/runtime/learning safety checks

- Web JavaScript parses with `node --check` after the runtime settings, lifecycle-menu, per-field defaults, learning remove/ban, and denylist UI changes.
- Model profile defaults are 36% GPU layer offload in config, runtime-model decoding, Web UI, native GUI, and seeded built-in profile. Untouched legacy 70%/90% recommended profiles migrate to 36% while Manual/custom profiles are preserved.
- `Reload / Apply settings` saves the visible runtime profile before unload/load; ordinary setting edits never reload a live model automatically.
- Model cards distinguish saved GPU target from the loaded effective profile and `-ngl` reported by the active Qwen runtime.
- `Clear temp cache` only removes transient non-GGUF files under EngCoder's configured model-cache tree. Downloaded model weights have a separate confirmed delete route, and Local GGUF/external paths are never deleted by EngCoder.
- Learning removal rewrites `verified-evidence.jsonl`; hard bans persist in `learning/denylist.json`, remove matching existing evidence, block prompt- or response-equivalent future ingestion through `LearningService::record`, and can be explicitly unbanned.
- The artifact environment cannot execute the authoritative Windows/MSVC build; rerun `auto.bat` on the target machine for final GUI/test acceptance.

## 0.6.6 source implementation and verification (2026-10-07)

- **Passed:** `node --check` across every `projects/web/js/*.js` script.
- **Passed:** an isolated Node VM regression for native raw text and reasoning delta capture, 96 KiB tail bound, event cursor sequence, required UI model selectors, and conversation-persistence hook presence.
- **Passed:** independent C++23 GCC `-Wall -Wextra -Wpedantic -Werror` compile of `src/controller/task_intent.cpp` and runnable smoke test of conservative simple/coding/complex classification plus training/persistent replay of 20 numeric controller examples.
- **Passed:** Python source compilation and Bash `-n` for installer/shell scripts.
- **Not run:** full CMake build and Catch2 suite. The artifact container does not provide the project's `nlohmann_json` CMake package or complete declared native dependencies; `cmake` stops at `find_package(nlohmann_json)`.
- **Not run:** Windows MSVC/Visual Studio, Qwen GGUF first load/reload, LM Studio companion model, live Web/Edge UI, model draft compatibility, real tokenizer identifiers, OS-isolated sandbox behavior. All require a target-machine run and logs.
- **Compatibility note:** older sections above are retained as historical release-validation records; 0.6.6 continues the revised 95%-default GPU slider where new 100% corresponds to old 60%, not the obsolete old 36% default.

Track remaining work, especially security and MTP compatibility certification, in `docs/FUTURE_MISSIONS.md`.

## 0.6.6 maintenance: warning, profile persistence and composer missions

- `repair_legacy_self_draft` result is now explicitly consumed; the previous `[[nodiscard]]` discard at `native_model_manager.cpp` no longer exists.
- The built-in model profile no longer triggers an unconditional disk write on every EngCoder startup.
- Native write uses a checked unique temporary file plus replace, and a corrupt built-in profile is preserved instead of being replaced blindly. Added Catch2 regression tests for no-op manifest timestamp and corrupt input preservation. Native tests are **pending** MSVC/CMake dependencies.
- `projects/web/js/mission_data.js` is generated from the one canonical markdown register. Run `python scripts/sync_missions.py --check` to detect drift. The bottom Missions control and filtered modal are included in both Web and shared Desktop UI via native static file routes.

### 0.6.6 maintenance artifact acceptance (C4834 + composer Missions)

- **Passed:** `python scripts/sync_missions.py --check` — 24 open P0–P2 objectives match the generated Web data.
- **Passed:** `node projects/web/tests/mission_smoke.cjs` — footer button, mission count, dialog open/close, priority filter, search and safe text nodes.
- **Passed:** `node --check` for every `projects/web/js/*.js` source.
- **Passed:** C++23 `-Wall -Wextra -Wpedantic -Werror` standalone POSIX `core::replace_file` test — overwrite succeeds; a failed replacement leaves the existing manifest intact.
- **Passed:** previous standalone C++23 shadow-router smoke (simple/complex/coding, numeric learning and persistence) and `bash -n` across shell files.
- **Source verified:** built-in profile repair result is consumed (`C4834` origin removed); unchanged profiles no longer rewrite manifest on startup; native save checks errors and replaces via platform-specific helper.
- **Not run here:** authoritative Windows MSVC solution build, dependency-linked full CTest, Edge layout/4K visual QA, actual model runtime and GPU reload. Verify on the target machine before deployment.

## 0.6.6 Windows CTest hotfix — MTP validation

- Corrected an unreachable validation branch: a self-referential legacy speculative repository was rejected with the generic missing-GGUF error before the specific same-repository rejection could execute.
- Retained the stricter runtime policy: no remote draft repositories, missing local draft GGUF, unsupported draft type, or non-GGUF file can pass validation while MTP is enabled.
- Expanded the existing Catch2 unsafe-scheduling test with positive and negative MTP cases so validation precedence is covered.
- Passed: 8 isolated strict C++23 (`-Wall -Wextra -Wpedantic -Werror`) runtime-MTP branch checks, compiled directly from the exact production validation block. These exercise validation ordering, but do not replace the full Catch2 suite.
- Passed: JavaScript (`node --check`), Bash (`bash -n`), and Python syntax checks for the packaged source.
- Target-machine acceptance: rebuild and rerun `auto.bat`; all 45 CTest entries in the user-reported maintenance build must pass before launch. The Linux artifact environment cannot certify the Windows/MSVC CTest suite.

## 0.6.7 source checks

- `node --check` for every application/test `.js/.cjs` file; `node projects/web/tests/mission_smoke.cjs` and `release_067_ui_smoke.cjs`.
- Standalone C++23 `task_intent.cpp` regression smoke for greetings/complex/coding and shadow journaling.
- New Catch2 native `automatic small responder handles greetings without addon routing or tool authority` case is registered; full dependency-linked MSVC/CTest must be run on Windows.
- `scripts/sync_missions.py --check` verifies all 24 unfinished missions are still exposed in the footer.
- No actual Qwen/LM Studio live inference, Windows/CUDA runtime or MTP GGUF compatibility certification available in this build environment.

### Executed in this source release environment

- Passed `node --check` on 11 Web scripts and tests.
- Passed `mission_smoke.cjs` and `release_067_ui_smoke.cjs`.
- Passed mission-source parity check (24 still-open missions).
- Passed Python AST (5 files), JSON parser (5 files), and `bash -n` checks.
- Passed a standalone g++ C++23 intent and shadow-journal harness with `-Wall -Wextra -Wpedantic -Werror`.
- Native linked Catch2/CTest suite and Windows live Qwen/LM Studio runtime are **not run** in this environment; these are explicit acceptance blockers, not reported successes.

## 0.6.7 generated-only panel acceptance

- `node projects/web/tests/generated_output_only_smoke.cjs` validates streaming fragment capture, exclusion of controller/profile events, legacy payload migration, cursor progression, and in-place rendering contract.
- Live browser/Qwen validation still required on Windows.

## 0.6.8 offline and hardware qualification

- Tested offline: Web regression smoke for generated-output-only panel, Missions viewer (24 outstanding), attachment visibility and independent response routing; syntax validation; isolated C++23 workspace checkpoint capture/restore and traversal-denial scenario; Python trainer denylist ingestion smoke.
- Test source added but not executed under Catch2 in this environment: hierarchy specialist → reviewer → Qwen, review-off/failure cases, small responder failed-before-stream vs failed-after-stream, wrong-target checkpoint denial.
- Required target acceptance: MSVC Release/Debug build + full CTest green, native Qwen GPU reload, LM Studio specialist/reviewer model selection, cancellation, actual remote bearer authentication, training-ban race evaluation, and runtime process isolation. No assertion of all-missions-complete.

## Math renderer and native-model guide maintenance (2026-10-08)

**Verified in current offline environment:** `node --check` of the new MathML parser and updated renderer; MathML subset smoke tests for inline/display math, fraction, boxed result, code isolation and safe text; all prior Web UI smoke tests; mission UI synchronized to 25 open missions; source archive integrity and patch applicability (see release log).

**Not verified:** Browser image inspection/visual layout in the local headless Chromium environment (startup timed out); Windows Edge high-DPI rendering; full MSVC build/CTest; other GGUF backend compatibility; any three-concurrent-native-model scenario. The latter remains a documented engineering mission. Existing Qwen native runtime source and GPU configuration were intentionally not changed by this maintenance.

## 0.6.9 timing quick reference (2026-10-08)

- `node projects/web/tests/release_069_run_quick_metrics_smoke.cjs`: verifies Run quick-reference position, completion/live timing, TPS estimate labeling, and persisted original clock anchors.
- Model TPS is an estimate until the backend exposes tokenizer-based inference stats; llama.cpp CLI benchmarks cannot be equated to end-to-end agent task speed.
- A live Windows/Edge test with Qwen and LM Studio remains required.

- Qwen Live 2 idle persistence: `node projects/web/tests/qwen_live_idle_persistence_smoke.cjs`; native journal test cases also check `QwenOutputSnapshot.latest` after finish and ring pruning.
