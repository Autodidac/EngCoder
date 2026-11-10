# EngCoder architecture

This document records the original 0.6.5 internals and their evolution through 0.6.9. See [SUBAGENTS_QUICKSTART.md](SUBAGENTS_QUICKSTART.md) for the current operational three-tier runtime; see FUTURE_MISSIONS.md for validation limits.

## Product composition

EngCoder separates orchestration, learned language capability, domain capability, deterministic execution, memory, and training:

```text
request
  |
  v
Architect
  |  route / retrieve / choose capability / verify / stop
  v
KeyMaker Foundation + optional KeyMaker addon
  |
  +--> C++23 / docs / planning / tool-use / reranking addons
  +--> deterministic native tools
  |       `--> policy / permissions / checkpoints / verification
  v
response
```

### Architect

Architect is the always-available low-latency controller/harness. It is optimized for structured decisions rather than user-facing prose. It decides whether retrieval or a capability addon is useful, controls escalation and stopping, and emits structured telemetry such as `addon`, `retrieve`, `confidence`, and `reason`.

When no trained Architect component is inference-ready, EngCoder uses the deterministic trigger/policy path rather than inventing a learned result.

### KeyMaker

KeyMaker is EngCoder's local learned-model and training family. The resident **KeyMaker Foundation** supplies general English language/reasoning and produces the normal user-facing answer. Capability-specific weights attach as KeyMaker addons instead of duplicating a complete chat model for every domain.

Current learned addon roles are:

- `cpp` -> `cpp23-core` + `cpp23-debugging`
- `docs` -> `documentation`
- `planner` -> `planning`
- `tools` -> `tool-use`
- `reranker` -> `reranking`

Internal provider IDs are `engcoder://keymaker`, `engcoder://architect`, `engcoder://cpp`, `engcoder://docs`, `engcoder://planning`, `engcoder://tools`, and `engcoder://reranker`.

## Deterministic runtime boundary

Learned components may recommend actions but never own side effects. Native C++ remains authoritative for workspace containment, approval rules, checkpointing, process execution, tool policy, persistence, cancellation, verification, and model residency/lifecycle management.

For HTTP/Web tasks, a policy result of `require_approval` suspends the exact immutable tool call inside `AgentRuntime` instead of terminating the task. `TaskScheduler` keeps the task active while the shared Web/Desktop client shows the tool, arguments, working directory where applicable, and policy reason. A one-shot approval resumes only that matching call ID; denial becomes a normal tool-denied observation so KeyMaker can continue with a safer plan. Direct diagnostic clients that do not opt into interactive approval retain the terminal `awaiting_approval` behavior rather than blocking indefinitely.

## Process topology

`EngCoderCLI` embeds `RuntimeHost`. `EngCoderServer` hosts the same runtime headlessly. On Windows, `EngCoder.exe` owns `RuntimeHost` and one HTTP server, then opens the same `projects/web` page used by browser mode in an isolated app-mode Edge profile. The embedded server defaults to loopback port `14321`. The desktop page registers an ephemeral per-launch lifecycle token; closing that app-mode page signals the native owner, which stops the HTTP server and tears down the dedicated Edge process tree after a short reload-safe grace period. The page always uses same-origin APIs. Local access is always available; **Serve remotely** optionally admits trusted-LAN clients to that same runtime rather than creating a second server or frontend.

```text
EngCoder.exe
  +-- RuntimeHost
  |    +-- Architect
  |    +-- KeyMaker Foundation
  |    +-- AddonRegistry / ModelRegistry
  |    +-- ModelResidencyManager / InternalModelHost
  |    +-- LearningService / TrainingService
  |    +-- tools / policy / persistence
  +-- HTTP server (local by default; trusted-LAN opt-in)
       `-- shared EngCoder Web UI + same-origin API

EngCoderCLI.exe    -> RuntimeHost directly
EngCoderServer.exe -> RuntimeHost + HTTP adapter
```

The native ImGui interface remains available through `--native-ui` as a diagnostic fallback only. Remote serving is disabled by default; the exposure toggle and provider-credential mutation endpoints accept changes only from loopback clients.

## Internal and external model routing

`MultiplexProvider` routes `engcoder://...` identifiers into the internal KeyMaker host. Other model identifiers use the configured external provider. External models can be fallbacks, teachers, or verifiers but do not become KeyMaker weights.

For LM Studio, EngCoder can detect when a selected installed model was unloaded and explicitly reload it before inference when the provider exposes its native model-management API. This recovery is controlled by `ENGCODER_MODEL_AUTO_LOAD` and defaults to enabled.

## Model registry and ABI

`ModelRegistry` projects an inference-ready generation into logical components: Architect, KeyMaker Foundation, addon, or auxiliary. Components record generation, provider ID, ABI/family, checksum, parameter count, estimated bytes, pinning, trainability, and active/readiness state.

Scratch generations promote atomically for safe rollback while runtime routing and GPU residency operate at component/lane granularity. See `MODEL_ABI.md`.


## Native downloadable model runtimes

Native runtime identity changes inference/residency implementation, not agent semantics. When a `qwenflash://` profile is selected as Foundation, `AgentRuntime` still performs Architect routing, memory retrieval, addon consultation, tool/MCP execution, anti-looping, verification, and session persistence. Before each Qwen Flash turn it also applies that profile's effective per-slot context policy and deterministic automatic compaction. There is no separate Qwen agent loop.


EngCoder has a model-management layer between model identity and inference transport. Internal KeyMaker generations, native downloadable runtimes, and external providers remain distinct:

```text
                         Architect / AgentRuntime / tools
                                      |
                               MultiplexProvider
                     +----------------+----------------+
                     |                |                |
              engcoder://       qwenflash://     provider model id
                     |                |                |
             InternalModelHost  NativeModelManager  LM Studio/Bionic
                                      |
                               QwenFlashRuntime
                                      |
                    mmap/lazy GGUF + pinned expert store + GPU LRU
```

`NativeModelManager` owns catalog, download/install state, load/unload, and cache management. Runtime drivers own only backend-specific lifecycle and hardware policy. The OpenAI-compatible protocol used by the Qwen Flash backend is an internal wire format, not a provider classification. See `MODEL_LIBRARY.md`.

## Residency

Internal inference follows NVMe artifact storage -> system-RAM warm state -> CUDA VRAM hot set. KeyMaker Foundation and Architect are pinned while capability addons move to CUDA on demand and use a bounded usage/LRU eviction policy. See `MODEL_RESIDENCY.md`.

## Learning and training

Verified interactions enter durable retrieval memory immediately but do not mutate active weights. Eligible evidence is admitted to candidate training; candidates are evaluated and atomically promoted or rejected. Foreground inference preempts background training at safe checkpoint boundaries and the interrupted job resumes afterward. See `LEARNING_POLICY.md` and `TRAINING.md`.

## Source self-iteration

The source iteration loop lives inside the existing RuntimeHost task flow. KeyMaker inspects and proposes changes in an operator-designated candidate. Native policy binds edit approval to the current call and checkpoints the target. The host owns CMake and CTest execution, records their exit results, and feeds bounded failure evidence back to KeyMaker for a targeted repair. A passing candidate stays in its checkout for operator review. See [candidate self-iteration](SELF_ITERATION.md).

## Current three-model runtime (0.6.9)

Automatic System 1 Architect selection and deterministic fallback remain the task ingress. The optional small external model can answer safe/simple text, or generate a bounded, untrusted checklist for a configured specialist. A separate optional medium external model reviews the checklist, before KeyMaker performs final complex reasoning/tool work. Math and sensitive cases bypass the small/middle advisory path. The stages execute sequentially with stop tokens and deadlines; this is **not** recursive independent subagent execution or an operating-system sandbox. A model-based controller proposal is distinguishable from the final responder model. Operational setup: [SUBAGENTS_QUICKSTART.md](SUBAGENTS_QUICKSTART.md).
