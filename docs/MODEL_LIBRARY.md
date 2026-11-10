# EngCoder Model Library

EngCoder 0.6 adds a first-class native model-library layer. It is deliberately separate from both EngCoder's internally trained KeyMaker generations and external model applications such as LM Studio or Bionic.

## Model classes

| Class | Identifier | Ownership |
| --- | --- | --- |
| Internal KeyMaker | `engcoder://...` | EngCoder training + residency |
| Native runtime | `qwenflash://...` | EngCoder model library + runtime lifecycle |
| External provider | provider model ID | LM Studio/Bionic/other OpenAI-compatible provider |

A native runtime may expose an OpenAI-compatible HTTP endpoint internally, but that is only its wire protocol. It does not become an external provider and does not inherit LM Studio/Bionic process or model-lifecycle state.

## Qwen Flash as a native System 1 foundation

`qwen-flash` is EngCoder's first native downloadable runtime type. A Qwen Flash profile can be selected as the KeyMaker/System 1 Foundation and then uses the same native EngCoder harness as other foundation models:

```text
Architect routing
      |
      v
System 1 Foundation (internal KeyMaker or qwenflash://...)
      |
      +-- addon routing
      +-- deterministic tools / MCP
      +-- episodic memory retrieval
      +-- workspace/checkpoint safety
      +-- anti-looping / verification
      +-- automatic context compaction
```

Qwen-specific inference mechanics stay below that boundary. They do not create a second agent loop.

## Qwen Flash runtime policy

The runtime is built from the pinned MoE-cache llama.cpp revision in `runtime/qwen-flash/backend.lock`.

```text
GGUF on disk / mmap
        -> lazy system-memory pages
        -> host-pinned cold expert store
        -> bounded GPU hot-expert cache
```

Recommended RTX 5080 16 GB / 64 GB RAM profile:

| Setting | Recommended |
| --- | ---: |
| Runtime policy | Recommended (proven QFR launch) |
| Total context | 81,920 |
| Parallel slots | 1 |
| Effective context / slot | 81,920 |
| Output/tool reserve | 4,096 |
| Auto-compaction trigger | 97% of usable window |
| Auto-compaction target | 72% of usable window |
| CPU threads / batch threads | 12 / 12 |
| Batch / micro-batch | 1024 / 256 |
| GPU layers | llama.cpp `auto` |
| GPU MoE expert cache | 6,144 MiB |
| Pinned host expert budget | 12,288 MiB |
| KV cache types | q4_0 / q4_0 |
| KV GPU layers | 0 |
| Pinned CPU KV buffers | on |
| FlashAttention | on |
| Phase-aware workspace | on |
| Load / lazy mode | mmap / on |
| Tensor override | `per_layer_token_embd.weight=CPU` |
| Early router | off |
| MTP/speculative decoding | off until explicitly configured |

MTP requires a **dedicated draft/sidecar model**. The draft repository/path must never point back at the main Qwen model. Legacy 0.6.5 profiles that accidentally stored the main repository as the draft are migrated with MTP disabled before the runtime can be restarted.

### Recommended / Automatic / Manual

- **Recommended** keeps the standalone QwenFlashRuntime scheduling/KV/cache baseline proven on the target 9900X / RTX 5080 / 64 GiB machine, but EngCoder's GPU layer-offload target is always honored instead of silently reverting to `-ngl auto`.
- **Automatic** probes system RAM, NVIDIA VRAM and logical CPU count at launch, then derives threads, batch sizes, expert-cache budgets and KV placement with a safety margin.
- **GPU layer offload %** is a 0–100% control with the same existing capped model-layer budget across Recommended, Automatic, and Manual. **New/default/reset value: 50%**. For the current 48-layer hint, 50% resolves to `-ngl 14`; the maximum remains `-ngl 29`. Exact `gpu_layers` overrides, including `all`, obey the same cap. Changing the percentage resets an exact layer override to `auto`. Existing saved profiles are preserved (including 99%); only creating/resetting a profile applies 50%. The control is not a VRAM-usage slider: cache, KV and workspace consume memory separately.
- **Manual** exposes every remaining Qwen Flash control directly.

The 81,920-token preset is intentional: after the 4,096-token reserve, 97% compaction begins at roughly 75.5K active tokens, keeping the working conversation above the requested ~75K floor before EngCoder compacts older state. Presets include 81,920, 98,304, 131,072, 196,608 and 262,144.

Increasing parallel slots divides the total server context between slots. EngCoder displays the effective per-slot context before launch and warns in the UI when it falls below 75K.

### Startup recovery

Recommended and Automatic first launch the requested profile unchanged. If the child process exits before `/health` becomes ready, EngCoder isolates optional backend features before sacrificing context:

1. same-context stable baseline with expert cache disabled when the failed attempt used it;
2. same-context minimal llama.cpp compatibility launch;
3. 77,824-token hardware-aware recovery profile;
4. 65,536-token diagnostic profile;
5. 32,768-token compatibility profile using the original proven QFR policy.

The recovered context/slot count becomes the active context reported to the agent harness. Manual mode is never silently retuned. Windows `0xC0000005` access violations are decoded explicitly in the retained startup error.

## Automatic context compaction

EngCoder owns explicit, observable context compaction at the agent layer. Recommended mode leaves llama.cpp's backend context-shift switch at its proven QFR default instead of forcing a new backend policy; Automatic/Manual can control it explicitly. EngCoder's own compaction remains the primary conversation-management policy and is reported in runtime telemetry.

Before every foundation-model turn, `AgentRuntime` budgets:

1. the model profile's effective per-slot context;
2. output reserve;
3. serialized tool definitions;
4. the current system/agent contract and active messages.

When the configured trigger is crossed, `ContextManager` keeps the root system contract and newest working set, converts older transcript/tool state into a bounded continuation summary, and leaves the exact full transcript persisted in the native session store for `session.search`. A `context.compacted` runtime event records before/after estimates and the number of compacted messages.

Compaction is deterministic orchestration state; it is not exposed as hidden model reasoning.

## Model profiles and download

Native manifests are stored under the EngCoder state tree at `models/runtimes/<id>/manifest.json` using `engcoder.runtime-model.v3`. A Qwen Flash profile can point to:

- a Hugging Face repository plus quant/variant; or
- an existing local GGUF path.

`Local GGUF path` is also an immediate load-time override. Entering a path and pressing **Load** bypasses the Hugging Face/cache source for that load, matching the original QwenFlashRuntime `--model` behavior. Saving the profile persists the path; saving is not required for a one-off override. Quoted Windows paths are accepted, split-model shard 2/3 paths are normalized back to shard 1, and the loader verifies that every split shard exists and is non-empty before launching. `mmproj-*.gguf` is rejected as a model override because it is the vision projector, not the model weights.

The built-in profile is populated rather than blank:

```text
Repository: unsloth/Qwen3.8-Flash-Next-GGUF
Quant:      UD-Q2_K_XL
Runtime:    qwen-flash
Context:    81,920
Slots:      1
```

Before downloading, EngCoder probes its configured cache plus standard Hugging Face environment/user caches and legacy Qwen/llama cache roots. If a complete matching GGUF already exists, EngCoder launches that first shard directly and does not redownload it.

Hugging Face downloads are performed by the native llama.cpp runtime using its sharded GGUF downloader. EngCoder controls `LLAMA_CACHE`, observes cache state, and exposes Download/Load/Unload/Use as KeyMaker/Clear cache actions through EngCoder Lab. Browser JavaScript never downloads model weights itself. Once every shard for the selected quant is present, EngCoder launches shard `00001` directly with `-m`; `-hf` remains the download/resolution path for incomplete caches.

Cache selection:

1. `ENGCODER_MODEL_CACHE_ROOT` when explicitly configured;
2. `LLAMA_CACHE`, `HF_HUB_CACHE`, `HUGGINGFACE_HUB_CACHE`, `HF_HOME`, or `XDG_CACHE_HOME` when present;
3. an already-existing standard `~/.cache/huggingface/hub` cache;
4. EngCoder's own `<data-root>/model-cache`.

This allows EngCoder to reuse the already-downloaded Qwen Flash weights without copying ~79 GB when the same cache is explicitly/shared by the user.

## Lifecycle separation

`NativeModelManager` owns catalog, registration, download/install state, load/unload, cache removal, and runtime-type metadata. `QwenFlashRuntime` owns only backend discovery/process lifecycle and sparse/MoE hardware policy. `MultiplexProvider` routes `qwenflash://` requests to the native runtime after EngCoder owns it; LM Studio/Bionic routes remain unchanged.

This separation is the extension contract: adding another native runtime should require a new runtime driver and catalog metadata, not changes to Architect, tools, memory, or external-provider code.

## Native runtime provisioning

Weights and inference runtime are separate. If Qwen Flash runtime binaries are absent, **EngCoder Lab > Models > Install Qwen Runtime Backend** starts a background build of the pinned backend. Platform installers stage only runtime artifacts into EngCoder state at `runtimes/qwen-flash/bin`.

The normal EngCoder build does not compile CUDA/llama.cpp. This keeps ordinary internal-model and LM Studio/Bionic workflows independent. The runtime installer uses the same pinned vcpkg baseline/toolchain and official verification-only `cuda` manifest dependency as the proven standalone QFR build. On Windows an explicit runtime-install action can provision NVIDIA CUDA Toolkit through WinGet when missing; on Linux CUDA must already exist.

## Migration from standalone QwenFlashRuntime

If the proven standalone QwenFlashRuntime backend cache exists, EngCoder resolves the exact pinned `build-167742d9dfa0/bin/Release` runtime, copies that known-good runtime directory into EngCoder's state tree, and records it as the pinned compatibility baseline. The EngCoder-owned copy is preferred on later launches; the original standalone QFR tree is never moved, modified, or deleted. The compatibility baseline remains distinct from the current patched EngCoder runtime recipe, so the UI can still offer a runtime upgrade.

A clean machine can instead set `ENGCODER_QWEN_FLASH_SERVER` to a compatible pinned backend build.

## External provider and native model roles

LM Studio weights are managed in LM Studio, not in EngCoder's native Model Library. EngCoder's external endpoint defaults to `http://127.0.0.1:1234` on the **EngCoder host** and can be overridden by `ENGCODER_MODEL_SERVER_URL`. The small responder and middle reviewer are role assignments to two *different* discovered external models. The internal Qwen Foundation is not modified by **Assign small + middle**. The Web UI may be served from a separate computer, but model inference and routing are still performed by the native host. See [SUBAGENTS_QUICKSTART.md](SUBAGENTS_QUICKSTART.md). The Qwen GPU slider is 0–100% of its configured safe *layer* budget. Historical percentage migrations above describe upgrades only; the UI no longer shows those old scales.
