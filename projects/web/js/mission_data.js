// GENERATED from docs/FUTURE_MISSIONS.md by scripts/sync_missions.py. Do not edit.
const ENGCODER_OPEN_MISSIONS = Object.freeze([
  {
    "id": "P0-001",
    "priority": "P0",
    "title": "True process sandbox",
    "details": "Workspace path checks do not contain `process.run`. Add isolated low-privilege OS principal/container or explicit safe executable allowlist; Windows restricted token/AppContainer policy, no-follow handle traversal and junction protection. Tests: a tool attempting to access files outside the project must be denied; cancel kills descendants.",
    "status": "open"
  },
  {
    "id": "P0-002",
    "priority": "P0",
    "title": "Authenticated remote API — partially implemented (requires penetration/Windows verification)",
    "details": "Prevent remote clients from invoking state-changing `/api/tasks`, model management or tool approvals without authentication; verify CSRF/CORS and TLS/proxy setup. Until then bind loopback only.",
    "status": "open"
  },
  {
    "id": "P0-003",
    "priority": "P0",
    "title": "MTP capability certification",
    "details": "Validate draft GGUF tokenizer/architecture/metadata against the internal main model and actual `llama-server` supported switches before presenting Enable. Test known compatible/incompatible pairing on real Qwen; display a clear diagnostic, preserve active model on failure. Do not use LM Studio as cross-process MTP.",
    "status": "open"
  },
  {
    "id": "P0-004",
    "priority": "P0",
    "title": "Windows native build, Qwen reload, offload and MTP end-to-end",
    "details": "Capture compiler/test outputs and confirmed GPU residency. Verify Qwen load, exact -ngl after changed GPU %, explicit reload, correct response, and negative test for incompatible MTP.",
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
    "details": "(Web UI currently uses localStorage, subject to quota). Build browser-independent transcript read API and event reconstruction, transactional checkpoints, corruption recovery, stable chat UUIDs, terminal-task reconciliation after native restart, and a true live event transport such as SSE.",
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
    "details": "Runtime-backed per-model compatibility matrix: context length, tools, vision, draft acceptance, tokenizer, KV formats, loaded state. Hide options until verified; use actual provider discovery with changing models and live device capabilities.",
    "status": "open"
  },
  {
    "id": "P1-011",
    "priority": "P1",
    "title": "Power-loss safe profile storage and locking",
    "details": "Atomic write + fsync + rename for native settings; race-free install/load/clear-cache/delete weights.",
    "status": "open"
  },
  {
    "id": "P1-012",
    "priority": "P1",
    "title": "Reliable small responder fallback — empty-stream retry implemented; live tests remain",
    "details": "Detect failure midgeneration and retry only when safe with original Foundation model without duplicating partial response; log model switch.",
    "status": "open"
  },
  {
    "id": "P1-013",
    "priority": "P1",
    "title": "Task restart/cancel/approval lifecycle tests",
    "details": "Pending approvals must survive refresh correctly but never execute without consent; no orphaned tasks on backend restart.",
    "status": "open"
  },
  {
    "id": "P1-014",
    "priority": "P1",
    "title": "Cache operation lifecycle serialization",
    "details": "Coordinate model downloads, load/unload, transient-cache clearing, and confirmed weight deletion under one operation lock; close time-of-check/time-of-use deletion races. Test concurrent install vs. clear/delete and cancellation.",
    "status": "open"
  },
  {
    "id": "P1-015",
    "priority": "P1",
    "title": "Checkpoint restore path hardening — exact-path bindings and traversal checks implemented; handle-relative Windows reparse-point tests remain",
    "details": "Validate untrusted checkpoint IDs and use secure handle-relative operations; test `..`, symlinks, reparse points, and Windows junctions before permitting restore or cleanup.",
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
    "details": "Verify an actual `/api/tasks` request traverses Architect -> chosen foundation -> advisory addons/tools -> approval -> verified memory, including provider 401/unload mid-run and stop/retry.",
    "status": "open"
  },
  {
    "id": "P1-018",
    "priority": "P1",
    "title": "Training preemption correctness",
    "details": "Confirm candidate weights never replace active weights; test checkpoint/resume, VRAM release, evidence deduplication, promotion gates, rejection, and rollback under realistic workload.",
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
    "details": "Verify auto-scroll and bottom anchoring during streaming, send/regenerate/refresh, toolbar wrapping at 320–768 px, and 4K high-DPI behavior with loaded models and the run-info drawer open. Include the repaired Load/More overlay, stable chat composer layout, and offline-safe rendering of math notation (safe MathML subset implemented; verify layout in Windows Edge and fallback cases).",
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
    "title": "Responses API streaming if needed; MCP server host if desired (existing integration is MCP client only)",
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
  }
]);
