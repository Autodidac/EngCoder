# ThreadSpire Architecture

## 1. Product invariant

ThreadSpire's serving intelligence remains stable while learning occurs beside it.

The internal model is not an adapter over an imported checkpoint. It is a custom decoder transformer whose active weights were initialized and trained locally. Imported local LLMs are optional tools for data generation, verification, routing experiments, and external LoRA workflows.

## 2. Runtime topology

```text
ThreadSpire.exe — C++23
│
├── Win32 GUI and source selection
├── Assistant and Coding Agent
├── deterministic task analysis and routing
├── episodic/retrieval memory
├── evidence and candidate registries
├── compiler/test evaluator
├── resource governor
├── std::jthread continual-learning coordinator
├── MCP and connected-local-LLM clients
└── supervised private PyTorch worker
    ├── ThreadSpire internal transformer
    ├── internal streaming generation
    ├── full-weight shadow-candidate training
    ├── modular skill-expert training
    ├── exact-resume checkpoints
    ├── evaluation and generation publication
    └── optional external-model LoRA tools
```

C++ owns application policy, persistence, routing, process lifetime, and promotion decisions. Python/PyTorch owns tensor execution. The process boundary keeps CUDA and package failures outside the GUI process.

## 3. Internal neural architecture

### 3.1 Token ABI

The base tokenizer is deterministic UTF-8 bytes:

```text
special structural tokens: 0–15
UTF-8 byte values:          16–271
vocabulary size:            272
```

This removes any dependency on a downloaded vocabulary and can represent every Unicode string and source file. The tradeoff is longer sequences than a mature subword tokenizer. Learned token merging can be added later without invalidating the byte ABI.

### 3.2 Core block

Each decoder block contains:

```text
RMSNorm
→ causal multi-head self-attention with RoPE
→ residual
→ RMSNorm
→ shared SwiGLU FFN
→ residual
→ RMSNorm
→ selected residual skill experts
→ residual
```

Input embeddings and output logits use tied weights. Generation uses a KV cache and PyTorch scaled-dot-product attention.

### 3.3 Modular skill experts

Every layer owns independently addressable residual SwiGLU experts:

- `english-core`
- `cpp23-core`
- `cpp23-debugging`
- `planning`

A lane selects the minimum useful set:

```text
fast     -> english-core
coder    -> english-core + cpp23-core + cpp23-debugging
thinker  -> english-core + planning + cpp23-core
reviewer -> english-core + cpp23-debugging
```

After bootstrap, continual updates can freeze the shared core and train one expert plus final normalization. This is full-weight expert training, not LoRA. The active generation remains immutable while the candidate is trained.

## 4. Local initialization and generations

`Create random model` uses a deterministic seed and creates `g000000` with:

```json
{
  "origin": "random-initialized-local-model",
  "status": "initialized-untrained",
  "external_base_weights": false
}
```

An untrained generation cannot serve requests. Bootstrap training creates a child candidate. If evaluation succeeds, `active.json` atomically points to the promoted trained generation.

```text
models/internal/
├── active.json
├── generations/
│   ├── g000000/
│   │   ├── config.json
│   │   ├── weights.safetensors
│   │   └── manifest.json
│   └── g000001/
│       ├── config.json
│       ├── weights.safetensors
│       ├── manifest.json
│       └── metrics.json
└── checkpoints/
```

Every load verifies the SafeTensors SHA-256 recorded in the manifest.

## 5. Candidate learning

### 5.1 Evidence classes

| Evidence | Initial destination |
|---|---|
| Current path, branch, transient tool state | context/retrieval |
| User-authored durable rule | immediate memory |
| Explicit correction | negative prior answer plus authoritative correction |
| Accepted answer | retrievable experience; weight-eligible under policy |
| Compiler/test-confirmed code | deterministic positive evidence |
| Unverified teacher output | quarantine |
| Repeated verified skill behavior | internal skill candidate |

The model cannot mark its own prose correct and train on it automatically.

### 5.2 Shadow update

1. Resolve the active parent generation.
2. Load its complete internal weights.
3. Select `all` for bootstrap or a named skill expert for continual learning.
4. Mix bootstrap replay with verified episodes.
5. Train a shadow model.
6. Save exact-resume state periodically.
7. Evaluate disjoint anchors.
8. Write a new checksum-verified generation.
9. Promote only when the candidate passes and its parent is still active.

The active files are never modified in place.

### 5.3 Exact resume

A checkpoint contains:

- complete candidate model state;
- optimizer state;
- pending parameter gradients and accumulation phase;
- completed step count;
- verified target-token count;
- shuffled record order and cursor;
- Python random state;
- CPU PyTorch random state;
- CUDA random state for every device.

After a crash or foreground preemption, training resumes from that state instead of silently restarting the candidate.

## 6. Resource priority

Inference owns priority.

```text
foreground request
→ create inference lease
→ candidate notices lease at a safe boundary
→ save checkpoint
→ exit with preemption code 75
→ release training tensors
→ serve request from active generation
→ requeue candidate
→ resume from checkpoint when idle
```

The C++ resource governor also delays work when CPU, RAM, power, or interactive activity make training inappropriate. The worker can later be placed on another GPU or remote worker because the candidate protocol is process-independent.

## 7. Connected local LLMs

Connected local LLMs and the internal model are separate sources.

A connected model can provide:

- immediate bridge inference when explicitly selected;
- teacher variations for bootstrap expansion;
- independent verification;
- routing comparisons;
- optional LoRA targets in Model Lab.

It cannot silently become the internal source. Its generated output is data with provenance, not imported inference weights.

## 8. Evaluation

Initial internal promotion uses held-out loss because the model begins from random weights. Code-specialist promotion should additionally require executable benchmarks:

- MSVC C++23 compilation;
- public and hidden tests;
- sanitizer/static-analysis results where available;
- requirement conformance;
- regression anchors;
- latency and memory budgets.

Loss improvement alone is not sufficient evidence of mature coding ability. The generation and candidate boundaries allow stronger evaluators to replace the initial evaluator without changing storage or rollback semantics.

## 9. Optional external LoRA path

External-model LoRA support remains available but is not part of internal inference. It uses the existing Transformers/PEFT path and can train adapters against an explicitly selected external model. Those artifacts retain their own base-model fingerprint and cannot be loaded as ThreadSpire internal generations.

## 10. Weaknesses and tradeoffs

- Byte tokenization is universal but sequence-inefficient.
- A 22.5M–93.6M parameter model trained from random weights will not immediately match a broadly pretrained model.
- The bundled corpus is a narrow bootstrap, not web-scale language pretraining.
- Local teacher distillation can accelerate growth but must be filtered to prevent imitation loops and identity contamination.
- Held-out loss is only an initial gate; behavioral and executable benchmarks remain necessary.
- Optimizer checkpoints can exceed model-weight size.
- Training and inference still contend for one GPU, so preemption has nonzero latency.
- Per-layer experts are architecturally modular, but completely hot-swappable expert files require a later ABI split from the current whole-generation SafeTensors file.

## 11. Long-term evolution

The stable boundaries are:

- tokenizer ABI;
- generation manifest format;
- candidate parent generation;
- skill names and routing metadata;
- evidence provenance;
- evaluator interface;
- process protocol.

The transformer implementation, tokenizer compression, router, experts, evaluation, scheduler, memory index, and training algorithm can evolve independently as new candidate types, provided they publish compatible versioned artifacts and never rewrite the active generation in place.
