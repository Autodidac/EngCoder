# Continual Learning Pipeline

## Internal-model state machine

```text
random initialization
  -> initialized-untrained generation
      -> bootstrap shadow training
          -> preempted/resumable
          -> rejected
          -> promoted trained generation
              -> captured experience
                  -> context/retrieval
                  -> negative evidence
                  -> verified weight evidence
                      -> scoped internal skill candidate
                          -> preempted/resumable
                          -> rejected
                          -> promoted generation
```

The active generation is never the training target.

## Positive evidence admission

A live episode may enter positive weight training only when it has:

- nonempty prompt and response;
- unique normalized content;
- no human rejection;
- deterministic verification, trusted bundled provenance, or at least four distinct human-approved examples for the same behavior;
- durable-skill classification rather than volatile state;
- a supported skill label.

Compiler and tests can provide deterministic verification. A model judging its own answer correct cannot.

## Bootstrap data

The bundled files are message-disjoint:

```text
bootstrap.jsonl          3,632 unique training records
bootstrap_anchors.jsonl    320 held-out anchors
```

They cover English, exact instruction following, multilingual-to-English reading, C++23, debugging, architecture, tests, and tool trajectories. They are a starting curriculum, not a mature language pretraining corpus.

## Continual-training target control

After bootstrap, the automatic continual-learning target starts on `english-foundation`. The initial bootstrap corpus still contains both language and coding material, but C++ specialization is no longer silently selected as the first continual target. The Learning page keeps target selection, automatic example generation, evidence source, and candidate queueing independent.

Built-in seed records are filtered to the selected target. Cursors are stored per target and dataset, so switching targets does not consume another skill's sequence. Trusted custom JSONL can be imported without a teacher, but it still goes through shadow training, evaluation, promotion, and rollback. See `CUSTOM_TRAINING.md`.

## Full-weight internal candidate

For `scope=all`, bootstrap training updates the complete custom transformer.

For a named scope, all shared parameters are frozen. The worker trains the corresponding residual skill expert in every layer plus final normalization. Supported initial scopes are:

- `english-core`
- `cpp23-core`
- `cpp23-debugging`
- `planning`

Training records are selected by explicit skill, domain, and relevant tags.

## Checkpoint and preemption

At configured intervals and before cooperative preemption, the worker saves:

- model state;
- optimizer state;
- pending parameter gradients and gradient-accumulation phase;
- step and target-token counters;
- dataset order and cursor;
- Python, CPU PyTorch, and CUDA RNG states.

A foreground inference marker causes a checkpoint and exit code `75`. The C++ coordinator marks the candidate interrupted and requeues it. The next worker resumes the same candidate state.

## Evaluation and promotion

The worker evaluates the active parent and shadow candidate on held-out anchors. Bootstrap may promote the first finite trained child of an untrained parent. Later candidates must not worsen the held-out loss under the current initial policy.

Promotion also requires the C++ candidate parent generation to match the active skill registry. This blocks stale candidates from overwriting newer work.

Published generations include a SafeTensors checksum and metrics. `active.json` is replaced atomically only after publication.

## Immediate learning versus weight learning

Immediate memory is the fastest learning path and remains active even when background weight training is paused. It stores user corrections, accepted context, and verified outcomes for retrieval in later prompts. Short conversation prompts may use a unique conservative spelling match; code, tools, and repository prompts require exact verified matches. The original prompt is never rewritten and a typo match never creates synthetic training evidence.

Weight learning is slower and more conservative. One human-approved phrase remains memory-only. Four distinct approvals for the skill, or independent compiler/test/reference evidence, are required before a record can enter a shadow candidate. Both the C++ admission service and Python data loaders enforce that rule, including for historical records. Turning off background learning stops candidate training but does not erase context or memory.

## Conversation, coding, and self-iteration

ThreadSpire keeps these concerns separate without exposing sampler jargon in the interface:

- ordinary conversation uses modest response variation while retaining the structural corruption gate;
- coding and tool execution use low-variance generation, exact memory, and compiler/test evidence;
- the persistent verified-memory store is the small shared starter cache used before any provider route;
- self-iteration is a repository transaction: propose a patch, validate it, build, test, and either retain the verified result or roll back.

A successful repository trajectory is admitted as deterministic coding evidence and may queue a scoped shadow update. It does not grant the model permission to rewrite or replace the running executable. Source and model promotion remain separate atomic operations with independent rollback.

## Automatic teacher generation and immediate updates

A connected external local LLM may propose examples. The internal model is never eligible to teach or verify itself. A distinct external verifier, trusted bundled reference, compiler, test suite, or user acceptance must establish positive evidence; otherwise generated output remains quarantined.

A trusted or independently verified episode increments the corresponding skill's pending-evidence counter. For the internal model, the promotion microbatch is one admitted episode, so the skill is queued immediately. Duplicate queued requests for the same skill coalesce. The teacher loop pauses while that queue or an active trainer exists, then resumes after the weight work drains.

The connected model's weights are never copied into the internal model. Only verified input/output evidence is loaded by the Python worker and used to train ThreadSpire's own tensors.

The Learning page reports both sides of this pipeline:

```text
teacher/reference admission
  -> evidence counter
  -> queued skill update
  -> active shadow candidate
  -> trainer progress/log
  -> held-out evaluation
  -> rejected or atomically promoted generation
```

## Generated-output gate

The private model uses byte tokens, so a generic token-repetition penalty would distort ordinary character frequencies. Generation instead blocks only exact repeated byte n-gram continuations. Completed internal drafts pass a structural corruption gate before display. Failed drafts are retried once greedily, then returned as `low_confidence` so routing can fall back without showing the rejected text.

This gate detects loops and malformed output; it is not a factual verifier.

## Optional external LoRA

The legacy external-model path remains available:

```text
explicit external base
  -> LoRA candidate
  -> evaluation
  -> optional FFN-delta consolidation
```

This path has a separate artifact ABI and cannot masquerade as an internal generation.
