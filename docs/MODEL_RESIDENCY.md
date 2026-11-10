# EngCoder model residency

EngCoder targets consumer GPUs with more system RAM than VRAM.

## Memory hierarchy

```text
NVMe artifact store
      |
      v
CPU RAM warm generation
      |
      v
CUDA VRAM hot set
  pinned: KeyMaker Foundation + Architect
  pageable: capability addons
```

## Initial load

The internal worker deserializes SafeTensors into CPU RAM. With lazy addon residency and CUDA available, shared transformer modules plus `keymaker-foundation` and `architect-controller` are moved to CUDA. Other addon FFNs stay in CPU RAM. EngCoder does not intentionally upload every addon to VRAM and then move cold addons back out.

## Addon activation and eviction

`LoadedInternalModel.activate_lane()` resolves a logical capability to learned lanes, protects lanes required by the current request, checks live CUDA allocation against the configured byte budget, evicts non-required least-recently-used addons when needed, and moves the requested addon weights to CUDA.

KeyMaker Foundation and Architect are normal pinned residents and are not addon-LRU eviction victims. `release_idle_addons()` returns every non-pinned hot addon to CPU RAM while retaining the warm generation.

## Defaults

- `ENGCODER_VRAM_BUDGET_GIB=13.5`
- `ENGCODER_MAX_RESIDENT_ADDONS=3`

The default leaves headroom on a 16 GiB GPU for KV cache, activations, CUDA/runtime allocation, and foreground work. Training has separate transient requirements and cooperatively yields to foreground inference.

## External-provider recovery

External provider models are not controlled by EngCoder's internal CUDA residency manager. When LM Studio exposes native model-management state, EngCoder checks the selected model immediately before inference. If the model is installed but unloaded, EngCoder reloads it automatically before sending the request. Disable this only with `ENGCODER_MODEL_AUTO_LOAD=0`.

## Observability

- `GET /api/engcoder/residency`
- `GET /api/engcoder/residency?load=1`
- `POST /api/engcoder/residency/activate`
- `POST /api/engcoder/residency/release-addons`

The UI shows device, VRAM budget, CUDA allocated/reserved/total memory, hot addons, and per-component placement/pinning/size.

## External model residency note (0.6.9)

Native EngCoder residency and its Qwen GPU-layer controls do **not** reserve or govern RAM/VRAM used by LM Studio. Before loading three models, leave headroom and monitor resource use in the external host. External small/middle models are invoked through their provider and not automatically loaded into EngCoder's native Qwen runtime.
