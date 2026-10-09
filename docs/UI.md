# EngCoder UI contract

Desktop and Web are one product surface. On Windows, normal `EngCoder.exe` owns the runtime/loopback adapter and opens the exact same `projects/web` frontend used by browser mode. `--native-ui` opens the diagnostic ImGui fallback.

## Chat-first surface

The shared frontend preserves the full BotFace-derived interaction shell: GPT-style composer, attachments, expanding input, voice controls, send/stop, chat history, editing/regeneration, copy/verify actions, local persistence, chats drawer, and per-message Run info drawer. EngCoder extends that shell; it does not replace it with a runtime dashboard.

## Run info

Each assistant response owns compact structured runtime telemetry rather than a large debug console:

```text
Worked for 18s  >
  Accepted request
  Architect selected C++ addon
  Recalled verified memory
  Loaded C++ addon
  Ran build
  Verified result
```

Run info summarizes Architect routing/model time, KeyMaker inference, addons, tool calls, memory retrieval, and task timing. The optional runtime timeline collapses by default unless the user chooses otherwise. It reports observable runtime events; it never exposes or fabricates hidden chain-of-thought. The report is stored with its chat message and deleted with the chat/native runtime session.

## Session Settings

Session Settings stays compact and conversation-oriented:

- the currently served EngCoder runtime health strip;
- additional session instructions;
- temperature and maximum output tokens;
- response streaming toggle;
- history inclusion and maximum history turns;
- voice controls;
- Run info/timeline display controls;
- appearance/theme/message/font controls;
- clear/copy chat utilities.

There is no Current/Local/Custom runtime selector. The page always calls the same EngCoder runtime that served it. Session instructions are lower priority than the native EngCoder runtime contract and cannot bypass deterministic tool/policy enforcement.

## EngCoder Lab

Engineering/model controls live in a separate `EngCoder Lab` dialog with five panels:

- **Runtime:** local serving status, trusted-LAN **Serve remotely**, external model-provider health and process-local API token.
- **Models:** Native Model Library plus KeyMaker Foundation, Architect, capability addon assignments and routing controls. Qwen Flash cards expose Download/Load/Configure/Use as KeyMaker/Unload/Clear cache. The configuration form is pre-populated with the recommended 81,920/1-slot RTX 5080 profile, exposes Recommended / Automatic / Manual tuning (GPU offload default 50% of the configured safe layer budget; explicit layer overrides cannot bypass the cap), and shows effective context per slot including a below-75K warning.
- **GPU:** physical/logical residency, pinned core activation, and idle-addon release.
- **Training:** model initialization, training target/profile, checkpoint cadence, pause/resume/cancel.
- **Learning:** verified evidence and immediate retrieval-memory status.

This separation prevents model/training management from consuming the ordinary Web UI while preserving complete access to EngCoder's local-AI functionality.

## Themes

The shared Desktop/Web surface provides six matching presets:

- EngCoder Dark
- EngCoder Light
- Windows System
- Windows Dark
- Windows Light
- Matrix Green

Windows System follows the live OS/browser light/dark preference. Theme and UI settings persist in the dedicated EngCoder desktop browser profile and in normal Web local storage.

## Diagnostic native fallback

The ImGui fallback exposes the same six theme choices for diagnostics. On Windows it prefers Segoe UI Variable for UI text and Cascadia Code/Mono for telemetry, uses SDL high-pixel-density rendering, opts into Per-Monitor V2 DPI awareness before window creation, and recalculates scaling when the window moves between mixed-DPI/4K displays.

Feature parity is contractual rather than pixel/implementation identity: the served BotFace frontend and SDL/ImGui client call the same native model-library/runtime lifecycle semantics, while each frontend presents those controls in the UI structures appropriate to its implementation.

## Responsibility boundary

JavaScript calls same-origin EngCoder native `/api/*` endpoints only. It does not own a second agent loop, model credentials, side effects, training policy, or model residency. Architect and KeyMaker remain native-runtime concepts even though the shared frontend controls them.


## Native Model Library controls

The Model Library is an EngCoder runtime surface, not an LM Studio configuration screen. Qwen Flash profiles expose context/slot/compaction, CPU/batch scheduling, expert-cache, pinned-host, KV placement/types, mmap/lazy, FlashAttention/workspace, reasoning-history, backend context-shift, and optional MTP controls. Recommended values are populated on first open and when creating a profile.

Automatic compaction is visible in Run info through `context.policy` and `context.compacted` events. It preserves the system contract and current working set while exact older session history remains searchable from native persistence.

### Composer quick controls (0.6.5 Epoch integration pass)

The existing chat composer layout remains unchanged. A single compact quick bar below the composer exposes routing mode, the active KeyMaker, history, Runtime/GPU, and Session Settings without creating a duplicate control stack. The `+` menu reaches the same existing settings/runtime surfaces. Changes synchronize with the same underlying settings used by `/api/tasks`; the controls do not create a separate chat implementation. Qwen's GPU control is explicitly a **model-layer offload target**: the selected percentage is converted to the effective llama.cpp `-ngl` value on every load and the active effective profile reports that value; expert/KV/workspace VRAM remains separate.

## Main composer Missions (0.6.6 maintenance)

The bottom composer quick bar has a compact **Missions** item, leaving Send, Stop, mic and attachment controls in their original positions. Open it to review/search outstanding P0–P2 engineering objectives carried forward from older EngCoder revisions. The list is read-only: source changes and acceptance evidence, not user clicks, determine completion.

`docs/FUTURE_MISSIONS.md` is the canonical register. After editing it, run `python scripts/sync_missions.py` to regenerate `projects/web/js/mission_data.js`. CI/release checks should run the same command with `--check`.

## 0.6.9 — model setup and routing

See [SUBAGENTS_QUICKSTART.md](SUBAGENTS_QUICKSTART.md). Served and desktop UI share the same server-side model routing and theme palette. Use the **Assign small + middle** action to configure only the two external roles, without replacing KeyMaker. An unassigned skill addon can reuse the configured small responder for a constrained checklist; Run info differentiates successful specialist/reviewer stages from failures. **Output** remains generated model text only; Engine routing telemetry stays out of it. The Qwen GPU control shows its current safe model-layer budget, not historical remapping.
