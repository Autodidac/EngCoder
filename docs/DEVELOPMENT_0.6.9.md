> **Current maintenance note:** The current 0.6.9 three-model setup, external server configuration, subagent fallback rules, and served UI acceptance steps are documented in [SUBAGENTS_QUICKSTART.md](SUBAGENTS_QUICKSTART.md). Historical release records below remain for regression tracking.

# EngCoder 0.6.9 — Model identity and UI contract

**0.6.9 hotfix acceptance:** The first Catch2 case, `Architect conservatively routes straightforward text`, must treat `What is 12% of 180?` and worded/symbolic arithmetic as complex; greetings and ordinary definition questions remain simple. The native routing test was expanded instead of relaxing its expectations. On Windows, verify all 49 CTest cases with `auto.bat` before launching.

## Model hierarchy

- **System 1 Architect:** deterministic conservative intent routing with observed shadow learning; it does not itself claim to be the answer-generating model.
- **Small external responder:** tool-free final responses only for classified simple requests, or a bounded *untrusted* preparatory checklist for eligible complex requests.
- **Independent middle external reviewer:** optional, tool-free audit of the small/specialist notes, never the final responder.
- **KeyMaker Foundation:** owns coding, mathematical, security-sensitive and tool-using final answers. Its selected model is not replaced by addon assignments. Addon model selection is advisory only.
- **MTP draft decoding:** llama backend's compatible internal draft mechanism; unrelated to independent LM Studio responder/reviewer assignments. Do not present unqualified arbitrary external models as MTP drafts.

Small and middle must use *different* external model IDs. Model identities may refer to different loaded provider models; membership in the model discovery list is not proof of physical memory residency or supported reasoning flags. Native Qwen models must not be used as additional addon instances when they could evict the selected foundation.

## Retry and historical model identity

Each message records the actual final responder and its native request role (`small-response` vs `keymaker-foundation`) from `model.request`/`model.response`, along with attempted preparatory and review models. The stored role travels through Web session snapshot, refresh recovery and Retry; never infer the role solely from a model ID prefix, since an LM Studio model can be the KeyMaker foundation.

The right drawer offers **Output** (generated fragments only) and **Run info** (the structured sections, actual model roles, timing, tools and timeline). Historical model labels are derived from per-task events, not today's Lab selectors. Runtime events remain persisted separately and may contain diagnostic details that do not belong in Output.

## Target-machine acceptance

1. Extract the full source archive and run `auto.bat` on Windows; all native CTest tests must pass.
2. Load three distinct models (small, middle, main), assign their roles in Lab, and confirm short greetings go to small while math/coding go to KeyMaker. Inspect the event log and per-message Run info to confirm identities, not just visible answer style.
3. Regenerate both a small-model message and a foundation message, refresh the browser between runs, and confirm each uses the prior actual model/role.
4. Check middle review after small preflight and when unavailable, verify the fallback remains on KeyMaker. Ensure cancellation does not leave delegated inference working after task stop.
5. Verify desktop and served Web UI use the same six themes, preserve separate local font/layout settings, show both right-panel tabs and retain the Chats resize behavior.
6. Repeat with insufficient RAM/VRAM to determine safe physical residency; OS-level sandboxing, actual GPU headroom enforcement, real provider identity for every model, MTP pairing certification, and full Windows live end-to-end tests remain unfinished missions until proven.

Do not remove the previous release acceptance criteria from `docs/FUTURE_MISSIONS.md` or earlier development notes. Preserve the Qwen-specific saved offload percent and never reset the user's working 99% configuration.
