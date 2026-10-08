# EngCoder training

EngCoder owns the model lifecycle; the included Python/PyTorch worker performs numerical tensor work behind a native C++ scheduling boundary.

## Scratch model profiles

`desktop-16gb` is the intended serious single-GPU profile for this architecture. These are **scratch-model architecture sizes**, not LM Studio model/download sizes. Current measured profiles are:

| UI profile | Total params | Foundation | Addons | Context | Purpose |
| --- | ---: | ---: | ---: | ---: | --- |
| 359M · 16 GB GPU target | 358,947,840 | 259,857,408 | 99,090,432 | 2,048 | Recommended serious single-GPU profile |
| 262M · Desktop | 262,469,760 | 197,441,664 | 65,028,096 | 2,048 | Smaller desktop training |
| 123M · Large | 123,077,760 | 71,468,160 | 51,609,600 | 2,048 | Mid-size local training |
| 65.7M · Standard | 65,691,136 | 38,166,016 | 27,525,120 | 1,536 | Lightweight development/training |
| 29.6M · Micro | 29,605,248 | 17,218,944 | 12,386,304 | 1,024 | Pipeline/smoke testing only |

The selected profile determines the complete randomly initialized model. **Training target** then chooses which component of that model is updated during a run.

Initialization creates random weights. It does not copy weights from an external LLM.

## Recommended curriculum order

1. KeyMaker Foundation (`keymaker-foundation`).
2. Architect (`architect-controller`).
3. C++ capability (`cpp23-core`, then `cpp23-debugging`).
4. Documentation, planning, tool-use, reranking lanes.
5. Continual/background candidates from verified evidence.

Do not train every lane blindly at once; dependencies and evaluation quality matter more than maximum GPU occupancy.

## Candidate lifecycle

```text
active generation
    + curated data / eligible evidence
    v
resumable checkpoint
    v
candidate generation
    v
held-out/evaluation gates
   / \
promote reject
   |       |
active   audit history / prunable weights
```

Active weights are not modified in place. Promotion updates the active pointer only after candidate evaluation.

## Foreground inference preemption

The C++ runtime increments foreground inference users and writes `control/inference.priority`. The training worker checks that control state at safe boundaries, checkpoints, releases its training slot/cache, and exits with cooperative code 75. `TrainingService` treats 75 as `paused-for-inference`, waits until inference priority clears, and relaunches the same command. The worker resumes from the saved checkpoint.

Manual pause uses `control/training.paused`. Cancellation uses the C++ stop token/process cancellation path.

## Evidence input

When `<data-root>/learning/verified-evidence.jsonl` exists, C++ passes it as `--episodes` to `train-internal`. `LearningService` marks only sufficiently confident user/compiler/test/reference-verified records as training eligible, and the Python curriculum loader applies its own admission rules as well. Immediate retrieval memory and neural training therefore share evidence without being the same mechanism.

## API/UI

The EngCoder settings panel exposes profile, training scope, safety-cap steps, checkpoint interval, initialize, start/resume, pause/resume, cancel, and live status.

Relevant API routes are documented in `PROTOCOL.md`.
