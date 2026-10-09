# EngCoder repository instructions

EngCoder is a local-first C++23 AI application. Preserve this architecture unless an explicit design change is requested.

## Canonical architecture

1. **Architect is the fast controller/harness layer.** It owns routing, retrieval decisions, capability activation, escalation, stopping, and orchestration. It is not the main prose model.
2. **KeyMaker is the local learned-model and training family.** The KeyMaker Foundation is the resident English language/reasoning model.
3. **Capabilities are modular KeyMaker addons.** C++23, documentation, planning, tool use, reranking, and future domains remain independently addressable learned lanes/components.
4. **Native C++ policy is authoritative.** Learned components may recommend actions but cannot bypass workspace containment, approvals, checkpoints, validation, or tool policy.
5. **One orchestration authority.** `RuntimeHost` owns agent state. Desktop/Web/HTTP are adapters, never competing agent loops.
6. **One product identity: EngCoder.** Prior UI and architecture names are implementation history only and must not appear as current product branding.
7. **Training belongs to EngCoder.** The Python/PyTorch worker is a replaceable numerical backend controlled by C++, not a second application.
8. **Inference outranks training.** Training must checkpoint/preempt safely for foreground inference and resume from the checkpoint.
9. **Residency is physical.** Internal CUDA inference follows NVMe -> RAM warm cache -> VRAM hot set. Architect and KeyMaker are pinned; addons page in/out under count and byte budgets.
10. **Verified memory and neural learning remain separate.** Evidence may become retrieval memory immediately; weight promotion requires candidate training and evaluation.

## Naming and compatibility

- New public terminology is EngCoder, Architect, KeyMaker Foundation, and KeyMaker Addons.
- New public routes use `/api/engcoder/*` and new environment variables use `ENGCODER_*`.
- Legacy aliases may remain only at compatibility boundaries required to read older state or serve older clients. Never expose those aliases in the normal UI or current documentation.
- `expert.*` tool names are deprecated compatibility aliases for `addon.*`.
- Do not introduce `.inl` files.

## C++ rules

- C++23, RAII, `std::expected`, explicit ownership, bounded concurrency, and composition by default.
- Keep platform-neutral and platform-specific files clearly separated.
- Prefer deterministic data/control paths over unnecessary polymorphism in hot code.
- Treat filesystem, process, model-provider, and retrieved content as untrusted input.
- Preserve cross-platform build definitions and exports.
- Do not weaken checks just to make a build pass.

## Model/runtime rules

- Never upload every addon to CUDA at startup and then evict it. Cold addons start in system RAM.
- Never evict pinned Architect/KeyMaker components as part of normal addon LRU.
- Keep logical residency and worker-reported physical CUDA residency distinguishable in API/UI.
- Generation manifests/checksums are part of the load contract. Never activate corrupt or ABI-incompatible artifacts.
- A candidate must never overwrite active weights in place.
- External providers are optional teachers/fallbacks. If LM Studio evicts a selected model, EngCoder should restore it automatically when its native model-management API is available.

## UI rules

- Desktop and Web use the same `projects/web` frontend for permanent feature parity. The native ImGui surface is diagnostic fallback only.
- Preserve EngCoder Dark and EngCoder Light and keep Windows System, Windows Dark, Windows Light, and Matrix Green at feature parity.
- Session Settings stays chat/session focused. Architect/KeyMaker/addon, GPU, training, and learning controls belong in the separate EngCoder Lab surface; do not consume the normal chat settings with engineering controls.
- The shared Web page always uses the runtime origin that served it. Do not reintroduce Current/Local/Custom runtime selectors or browser-configured EngCoder endpoints.
- **Serve remotely** is the sole normal network-exposure control. It is off by default, exposes the same runtime/UI to a trusted LAN, and must remain locally controlled.
- Normal settings use discovery, dropdowns, toggles, and status cards. Raw model/provider details belong in EngCoder Lab or automation environment variables, not ordinary Session Settings.
- Activity shown in chat is compact structured telemetry, not hidden chain-of-thought. Completed activity collapses to a `Worked for ...` summary by default.
- The browser calls the native runtime only; never reintroduce browser-to-model orchestration.
- The native fallback must remain per-monitor-DPI aware and prefer modern system fonts.

## Work discipline

Read this file and the canonical docs before architectural changes. Inspect existing code before editing. Make coherent changes, run the strongest available build/tests, record real limitations in `VALIDATION.md`, and never claim a platform build that was not executed.

### Model routing maintenance (0.6.9)

Preserve the three independent roles and tool authority boundaries: System 1 Architect/controller, external small/checklist-only specialist, independent external middle reviewer, and KeyMaker final responder. A model attempted and rejected during controller classification is not a completed subagent. A checklist from an unassigned specialist must be bounded, non-authoritative, have `tool_choice=none`, and be routed through review if one is configured. Mathematical and reliability-sensitive tasks bypass this path. Keep native vs LM Studio setup precise; remote browser connects to EngCoder, while native EngCoder connects to LM Studio. Use docs/SUBAGENTS_QUICKSTART.md and keep docs/FUTURE_MISSIONS.md intact.

### Chat math and multi-native hosting (0.6.9 maintenance)

Use `projects/web/js/math.js` for offline, DOM-only MathML formatting. Preserve raw generated model output and never evaluate HTML/JS from model content. Keep code fences literal. Refer to `docs/MATH_AND_NATIVE_MODELS_0.6.9.md` before editing native model lifecycle. The existing native Qwen runtime currently owns **one** model slot; do not represent the separate small/middle LM Studio models as native sidecars or cross-process MTP. A full multi-model native pool is a new P1 mission requiring memory budgets, process containment, architecture-qualified flags, and real Windows certification. The user-configured Qwen GPU scale/offload must remain untouched.

### Preparatory model output contract (0.6.9 maintenance)

Non-authoritative 9B/27B advisors must emit compact structured handoffs, not raw reasoning or source-code replacements. Validate provider text, tool calls, and `finish_reason` before admitting notes to KeyMaker context. Send KeyMaker the complete original request after untrusted notes. Use `warrants_specialist_handoff` to avoid calling small/reviewer models for basic code examples. Never retry a failed small preflight automatically, and never downgrade the final responder's tool authority or change the Qwen GPU profile as a routing side effect. Preserve every mission in `docs/FUTURE_MISSIONS.md`.

### 0.7.1 preservation contract

The Qwen GPU control remains 0..100% on the established 60%-layer ceiling. Only the default changed from 95% to 50%; do not alter its scale or overwrite saved user profiles (including custom 99%). Version identifiers for the UI, CMake, vcpkg and training worker must agree. Qwen Live 2 idle-retained output and all mission entries are protected regression behavior.
