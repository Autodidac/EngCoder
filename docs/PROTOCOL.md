# EngCoder served HTTP protocol

HTTP is an adapter over the native `RuntimeHost`. Desktop and browser mode load the same served frontend and call same-origin APIs. Local access is the default; trusted-LAN access is an explicit Serve remotely mode.

## Health

`GET /health`

```json
{"ok":true,"product":"EngCoder","version":"0.6.5","ui":"/ui/"}
```


## Serving and model provider

- `GET /api/engcoder/serving` — local/remote serving status and URLs.
- `POST /api/engcoder/serving` with `{ "remote_enabled": true|false }` — toggles trusted-LAN access. This mutation is accepted only from loopback clients.
- `GET /api/engcoder/provider` — external model-provider endpoint, token presence, and discovery health.
- `POST /api/engcoder/provider` with `{ "token": "..." }` — updates the provider token in the native process; accepted only from loopback clients and never persisted by the Web UI.

The page itself does not select a runtime URL. It talks to the origin that served it.

## Provider discovery

`GET /api/model/discovery` discovers compatible external-provider models. Internal components are exposed by the EngCoder registry endpoints.

## Architecture and models

- `GET /api/engcoder/architecture`
- `GET /api/engcoder/models`

The architecture response includes Architect, KeyMaker Foundation, addons, residency, training, learning, and internal-host state.

## Residency

- `GET /api/engcoder/residency`
- `GET /api/engcoder/residency?load=1`
- `POST /api/engcoder/residency/activate` with `{ "model": "engcoder://cpp" }`
- `POST /api/engcoder/residency/release-addons`

## Training

- `GET /api/engcoder/training/status`
- `POST /api/engcoder/training/doctor`
- `POST /api/engcoder/training/initialize`
- `POST /api/engcoder/training/start`
- `POST /api/engcoder/training/pause`
- `POST /api/engcoder/training/cancel`

## Learning and memory

- `GET /api/engcoder/learning`
- `POST /api/engcoder/learning/evidence`
- `POST /api/engcoder/memory/query`
- `GET /api/engcoder/memory/recent?limit=20`

## Task submission

`POST /api/tasks`

```json
{
  "project_id": "web",
  "session_id": "session-id",
  "message": "inspect the repository and run the tests",
  "foundation_model": "engcoder://keymaker",
  "controller_model": "engcoder://architect",
  "forced_addon": "",
  "auto_route": true,
  "addon_models": {"cpp":"engcoder://cpp"},
  "session_instructions": "Prefer compact answers for this session.",
  "temperature": 0.15,
  "max_tokens": -1,
  "include_history": true,
  "max_history_turns": 20
}
```

The server responds HTTP 202 with `project_id`, `session_id`, and `task_id`. Session instructions augment, but do not replace, the native EngCoder runtime contract. `max_tokens=-1` leaves the provider/internal worker limit at its runtime default; history can be disabled or bounded per session request.

## Events

Important events include `task.*`, `controller.decision`, `addon.*`, `residency.*`, `memory.retrieved`, `model.*`, `tool.*`, `verification.completed`, and `agent.stall_detected`.

`POST /api/tasks/{task_id}/cancel` cancels an active task.

When deterministic/weighted tool policy requires human approval, scheduled HTTP tasks remain alive and emit `tool.approval_required` followed by `task.awaiting_approval`. The latter includes `call_id`, `tool`, immutable `arguments`, `reason`, reviewer, and probabilities. It is a suspended state, not a terminal task result.

`POST /api/tasks/{task_id}/approval` resolves the currently pending call:

```json
{
  "call_id": "call-id-from-task.awaiting_approval",
  "approved": true
}
```

Approval is one-shot and bound to both task and call ID. Approval emits `tool.approved` and executes that exact already-reviewed call. Denial emits `tool.denied`, returns a tool-denied observation to KeyMaker, and lets the same task continue so it can choose another strategy.

Old tool aliases can remain at private compatibility boundaries, but current clients use Architect/KeyMaker/addon terminology and `/api/engcoder/*` routes.


## Native Model Library (0.6+)

Read-only state: `GET /api/engcoder/model-library`.

Loopback-only mutations:

- `POST /api/engcoder/model-library/register`
- `POST /api/engcoder/model-library/install`
- `POST /api/engcoder/model-library/load`
- `POST /api/engcoder/model-library/unload`
- `POST /api/engcoder/model-library/remove`

These endpoints manage EngCoder-owned native runtimes and never change external-provider credentials or internal KeyMaker weights.

### Native model runtime management (0.6+)

- `GET /api/engcoder/model-library` — native runtime/model catalog, cache, download, and lifecycle state.
- `POST /api/engcoder/model-library/runtime/install` — local-only; asynchronously provisions the pinned Qwen Flash CUDA runtime.
- `POST /api/engcoder/model-library/register` — local-only; register a Hugging Face repo/quant or local GGUF.
- `POST /api/engcoder/model-library/install` — local-only; start/verify model weight installation.
- `POST /api/engcoder/model-library/load` — local-only; load/start a native model.
- `POST /api/engcoder/model-library/unload` — local-only; stop the active native model.
- `POST /api/engcoder/model-library/remove` — local-only; remove a user profile and optionally its cache.

Native runtime lifecycle is independent of `/api/engcoder/provider`, which remains the external LM Studio/Bionic/OpenAI-compatible provider surface.
