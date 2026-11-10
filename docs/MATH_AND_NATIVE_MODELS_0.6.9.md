# EngCoder 0.6.9: math rendering and model-host roadmap

## Math output in chat

**Maintenance correction:** The native HTTP server must explicitly register `math.js`; without `/ui/js/math.js` the parser is never loaded and the chat silently displays LaTeX delimiters. Served and desktop clients share this route. UI responses now use `Cache-Control: no-store` to prevent stale scripts after upgrades. The `served_static_assets_smoke.cjs` test covers the complete startup script list.

- Desktop and served Web UI use the same `projects/web` renderer.
- Chat messages now render a safe, offline MathML subset of TeX without a CDN or JavaScript dependencies. `\(...\)`, `\[...\]`, `$...$`, and `$$...$$` are recognized. Fractions, superscripts, subscripts, square roots, boxed results, modular notation, and common operators are supported.
- Fenced code blocks remain literal. Raw Output remains **the actual unmodified generated text** (including original TeX), as requested. Saved conversations and Copy retain the source text.
- Unrecognized commands are displayed literally. This is a deliberately small renderer, **not full KaTeX compatibility**. Complex equation layout must be certified in Windows Edge across themes/zoom, and a richer trusted renderer may be added later.
- Do not use `innerHTML` to render model-supplied math; MathML nodes and text nodes are constructed from DOM APIs. No code execution or external image requests are introduced by math parsing.

## What EngCoder can host today

EngCoder already owns a **native model library**:

1. Open **EngCoder Lab → Models → Model Library** on the host PC.
2. To download weights, enter an actual Hugging Face repository and its GGUF quant/variant. Save the model profile. Choose **Download model weights** on the card, then **Load** after the download is complete.
3. For an existing GGUF, enter an absolute file path on **the machine running the native EngCoder server**, not the browser PC. For split GGUF weights select shard `00001-of-N`. Save the profile, then Load.
4. Use model cards to inspect cache status and unload/reload. Removing temporary cache is separate from explicitly deleting downloaded weights.

**Current limit:** The built-in native runtime holds one active GGUF at a time. It has Qwen3.8-specific optimizations and the existing GPU/offload cap; arbitrary GGUF/model architectures and MTP pairings are not automatically qualified. Do not expose incompatible controls or silently adopt the foundation GPU budget for other models.

**Three loaded models in the current version:** Qwen Foundation is native. The 9B and 27B can be simultaneously available through LM Studio's **OpenAI-compatible HTTP endpoint**, not MCP. LM Studio downloads/serves those models; EngCoder selects them as small responder / middle reviewer / Architect and orchestrates their advisory handoff. Assignments are stored per browser profile/session settings; a distinct served browser profile may need its own role selections.

**Served mode:** The Web UI is served by EngCoder. It talks only to the native EngCoder server. A remote browser must not need a local LM Studio connection. Current native model installation/loading endpoints are loopback-only security operations; use the EngCoder host's local UI for these actions until authenticated remote model administration is separately verified.

## Next infrastructure mission: EngCoder-owned three-model host pool

The registered mission in `docs/FUTURE_MISSIONS.md` describes the required implementation. Complete it before claiming simultaneous native hosting. Required engineering gates:

- Independently owned llama.cpp server processes, per-model loopback ports, health checks, cancellation, timeouts and child process containment.
- Persistent model registry with safe import/download and compatible per-model runtime options. Treat model size, architecture, tokenizer and supported flags as inspected capabilities, not assumptions from display names.
- RAM/VRAM budget including Qwen's large CPU-resident weights and the 9B/27B sidecars; never auto-load all three with no memory preflight.
- Provider routing by stable native URI across server, desktop, and served Web UI. Models do not gain tool authority by being hosted locally.
- No automatic sidecar replacement of KeyMaker, and no cross-server MTP claim.
- Full Windows Debug/Release build, CTest and real GPU memory/three-model integration tests.

Until those gates pass, retain the external-HTTP small and middle model path rather than breaking Qwen's working runtime.
