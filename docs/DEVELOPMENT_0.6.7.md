# EngCoder development and operational guide — 0.6.7

## Invariants for all future releases

1. Maintain one agent/runtime host (C++23), one Web UI, and the existing window stack. Do not create a second HTTP runtime or replace the window manager.
2. Keep `POST /api/tasks` (agent+approvals+session events) distinct from `/v1/chat/completions` (direct inference). Preserve saved chat/session schema and run compatibility migrations.
3. Use `engcoder::component` namespaces, C++23, platform-neutral sources under `include/engcoder` and `src`; Windows implementation under `src/process/process_windows.cpp`, POSIX under `process_posix.cpp`.
4. Never silently reload native models when a setting changes. Apply/reload must be explicit and must validate a prospective profile **before** stopping the healthy backend. Cache cleaning must never delete GGUF weights.
5. No automatic training from tool outputs; respect Remove + Ban across every ingestion and training path. This guarantee is **not yet completed**; see future missions.
6. Subagent models are untrusted advisors, not authorized tool executors. Foundation and C++ tool policy own all side effects; project workspace roots are *not* a secure OS sandbox by themselves.
7. Persist working state and expose failures. Keep the event store and model identity visible. Do not describe estimated token counts as tokenizer-accurate counts.
8. Never advertise an inference capability using a name string alone. Require provider discovery and native runtime/weight qualifications; do not assume all MTP draft models interoperate.

## How 0.6.7 routes work

- Input is classified conservatively into `simple`, `coding`, or `complex` in `controller/task_intent.cpp`. Unknown input escalates to `complex`.
- Architect can select a configured specialist addon (user-assigned LM Studio model) and a bounded advisory consultation; no tools are sent to those advisors. AgentRuntime then chooses the final responder: the external simple-response model only for `simple` + automatic routing, otherwise KeyMaker Foundation (internal Qwen).
- `controller/PredictiveRouteShadow` consumes numeric derived features and policy labels only, journaled under data root `controller/route-shadow-v1.samples`. Prediction is **observe-only**, never authoritative. This is an online classifier development scaffold, *not* a trained JEPA/JEV.
- The Web selector is explicit: a simple response stays on Qwen if no external model is assigned. Only discovered external models can be selected; stale saved IDs show a fallback message instead of being silently used. The automatic routing toggle controls final responder selection independently of capability-addon enablement.
- Debug route using `controller.task_classified`, `controller.decision`, `controller.model_selected`, `addon.*`, `model.*` task events in right panel.

## Chat save/restore

- Native SQLite session store appends user messages/events and completed model responses.
- Web creates the assistant record before task submission, checkpoints streamed text and a durable `task_id`/event sequence every ~150ms, flushes on page hide, and on refresh **polls the existing session** instead of resending the prompt.
- Web snapshots are in browser localStorage; quota/clear-browser-data are outstanding reliability limitations. They are not synced across browsers or computers. Restarting the native backend during a task can leave an in-progress record until it is reconciled (mission P1).

## Native MTP vs external subagents

- `draft-mtp` is a native llama.cpp execution mode requiring an independent, compatible GGUF draft. The UI must require a local draft file and report compatibility restrictions before use.
- A discovered LM Studio model is usable as a small final-text responder or independent specialist subagent, **not** a speculative-decoding sidecar of llama.cpp. Unsupported draft type ngram-mod and self-HF draft repo are not exposed.
- Local GGUF magic and path separation can be checked before launch, but tensor compatibility, tokenizer, model architecture and exact backend flags require qualification on the actual backend; complete capability certification is P0.

## Build and validation

Run `auto.bat` or the documented Windows CMake/vcpkg build on the target machine, run `check.bat`/CTest and test native Qwen load -> text -> change GPU scale -> Apply/reload -> text, MTP disabled/enabled with a verified draft, then cancel/refresh/resume. Run two concurrent addons with LM Studio and watch routing events. On Linux, `cmake --preset` depends on installed vcpkg dependencies; a single independent `task_intent.cpp` can be compiled with `g++ -std=c++23` for a minimal smoke test.

Never promote a version to verified from only JS syntax checks: capture the Windows compiler output, all CTest results, a live Qwen+LM Studio session trace and MTP backend log.

## Source areas

| Responsibility | Files |
| --- | --- |
| Intent/predictor | `projects/native/{include/engcoder/controller/task_intent.hpp,src/controller/task_intent.cpp}` |
| Routing/subagents | `projects/native/src/controller/architect_controller.cpp`, `src/agent/agent_runtime.cpp`, `src/addons/addon_registry.cpp` |
| Tool authority | `projects/native/src/tools/builtin_tools.cpp`, `src/tools/tool_registry.cpp` |
| Durable events | `projects/native/src/persistence/session_store.cpp`, `src/server/http_server.cpp` |
| Browser streaming | `projects/web/js/api.js`, `state.js`, `render.js`, `main.js` |
| Native model/MTP | `projects/native/src/model/runtime_model.cpp`, `qwen_flash_runtime.cpp`, `native_model_manager.cpp` |
| Requirements/deferrals | `docs/FUTURE_MISSIONS.md` |

## 0.6.7 UI and routing regression procedure

1. Connect LM Studio with a small text model. In EngCoder Lab, assign the discovered model under **Simple response model**. Leave automatic routing on.
2. Send `hi`, `HI!`, `hey?`. The right-side event stream must report `controller.task_classified` intent `simple` and `controller.model_selected` role `small-response` with the chosen external model. No tool definitions go to this responder.
3. Disable **Enable capability addons** while leaving automatic routing on. Send `hi` again: the small model must still be selected. Then send a Vulkan/C++ refactor request: it must select KeyMaker.
4. Remove the selected small model from LM Studio and refresh. UI must indicate the fallback; EngCoder must not advertise that unavailable model.
5. With no files attached, confirm that the composer has no empty attachment counter. Add a text file and confirm the count appears; remove it and confirm it disappears.
6. With the internal Qwen profile already configured at 99%, verify that the setting survives application restart. **Do not reset the requested percentage** or perform a reload on settings change. With 48-layer hint and the existing capped scale, 99% and 100% both resolve to `-ngl 29`.
7. Run the CTest suite and inspect the Missions footer (24 unfinished objectives). Actual native Qwen/LM Studio inference must be verified on Windows before marking the release fully accepted.
