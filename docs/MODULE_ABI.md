# Internal Model and Skill ABI

## Internal generation layout

```text
models/internal/
├── active.json
├── generations/
│   └── gNNNNNN/
│       ├── config.json
│       ├── weights.safetensors
│       ├── manifest.json
│       └── metrics.json        # trained candidates
└── checkpoints/
    └── <parent>-<scope>/
        └── latest.pt
```

## Manifest format

Current format identifier:

```text
threadspire.internal.v1
```

Important fields:

```json
{
  "format": "threadspire.internal.v1",
  "origin": "random-initialized-local-model",
  "external_base_weights": false,
  "generation": "g000001",
  "parent": "g000000",
  "status": "trained",
  "parameter_count": 22527360,
  "weights_file": "weights.safetensors",
  "weights_sha256": "...",
  "config_file": "config.json",
  "steps": 1200,
  "tokens_seen": 0,
  "validation_loss": 0.0,
  "training_scope": "all",
  "skills": [
    "english-core",
    "cpp23-core",
    "cpp23-debugging",
    "planning"
  ]
}
```

The loader rejects an unsupported format, missing local file, invalid generation name, missing config, or checksum mismatch.

## Neural compatibility

Version 1 fixes these contracts for one generation:

- vocabulary size and special-token IDs;
- byte-token offset;
- context length;
- hidden width;
- layer and head counts;
- shared FFN width;
- expert width;
- ordered skill names;
- tensor names and shapes.

A future generation with a different architecture must declare a new compatible family or use an explicit migration/distillation candidate. It must not be loaded silently under the old ABI.

## Skill experts

Each transformer layer contains one residual full-weight expert per declared skill. A scoped update may train one named expert across every layer while shared attention, embeddings, and shared FFNs remain frozen.

Version 1 stores the complete generation in one SafeTensors file. This makes promotion and rollback simple and checksum-complete. A later format may store each expert independently with a shared-core hash; that change requires a new manifest version.

## Active pointer

`active.json` contains the promoted generation only. Publication uses a temporary file and atomic replacement. Historical generations remain unchanged for rollback and forensic evaluation.

## Checkpoint format

`latest.pt` is training state, not an inference artifact. It contains candidate weights, optimizer state, dataset order/cursor, counters, and random states. It is tied to one parent generation and scope. A mismatched parent or scope is ignored rather than resumed incorrectly.

## External-model skill delta ABI

The optional external LoRA/FFN workflow retains its separate legacy format:

```text
threadspire.external-skill-delta.v1
```

Those modules declare an external base-model identifier and fingerprint. They cannot be loaded as `threadspire.internal.v1` generations and do not satisfy `external_base_weights: false`.
