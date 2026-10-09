# EngCoder future-information and engineering missions (0.7.1)

Keep all unresolved missions until acceptance evidence is recorded. Status labels describe **work remaining**, not feature promises.

## P0 — safety and backend compatibility

- [ ] **True process sandbox.** Workspace path checks do not contain `process.run`. Add isolated low-privilege OS principal/container or explicit safe executable allowlist; Windows restricted token/AppContainer policy, no-follow handle traversal and junction protection. Tests: a tool attempting to access files outside the project must be denied; cancel kills descendants.
- [ ] **Authenticated remote API — partially implemented (requires penetration/Windows verification).** Prevent remote clients from invoking state-changing `/api/tasks`, model management or tool approvals without authentication; verify CSRF/CORS and TLS/proxy setup. Until then bind loopback only.
- [ ] **MTP capability certification.** Validate draft GGUF tokenizer/architecture/metadata against the internal main model and actual `llama-server` supported switches before presenting Enable. Test known compatible/incompatible pairing on real Qwen; display a clear diagnostic, preserve active model on failure. Do not use LM Studio as cross-process MTP.
- [ ] **Windows native build, Qwen reload, offload and MTP end-to-end.** Capture compiler/test outputs and confirmed GPU residency. Verify Qwen load, exact -ngl after changed GPU %, explicit reload, correct response, and negative test for incompatible MTP.
- [ ] **Learning Remove + Ban end-to-end — evidence reader and serialized ingestion implemented; in-flight weight-training cancellation remains.** Existing trainer may consume previously captured verified JSONL evidence without rechecking bans. Deny during every training ingestion, retroactive purge/ban, concurrent ingestion safeguards.

## P1 — session durability, controller and multiagent quality

- [ ] **Server-side conversation transcript sync** (Web UI currently uses localStorage, subject to quota). Build browser-independent transcript read API and event reconstruction, transactional checkpoints, corruption recovery, stable chat UUIDs, terminal-task reconciliation after native restart, and a true live event transport such as SSE.
- [ ] **Measured routing quality.** Add independent labeled evaluation, confidence calibration, user correction labels, safe training data provenance, learning-rate scheduling, rollback and benchmark on simple/complex/coding real tasks. Replace heuristic shadow only after proven improvement; optionally research genuine JEPA/JEV latent world-state training if justified.
- [ ] **Subagent concurrency/skills — sequential three-tier reviewer and small-model specialist fallback implemented; concurrency/cost budget remains.** Add planner/coding review parallel fanout with time/cost budgets, explicit per-provider concurrency, max output size, subagent verification, context contamination defenses, deterministic reduction, cancellation and failure fallback. Current implementation is bounded advisory consultations.
- [ ] **EngCoder-owned concurrent native model hosting (small / middle / KeyMaker).** Currently the native GGUF library downloads, imports and runs one model at a time, while simultaneous 9B/27B advisors require an external inference provider. Add independently owned llama.cpp sidecars, persistent GGUF registrations and assignments, per-model ports/health/auth, architecture-specific flag negotiation, startup ownership, cancellation, real RAM/VRAM budgets and safe unload/recovery. Route each native URI to its actual resident backend; no controller or reviewer may replace KeyMaker implicitly. Test all three models concurrently on Windows and in served mode; never advertise MTP pairing with unrelated models.
- [ ] **Capability manifest and dynamic UI.** Runtime-backed per-model compatibility matrix: context length, tools, vision, draft acceptance, tokenizer, KV formats, loaded state. Hide options until verified; use actual provider discovery with changing models and live device capabilities.
- [ ] **Power-loss safe profile storage and locking.** Atomic write + fsync + rename for native settings; race-free install/load/clear-cache/delete weights.
- [ ] **Reliable small responder fallback — empty-stream retry implemented; live tests remain.** Detect failure midgeneration and retry only when safe with original Foundation model without duplicating partial response; log model switch.
- [ ] **Task restart/cancel/approval lifecycle tests.** Pending approvals must survive refresh correctly but never execute without consent; no orphaned tasks on backend restart.

- [ ] **Cache operation lifecycle serialization.** Coordinate model downloads, load/unload, transient-cache clearing, and confirmed weight deletion under one operation lock; close time-of-check/time-of-use deletion races. Test concurrent install vs. clear/delete and cancellation.
- [ ] **Checkpoint restore path hardening — exact-path bindings and traversal checks implemented; handle-relative Windows reparse-point tests remain.** Validate untrusted checkpoint IDs and use secure handle-relative operations; test `..`, symlinks, reparse points, and Windows junctions before permitting restore or cleanup.
- [ ] **Native runtime fault injection.** Exercise stderr/exit-code propagation, delayed health, port collisions, repeated reload, child-process cleanup, timeout, cancellation, and fallback without losing the working profile.
- [ ] **Inference chain integration.** Verify an actual `/api/tasks` request traverses Architect -> chosen foundation -> advisory addons/tools -> approval -> verified memory, including provider 401/unload mid-run and stop/retry.
- [ ] **Training preemption correctness.** Confirm candidate weights never replace active weights; test checkpoint/resume, VRAM release, evidence deduplication, promotion gates, rejection, and rollback under realistic workload.

- [ ] **Durable future-information mission queue.** Convert tasks that exceed configured complexity, budgets, or security authority into SQLite-backed deferred missions with project/session provenance, explicit acceptance criteria, priority, safe resumption, and durable status transitions. The current bottom Missions viewer shows the source-backed engineering backlog only; it is not a live agent mission queue.
- [ ] **Main chat/footer end-to-end UX.** Verify auto-scroll and bottom anchoring during streaming, send/regenerate/refresh, toolbar wrapping at 320–768 px, and 4K high-DPI behavior with loaded models and the run-info drawer open. Include the repaired Load/More overlay, stable chat composer layout, and offline-safe rendering of math notation (safe MathML subset implemented; verify layout in Windows Edge and fallback cases).

## P2 — broader development

- [ ] Real tokenizer-ID stream with timestamps, prefill/decode throughput and true token counts if backend can provide token callbacks (not inferred from text delta chunk size).
- [ ] Native on-device vision input / attachments transport (existing UI intentionally restricts to text).
- [ ] Responses API streaming if needed; MCP server host if desired (existing integration is MCP client only).
- [ ] Cross-platform CI matrix, optimized performance baselines, sanitizer testing, 4K/zoom UI integration tests and full external local model compatibility checks. Include Windows Debug/Release, native desktop close/relaunch, bottom composer layout, every theme and font scale.
- [ ] Document local data retention, sensitive raw event/redaction policy and multi-user separation before remote enablement.

## Release acceptance: checklist for 0.6.8 verification

1. Native Windows x64 Release and Debug build with vcpkg; CTest green and no new warnings.
2. In Edge, send task; observe raw `model.text_delta`/tool events in right panel; refresh halfway; same task/session resumes without resending and final answer remains after another refresh.
3. Confirm simple text invokes the assigned LM Studio small model and coding invokes Qwen; no tools offered to simple models; reroute unknown requests conservatively.
4. Confirm assigned addon invokes its LM Studio model, returns advisory text, and never runs tools directly. Cancel test.
5. Confirm MTP choice appears only where backend+draft can be qualified, with negative pairing rejected **before** stopping working Qwen.
6. Run unsafe outside-workspace tool probe in a controlled test and capture denial before claiming fully sandboxed.
7. Preserve backlog and attach source logs; do not mark completed from source-only analysis.

## 0.6.8 evidence and carry-forward policy

The checked boxes above remain open until their complete acceptance criteria pass on the target platform. A partial source implementation is not a completed mission. The 24 existing objectives and their order are retained, including all older UI/load/cache missions. Before each new release, re-run the Windows test suite and document any new regressions. The app's Missions footer is generated from this canonical Markdown backlog.

For true independent subagents or more sophisticated speculative MTP, first verify provider capabilities, memory cost, cancellation, and security invariants rather than advertising incompatible options. Do not change existing user GPU offload settings during migration.

## 0.6.9 subagent acceptance still required

- [ ] On Windows with LM Studio 9B + 27B and native Qwen loaded, confirm JSON-validated work handoffs and routine C++23 direct routing, including rejection of reasoning-only or token-limit-truncated specialist replies. Confirm automatic small specialist fallback (with no dedicated addon assignment) finishes and the middle reviewer sees its checklist before KeyMaker responds. Confirm no duplicate small-model request on multi-skill refactoring prompts.
- [ ] Verify math/quantitative and sensitive tasks go directly to KeyMaker, with real provider traces rather than heuristic assumptions.
- [ ] Verify served-mode authenticated Web client runs the same three model roles and preserves the correct final responder and per-stage success/failure over refresh/retry.
- [ ] Add per-model observed VRAM/RAM preflight, resource budgets, provider capability probing, concurrent subagent scheduling, and fully isolated tool runtimes before claiming unrestricted multiagent safety.
