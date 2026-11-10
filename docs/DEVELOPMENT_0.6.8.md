# EngCoder 0.6.8 — development and acceptance

## Architecture (no UI redesign)

**System 1 / Architect:** conservative task classification and shadow training. Simple-only requests may go to a separately configured LM Studio response model. Complex or coding work stays on the internal KeyMaker Qwen foundation; if a configured small responder fails **before** producing any fragment, Qwen is tried once with no tools, but never after partial output.

**Subagents:** automatic routing consults at most two configured specialist addon models, bounded to 512 output tokens and 90 seconds, with no tool authority. The optional, independent LM Studio **Middle review model** checks specialist notes without tools, with 512 tokens / 75 seconds (Auto) or 1024 tokens / 150 seconds (Deep). The main Qwen receives both as untrusted evidence, decides the answer, and retains tool approvals. Reviewer failure never changes the main model. If you assign the same model to specialist and reviewer, review is skipped to avoid redundant review. Do not assert that execution has been parallelized; it is sequential by design.

**Analysis modes:** KeyMaker Auto/Fast/Deep are **prompt-level instructions**; actual GPU/backend reasoning budget flags may be offered in later versions only where provider capability manifests verify support. Auto applies deeper verification on coding requests, Fast favors concision, Deep asks the foundation to cross-check evidence and tests. No mode increases actual context or enables speculative decoding.

**Configuration:** In the existing EngCoder Lab / Models / Architect + KeyMaker selector, choose foundation, small-response model, each addon model, and an optional *different* middle review model. Set Multi-model verification Auto/Deep/Off and KeyMaker analysis Auto/Fast/Deep. The Qwen GPU scale and user-customized 99% setting are unchanged, and no unsupported MTP pairing is advertised.

**Observability:** Internally persisted native events include addon.requested/completed/failed, review.requested/completed/failed, controller.escalated, and model output fragments. Right panel still shows **only generated model text**, never the noisy controller/runtime metadata.

## Safety and limitations

- Workspace path and checkpoint ID validation now prevent traversal and bind new checkpoint IDs to captured relative paths. Missing-file checkpoints cannot delete a different file. Process tools still run with host identity: no complete OS isolation and Windows junction/TOCTOU checks are not certified.
- Learning Remove+Ban is checked during training evidence loading and immediately before training datasets are composed. Existing workers may require a restart for revocation: in-flight weight training is not proven stoppable.
- For opt-in LAN access, set `ENGCODER_REMOTE_API_TOKEN` to an unpredictable 32+ character secret before launching. All remote APIs require `Authorization: Bearer <token>`. Local UI remains loopback. Browser session stores the remote token until the tab ends. HTTP LAN traffic is not encrypted: require a trusted/private network and TLS reverse proxy; do not expose on the public Internet.
- Native draft-MTP remains internal llama.cpp with validated compatible GGUF only, not arbitrary LM Studio draft model. Shadow-trained controller is not a qualified JEPA neural predictor.

## Acceptance steps

1. Build EngCoder native Release and Debug on Windows with project vcpkg; run **all** CTest tests with 0 failures and check for warnings.
2. In Lab, assign small LM Studio specialist, distinct middle reviewer, and internal Qwen; ask a complex coding task. Confirm event order `addon.completed` → `review.completed` → foundation `model.request` and final Qwen response.
3. Test reviewer disabled and unavailable; Qwen should still complete. Stop midway; no orphaned tasks. Short greeting should bypass specialists if a simple responder is assigned.
4. Test 99% Qwen GPU offload and reload, verify effective `-ngl` without changing profile; qualified GGUF draft MTP only.
5. Confirm the right output panel remains generated fragments only; refresh during streaming and verify conversation persistence.
6. Confirm `ENGCODER_REMOTE_API_TOKEN` guards remote APIs. Block remote when no secret configured, reject cross-origin and invalid bearer. Use TLS off-host.
7. Verify training-ban regression under concurrent ingestion and retraining, checkpoint ID traversal, wrong-file restore and Windows symlink/junction attempt. Do not claim Windows OS sandbox until it actually denies outside-workspace tool process access.
8. Keep `docs/FUTURE_MISSIONS.md` as the canonical source-backed list; an unverified mission is open even when initial source work exists.
