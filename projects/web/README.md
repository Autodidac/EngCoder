# EngCoder shared Web/Desktop frontend

`projects/web` is the canonical EngCoder product UI. Browser mode and the normal Windows desktop application render this exact frontend; the native ImGui UI is diagnostic fallback only.

The frontend deliberately preserves the original BotFace-derived chat experience instead of turning the chat window into a model-management dashboard:

- chat/composer/history/edit/regenerate/copy/verify/attachments/voice remain first-class;
- Session Settings owns conversation/runtime/voice/Run info/appearance controls;
- EngCoder Lab separately owns Architect, KeyMaker, addon, GPU residency, training, and learning controls.

JavaScript talks only to the native EngCoder runtime API. It never connects directly to LM Studio or owns a competing supervisor/worker agent loop. `deno/` is optional development hosting only.

## Model Library (0.6+)

EngCoder Lab > Models includes the native Model Library for downloadable/imported runtime models. Qwen Flash profiles expose populated recommended settings rather than blank knobs: model source/quant, total context, slots/effective context, automatic compaction, batching, KV placement/types, MoE cache budgets, mmap/lazy policy, and optional MTP speculation. The browser only calls the native EngCoder model-library API; model downloads and runtime processes are owned by the C++ runtime, never by browser JavaScript.

## Generated output panel (0.6.8 hotfix)

The right-hand Generated output panel displays provider-streamed text, provider-exposed reasoning, and generated tool argument fragments as plain text, without diagnostic event names or runtime JSON. Native session events remain recorded for recovery/debugging. Existing Web transcript entries from older raw-log builds show their saved assistant text instead of restoring diagnostic JSON.

## Qwen live output across clients

The **Output** tab stays specific to the selected conversation. The **Qwen live** tab displays the most recent native Qwen stream, including inference requested by external OpenAI-compatible clients at `http://127.0.0.1:14321/v1` (such as a tool or MCP integration that uses EngCoder as its inference host). **Run info** is unchanged.

While the UI is open, a new external Qwen request switches to **Qwen live** if no local chat task is active. Polling is cursor-based and bounded; it does not record the model's prompt or telemetry in the visible stream. A remote browser needs the configured EngCoder bearer token; this is an **administrator-level host-wide feed**, not a per-user private feed. Do not share that token with untrusted users.

The captured data is provider-emitted text/reasoning/tool-argument *fragments*, not model tokenizer IDs. Calls that bypass EngCoder and go directly to llama-server (`:8188`) or LM Studio (`:1234`) cannot be observed. For those callers, point their OpenAI-compatible base URL at EngCoder's `/v1` route instead.
