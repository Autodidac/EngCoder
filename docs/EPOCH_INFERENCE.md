# EpochEngine inference through EngCoder

Epoch uses EngCoder's inference-only OpenAI-compatible surface on port `14321`.
This does not grant EngCoder write access to Epoch and does not enter the `/api/tasks`
agent workflow.

## Routes

- `POST /v1/chat/completions` — Chat Completions inference, streaming or non-streaming.
- `POST /v1/responses` — Responses-format inference. Kept separate; this route is non-streaming.
- `POST /api/tasks` — EngCoder's full Architect/KeyMaker/addon/tool/memory workflow.
- `GET /health` and status routes remain short-timeout operations during generation.

## Deadline contract

Generation has a separate default budget of three hours (`ENGCODER_INFERENCE_TIMEOUT_SECONDS=10800`).
A caller may set a longer or shorter per-request budget with `timeout_seconds`, `timeout_ms`,
or `X-EngCoder-Timeout-Ms`. The absolute deadline is propagated through routing, Qwen Flash
startup/recovery, model loading, and generation. Recovery attempts consume the same remaining
budget instead of starting a new timeout.

Discovery and health probes remain bounded to short timeouts.

## Tool contract

Chat Completions preserves `tools`, `tool_choice`, `parallel_tool_calls`, and output limits.
Supported choices are `auto`, `none`, `required`, and named functions. Unsupported or internally
inconsistent combinations are rejected as structured invalid requests rather than silently rewritten.
Responses requests are translated only where that protocol uses a different named-function shape.

## Streaming

`stream=true` on `/v1/chat/completions` emits OpenAI-compatible SSE chunks for assistant content,
reasoning deltas, tool-call metadata, and incremental function arguments. Tool arguments are not
reported as a completed tool call until the upstream stream has ended with valid complete JSON.
The stream terminates with a final `finish_reason` chunk and `data: [DONE]`.

## Cancellation and duplicate protection

Client disconnects request upstream cancellation. After an interrupted generation, EngCoder checks
the upstream slot state before admitting another generation. If termination cannot be confirmed,
it reports that generation may still be running rather than overlapping another request. Identical
concurrent inference bodies are rejected with `duplicate_inference`.

## Qualification

Run from the EngCoder source root while the Windows runtime and selected Qwen model are available:

```powershell
.\scripts\qualification\qualify-epoch-inference.ps1
```

The script sends a non-streaming Epoch-style source-proposal request with a required named function
and writes the complete response or exact structured failure to `out/qualification/`. It requests a
proposal only and has no file-write authority.
