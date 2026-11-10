# EngCoder training

EngCoder owns the model lifecycle; the included Python/PyTorch worker performs numerical tensor work behind a native C++ scheduling boundary.

## C++ boundary repair lab

### Run a generated repair in EngCoder

Use a disposable CMake candidate with independently written regression tests and configure its build before opening EngCoder. Set these variables for the candidate runtime:

| Setting | Value |
| --- | --- |
| `ENGCODER_ITERATION_CANDIDATE` | Absolute candidate checkout path, also used as the server workspace |
| `ENGCODER_ITERATION_BUILD_DIR` | Configured CMake build directory relative to the candidate |
| `ENGCODER_ITERATION_ESCALATION_MODEL` | Optional exact ID of a stronger model advertised by the configured provider |
| `ENGCODER_ITERATION_BOUNDARY_SOURCE` | Candidate-relative path to the one `.cpp` file the models may edit |
| `ENGCODER_ITERATION_BOUNDARY_RULE` | `clamp`, `index`, `closed`, or `capacity` |

In **EngCoder Lab → Runtime**, enable candidate self-iteration for the next send. Choose the initial model and describe the repair in chat. Each edit needs its own approval. Existing files require exact patches, and changes to the file while approval is pending invalidate that approval. The boundary workflow reproduces a real CTest failure before any edit and restricts edits to the configured source. When a candidate regression test fails, Architect can switch once to the configured stronger model within the existing two-repair limit. The next model gets the same candidate and actual validation output. Build and test process failures without a reported regression stay on the original route. Candidate runtimes bind to loopback.

Open the answer's **Run info** for the route change, build and test timeline, and saved lesson. After the source changes and all checks pass, EngCoder records the configured boundary rule as verified memory and can recall it for future scoped runs. It stores the compact rule without copying the candidate source or model prose into Learning, and this record is excluded from weight training. **EngCoder Lab → Learning** shows the memory and its source. This generated-repair workflow and the CLI's 228-byte option ranker have separate responsibilities.

### Train and compare the option ranker

Run from an EngCoder checkout with Python 3.8 or later, CMake, and a C++23 compiler. Choose a new output directory for each experiment.

```powershell
EngCoderCLI --boundary-lab train out/boundary-experiment
EngCoderCLI --boundary-lab teacher out/boundary-experiment http://127.0.0.1:39463 local-model-id
EngCoderCLI --boundary-lab route --help
```

`train` looks up three fixed C++ draft sections, records the URL, retrieval time and response hash, and retains authored notes instead of raw web content. It generates English descriptions and original C++ repair options for inclusive clamp, signed indexing, closed ranges, and unsigned capacity arithmetic. CMake compiles the authored options. The executable tests them against independently calculated boundary expectations, requiring a failing original and exactly one passing replacement in each case. External answers are treated only as A/B/C/D choices.

The optimizer deduplicates equivalent option sets before fitting the ranker. The serialized candidate has 56 single-precision learned weights and a four-byte version header, for 228 bytes. Held-out prompts change code structure and problem wording, and are scored after the artifact reloads. All cases still belong to the four supported families. This benchmark measures repair selection among supplied options.

To score teachers during the same command, append `--teacher http://127.0.0.1:39463 local-model-id` for each available local endpoint. Set `ENGCODER_BOUNDARY_TEACHER_TOKEN` in the invoking environment if the endpoint requires it. Only loopback is accepted. A refused, malformed or unavailable answer is reported separately from a wrong letter.

The separate `teacher --constrained` mode requests llama.cpp's four-letter grammar and disables template thinking. It reports a separate score for selection under a constrained decoder so answer-format and token-limit effects can be examined alongside the plain-prompt score.

`report.json` shows the learned and untrained scores, and the exhaustive verifier ceiling. `training-evidence.json` records the concrete repairs and test receipts. Each teacher has a separate report. `--boundary-lab suggest --help` explains how to try a saved ranker on four supplied C++ bodies. Keep the ranker alongside its `report.json` for checksum verification. It produces a suggestion for verification. The lab keeps the main Foundation and active addons untouched and makes no general English or unrestricted code-generation claim.

`route` replays a named held-out case and compiles fresh probes. Like Epoch's self-iteration routing, it separates a failed verification from a missing model or response, and it keeps verification authoritative across every tier. A passing local repair ends the request before contacting a teacher. If the repair fails, repeat `--fallback LOOPBACK_URL MODEL` with up to two already configured models, smallest capable tier first. The lab calls each listed model at most once, and only if its endpoint advertises it. Teacher output can choose an authored option but cannot supply executable code or override the probes. The result records every attempted lane and either returns a verified repair or reports that no tier produced one.

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
