# EngCoder model/component ABI

The model ABI lets Architect, KeyMaker Foundation, and KeyMaker addons evolve without turning EngCoder into a hard-coded monolith.

## Logical components

Every component records a stable ID, kind (`controller`, `foundation`, `addon`, or `auxiliary`), generation, provider ID, foundation family/ABI, weights checksum, parameter count/estimated bytes, and active/enabled/pinned/trainable state.

Canonical logical IDs are `architect`, `keymaker`, `cpp`, `docs`, `planner`, `tools`, and `reranker`.

## Current internal V2 generation

```text
<data-root>/models/internal/
  active.json
  generations/<generation>/
    manifest.json
    config.json
    weights.safetensors
    metrics.json
    components/
      architect-controller.safetensors
      cpp23-core.safetensors
      cpp23-debugging.safetensors
      documentation.safetensors
      planning.safetensors
      tool-use.safetensors
      reranking.safetensors
```

The complete generation contains shared KeyMaker transformer/foundation weights plus named learned lanes. Promotion remains atomic at generation granularity. The writer also publishes checksummed component SafeTensors sidecars for non-foundation learned lanes, while runtime routing and GPU paging operate at component/lane granularity.

Atomic generation promotion deliberately prevents an Architect/addon trained against one foundation generation from silently attaching to an incompatible one. A future storage migration can make component sidecars the primary loading source without changing the registry contract.

## Compatibility and readiness

A component is usable only when its foundation family/ABI matches the active generation contract, its checksums/format are accepted, and the generation is explicitly inference-ready. Merely creating random initial weights does not make a generation selectable for inference.

Older EngCoder internal-generation formats can be read at the private persistence boundary when their manifest contains compatible capabilities. Compatibility identifiers are not surfaced as current product terminology.

## Provider identifiers

```text
engcoder://keymaker
engcoder://architect
engcoder://cpp
engcoder://docs
engcoder://planning
engcoder://tools
engcoder://reranker
```

These identifiers describe roles/lanes. They do not imply seven duplicated full language models.
