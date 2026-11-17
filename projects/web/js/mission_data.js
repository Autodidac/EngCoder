// GENERATED from docs/FUTURE_MISSIONS.md by scripts/sync_missions.py. Do not edit.
const ENGCODER_OPEN_MISSIONS = Object.freeze([
  {
    "id": "P0-001",
    "priority": "P0",
    "title": "True process sandbox",
    "details": "Windows Job Objects and POSIX process groups implement descendant termination. Workspace path checks still leave `process.run` outside an OS sandbox. Add isolated low-privilege OS principal/container or explicit safe executable allowlist, Windows restricted token/AppContainer policy, no-follow handle traversal, and junction protection. Verify denial outside the workspace and exercise descendant cancellation with live children.",
    "status": "open"
  },
  {
    "id": "P0-002",
    "priority": "P0",
    "title": "Authenticated remote API",
    "details": "Ordinary serving rejects remote requests while **Serve remotely** is off and requires bearer authentication on protected dynamic routes when enabled. Candidate hosts bind loopback. Verify state-changing routes, CSRF/CORS and TLS/proxy behavior on Windows before remote acceptance.",
    "status": "open"
  },
  {
    "id": "P0-003",
    "priority": "P0",
    "title": "MTP capability certification",
    "details": "Local profile checks reject missing, self-referential or invalid GGUF drafts before stopping a working runtime. Validate draft tokenizer, architecture and metadata against the main model and actual `llama-server` flags before enabling MTP. Test known compatible and incompatible real Qwen pairings, and preserve the active model on failure. Keep MTP within the qualified native model process.",
    "status": "open"
  },
  {
    "id": "P0-004",
    "priority": "P0",
    "title": "Windows native build, Qwen reload, offload and MTP end-to-end",
    "details": "Windows Debug and Release builds and external GGUF inference have separate recorded evidence. Verify built-in Qwen load, exact -ngl after changed GPU %, explicit reload, correct response, physical residency, and negative MTP pairing. On a real split GGUF, link shards in place, keep the active model running until explicit reload, and verify the new path after reload.",
    "status": "open"
  },
  {
    "id": "P0-005",
    "priority": "P0",
    "title": "Learning Remove + Ban end-to-end — evidence reader and serialized ingestion implemented; in-flight weight-training cancellation remains",
    "details": "Existing trainer may consume previously captured verified JSONL evidence without rechecking bans. Deny during every training ingestion, retroactive purge/ban, concurrent ingestion safeguards.",
    "status": "open"
  },
  {
    "id": "P1-006",
    "priority": "P1",
    "title": "Server-side conversation transcript sync",
    "details": "The isolated Windows candidate reconciles interrupted tasks on restart and the in-Codex preview recovered the saved approval. Chats still use localStorage. Build browser-independent transcript reads and reconstruction, transactional checkpoints, corruption recovery, stable chat UUIDs, and a live event transport such as SSE.",
    "status": "open"
  },
  {
    "id": "P1-007",
    "priority": "P1",
    "title": "Measured routing quality",
    "details": "Add independent labeled evaluation, confidence calibration, user correction labels, safe training data provenance, learning-rate scheduling, rollback and benchmark on simple/complex/coding real tasks. Replace heuristic shadow only after proven improvement; optionally research genuine JEPA/JEV latent world-state training if justified.",
    "status": "open"
  },
  {
    "id": "P1-008",
    "priority": "P1",
    "title": "Subagent concurrency/skills — sequential three-tier reviewer and small-model specialist fallback implemented; concurrency/cost budget remains",
    "details": "Add planner/coding review parallel fanout with time/cost budgets, explicit per-provider concurrency, max output size, subagent verification, context contamination defenses, deterministic reduction, cancellation and failure fallback. Current implementation is bounded advisory consultations.",
    "status": "open"
  },
  {
    "id": "P1-009",
    "priority": "P1",
    "title": "EngCoder-owned concurrent native model hosting (small / middle / KeyMaker)",
    "details": "Currently the native GGUF library downloads, imports and runs one model at a time, while simultaneous 9B/27B advisors require an external inference provider. Add independently owned llama.cpp sidecars, persistent GGUF registrations and assignments, per-model ports/health/auth, architecture-specific flag negotiation, startup ownership, cancellation, real RAM/VRAM budgets and safe unload/recovery. Route each native URI to its actual resident backend; no controller or reviewer may replace KeyMaker implicitly. Test all three models concurrently on Windows and in served mode; never advertise MTP pairing with unrelated models.",
    "status": "open"
  },
  {
    "id": "P1-010",
    "priority": "P1",
    "title": "Capability manifest and dynamic UI",
    "details": "Backend capabilities and cache/active state are available for native profiles. Verify a per-model matrix covering context length, tools, vision, draft acceptance, tokenizer, KV formats and loaded state. Use actual provider discovery and live device capabilities to decide which controls to expose.",
    "status": "open"
  },
  {
    "id": "P1-011",
    "priority": "P1",
    "title": "Power-loss safe profile storage and locking",
    "details": "Native profile writes use atomic replacement and Windows requests write-through. Verify durable file and directory flushing across platforms and coordinate profile writes with install, load, cache clearing and weight deletion.",
    "status": "open"
  },
  {
    "id": "P1-012",
    "priority": "P1",
    "title": "Reliable small responder fallback — empty-stream retry implemented; live tests remain",
    "details": "Detect failure midgeneration and retry only when safe with original Foundation model without duplicating partial response; log model switch. Explicit output limits stop the request. Add a live small-to-Foundation availability check.",
    "status": "open"
  },
  {
    "id": "P1-013",
    "priority": "P1",
    "title": "Task restart/cancel/approval lifecycle tests",
    "details": "Pending approvals must survive refresh and execute only with consent. The isolated Windows Release candidate covers cancellation and preparation terminal events, CRLF preservation, patch preflight, paged reads, durable queueing and restart reconciliation. Release fixtures verify Deny, Stop, wrong-call and stale approvals, and preserving an intervening file edit. Inside Codex, the approval cleared after restart and Run info retained the reason after reload. Web fixtures cover Stop during submission, temporary disconnects, terminal cursor replay and attaching one observer when the saved chat is reopened. Working-source integration and a full standalone desktop restart remain open.",
    "status": "open"
  },
  {
    "id": "P1-014",
    "priority": "P1",
    "title": "Cache operation lifecycle serialization",
    "details": "The isolated Windows Release candidate reserves install admission and coordinates preparation, profile changes, transient-cache clearing and confirmed weight deletion. A fixture using local files verifies that only one of two competing callers starts installation, that competing mutations return busy, and that a retry succeeds after the installer exits. The Release API cancelled a delayed native backend twice without extra process launches. Cancellation at successful readiness or during a real download, cross-platform checks and working-source integration remain open.",
    "status": "open"
  },
  {
    "id": "P1-015",
    "priority": "P1",
    "title": "Checkpoint restore path hardening",
    "details": "Existing fixtures cover exact paths and traversal. The isolated patch test restored the original Windows line endings byte for byte. Use handle-relative operations and exercise symlinks, reparse points and Windows junctions for restore and cleanup.",
    "status": "open"
  },
  {
    "id": "P1-016",
    "priority": "P1",
    "title": "Native runtime fault injection",
    "details": "Exercise stderr/exit-code propagation, delayed health, port collisions, repeated reload, child-process cleanup, timeout, cancellation, and fallback without losing the working profile.",
    "status": "open"
  },
  {
    "id": "P1-017",
    "priority": "P1",
    "title": "Inference chain integration",
    "details": "A live isolated coding request exercised Foundation routing, an approved repair, stronger-model escalation and a verified-memory receipt. Extend the chain to advisory addons and verify provider 401, eviction or unload mid-run, and Stop/retry on the same workflow.",
    "status": "open"
  },
  {
    "id": "P1-018",
    "priority": "P1",
    "title": "Training preemption and conversation Foundation acceptance",
    "details": "A one-step CUDA smoke updated a disposable conversational candidate and left the active generation unchanged after rejection. Expand a deduplicated conversation-only batch and assess fresh dialogue across multiple turns before promotion. Keep role-play and C++ curricula in separate addon scopes. Verify checkpoint/resume, VRAM release, evidence deduplication, promotion, rejection, and rollback under realistic workload.",
    "status": "open"
  },
  {
    "id": "P1-019",
    "priority": "P1",
    "title": "Durable future-information mission queue",
    "details": "Convert tasks that exceed configured complexity, budgets, or security authority into SQLite-backed deferred missions with project/session provenance, explicit acceptance criteria, priority, safe resumption, and durable status transitions. The current bottom Missions viewer shows the source-backed engineering backlog only; it is not a live agent mission queue.",
    "status": "open"
  },
  {
    "id": "P1-020",
    "priority": "P1",
    "title": "Main chat/footer end-to-end UX",
    "details": "The browser passed dock resizing and width persistence at 1,440 × 900 and compact overlay at 620 × 800. Run info displayed live check receipts and retained a restart failure after reload. Verify bottom anchoring while streaming, send/regenerate/refresh, toolbar wrapping across 320–768 px, and loaded-model 4K/DPI with Run info open. Include Load/More, the composer and offline MathML in Windows Edge and fallback coverage.",
    "status": "open"
  },
  {
    "id": "P2-021",
    "priority": "P2",
    "title": "Real tokenizer-ID stream with timestamps, prefill/decode throughput and true token counts if backend can provide token callbacks (not inferred from text delta chunk size)",
    "details": "",
    "status": "open"
  },
  {
    "id": "P2-022",
    "priority": "P2",
    "title": "Native on-device vision input / attachments transport (existing UI intentionally restricts to text)",
    "details": "",
    "status": "open"
  },
  {
    "id": "P2-023",
    "priority": "P2",
    "title": "Add public Responses API streaming beyond the buffered endpoint. Evaluate MCP server hosting separately from the existing MCP client integration",
    "details": "",
    "status": "open"
  },
  {
    "id": "P2-024",
    "priority": "P2",
    "title": "Cross-platform CI matrix, optimized performance baselines, sanitizer testing, 4K/zoom UI integration tests and full external local model compatibility checks. Include Windows Debug/Release, native desktop close/relaunch, bottom composer layout, every theme and font scale",
    "details": "",
    "status": "open"
  },
  {
    "id": "P2-025",
    "priority": "P2",
    "title": "Document local data retention, sensitive raw event/redaction policy and multi-user separation before remote enablement",
    "details": "",
    "status": "open"
  },
  {
    "id": "P1-026",
    "priority": "P1",
    "title": "On Windows with LM Studio 9B + 27B and native Qwen loaded, confirm JSON-validated work handoffs and routine C++23 direct routing, including rejection of reasoning-only or token-limit-truncated specialist replies. Confirm automatic small specialist fallback (with no dedicated addon assignment) finishes and the middle reviewer sees its checklist before KeyMaker responds. Confirm no duplicate small-model request on multi-skill refactoring prompts",
    "details": "",
    "status": "open"
  },
  {
    "id": "P1-027",
    "priority": "P1",
    "title": "Verify math/quantitative and sensitive tasks go directly to KeyMaker, with real provider traces rather than heuristic assumptions",
    "details": "",
    "status": "open"
  },
  {
    "id": "P1-028",
    "priority": "P1",
    "title": "Verify served-mode authenticated Web client runs the same three model roles and preserves the correct final responder and per-stage success/failure over refresh/retry",
    "details": "",
    "status": "open"
  },
  {
    "id": "P1-029",
    "priority": "P1",
    "title": "Add per-model observed VRAM/RAM preflight, resource budgets, provider capability probing, concurrent subagent scheduling, and fully isolated tool runtimes before claiming unrestricted multiagent safety",
    "details": "",
    "status": "open"
  }
]);
