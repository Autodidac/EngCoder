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

- nonempty prompt and response after training-only Unicode cleanup;
- no emoji, keycap sequences, joiners, or variation selectors in stored learning text;
- EngCoder assistant identity rather than an underlying teacher/provider persona;
- unique normalized content;
- no human rejection;
- deterministic verification, trusted bundled provenance, or at least four distinct human-approved examples for the same behavior;
- durable-skill classification rather than volatile state;
- a supported skill label.

Compiler and tests can provide deterministic verification. A model judging its own answer correct cannot.

Chat display is intentionally lossless, but the learning boundary is not: emoji are removed from prompts, replies, and corrections before hashing or storage. Assistant self-identification from an underlying provider is normalized to EngCoder while the original provider/model ID remains separate provenance metadata. The Learn preview shows the exact post-cleanup text. Both Python loaders repeat the cleanup so historical or directly imported JSONL cannot bypass it.

## Bootstrap data

The bundled files are message-disjoint:

```text
bootstrap.jsonl          4,247 unique training records
bootstrap_anchors.jsonl    320 held-out anchors
```

They cover English, exact instruction following, multilingual-to-English reading, C++23, debugging, architecture, tests, and tool trajectories. They are a starting curriculum, not a mature language pretraining corpus.

## Autonomous English foundation

**Build English Foundation** is explicit, one-time, resumable, and separate from Live Auto and idle practice generation. Qwen maps licensed/user-owned long-form sources into competencies and difficult lessons. Nemotron expands train-partition lessons into short and medium variants. Qwen verifies Nemotron records and Nemotron verifies Qwen records; a model cannot approve its own output. Entire chapters or sections are assigned to training or held-out partitions before examples are generated.

The accepted document holdouts are combined with the 320 bundled anchors. Initial foundation promotion evaluates the complete combined set and requires the structural-output, identity, privacy, readiness, and loss-regression gates. The initial optimizer scope is the complete internal model; later microbatches freeze shared weights and train bounded experts. See [Autonomous Curriculum and Training](autonomous-curriculum-and-training.md).

## Continual-training target control

After bootstrap, the automatic continual-learning target starts on `english-foundation`. The initial bootstrap corpus still contains both language and coding material, but C++ specialization is no longer silently selected as the first continual target. The Learning page keeps target selection, automatic example generation, evidence source, and candidate queueing independent.

Built-in seed records are filtered to the selected target. Cursors are stored per target and dataset, so switching targets does not consume another skill's sequence. Trusted custom JSONL can be imported without a teacher, but it still goes through shadow training, evaluation, promotion, and rollback. See [EngCoder Learning and Retraining](../custom-training.md).

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
## Automatic idle learning and explicit weight training

Automatic idle learning is a persistent lightweight pipeline, not a timed session. When enabled, it curates approved evidence, asks available local teacher models for bounded variations, independently verifies the results, deduplicates them, and admits only eligible records. It always yields to Chat and other foreground work. Disabling it stops new automatic teacher passes; it does not erase accepted evidence or corrupt queued checkpoints.

Live Auto Training is a third, separately persisted pipeline. It turns successful live conversations into a one-pass ledger, requires the exact authorized local Qwen and Nemotron families to review every acceptance, and exposes every accepted, revised, needs-evidence, rejected, or privacy-excluded decision. It never changes foreground AI selection. See [Live Auto Training](live-auto-training.md).

Verified evidence automatically queues one coalesced shadow update per topic when its configured microbatch fills. `Compile data and retrain model` is a separate explicit action that can force a smaller pending verified batch. The persistent worker trains, evaluates, and promotes or rejects each candidate while the last promoted model continues serving. Neither pathway is automatic software self-improvement and neither edits application source.

The 12-hour requirement is a compute ceiling for a complete bounded compile, training, evaluation, and promotion cycle on the target RTX 5080 / Ryzen 9 9900X / 64 GiB system. It is enforced through bounded model size, curated replay, capped steps, checkpointing, and serialized candidates—not through a wall-clock countdown in the interface. The cycle ends when the finite work completes. Foreground inference still requests a safe checkpoint and recovery code `75`; the same candidate resumes afterward.

The visible report separates what can change: permanent verified memory is immediately retrievable but never weights; internal weights require the optional PyTorch trainer; Transformers/PEFT applies only to an explicitly compatible external base; image and audio engines are separate optional packs and are not trained by the text-weight workflow. Selecting or installing any pack remains isolated from both automatic learning and explicit training.
## Evaluation and promotion

The worker evaluates the active parent and shadow candidate on held-out anchors. Bootstrap may promote the first finite trained child of an untrained parent. Later candidates must not worsen the held-out loss under the current initial policy.

Promotion also requires the C++ candidate parent generation to match the active skill registry. This blocks stale candidates from overwriting newer work.

Published generations include a SafeTensors checksum and metrics. `active.json` is replaced atomically only after publication.

## Immediate learning versus weight learning

Immediate memory is the fastest learning path and remains active even when background weight training is paused. It stores user corrections, accepted context, and verified outcomes for retrieval in later prompts. Short conversation prompts may use a unique conservative spelling match; code, tools, and repository prompts require exact verified matches. The original prompt is never rewritten and a typo match never creates synthetic training evidence.

Conversational `remember`, `forget`, memory-list, `teach`, and `correct` controls are intercepted before provider routing. They print exact receipts and never contact a model. Permanent user facts remain in the editable user-memory file and are never weight data. Taught replies and corrections enter the same admission service used by the per-reply controls. A correction supersedes every earlier positive answer for that normalized prompt in exact and semantic retrieval; historical drafts remain negative audit evidence only. Both optional Python loaders independently keep only the newest hard correction and exclude stale positives before batching.
The visible Training Library applies the same rule: it presents the correction as the approved expected answer, shows the replaced draft as excluded audit material, and labels older approvals for the prompt as superseded.

Weight learning is slower and more conservative. One human-approved phrase remains memory-only. Four distinct approvals for the skill, or independent compiler/test/reference evidence, are required before a record can enter a shadow candidate. Both the C++ admission service and Python data loaders enforce that rule, including for historical records. Turning off Live Auto or automatic idle learning stops only that source; already admitted evidence, queued candidates, explicit training, context, and memory remain independent.

The interface reports two separate totals. **User-taught replies** is the number of durable human-approved or corrected examples. **Neural-weight jobs** is the number of coalesced topic updates in the persistent trainer queue. Job coalescing prevents redundant retraining; it never erases the individual teachings stored in memory/episodes.jsonl.

## Conversation, coding, and self-iteration

EngCoder keeps these concerns separate without exposing sampler jargon in the interface:

- ordinary conversation uses modest response variation while retaining the structural corruption gate;
- coding and tool execution use low-variance generation, exact memory, and compiler/test evidence;
- the persistent verified-memory store is the small shared starter cache used before any provider route;
- self-iteration is a separate repository transaction: pin the selected revision, clone it privately, propose and apply patches only in that clone, build, test, and seal a passing candidate commit.

The selected repository is never the build/repair workspace and is never hard-reset. A passing trajectory remains quarantined until its conversation result is explicitly approved. Approval rechecks the candidate commit, original source revision, and clean source status, then publishes only an `engcoder/candidate-…` review branch; it does not switch, merge, or change working files. Rejection publishes nothing. Compiler/test evidence can enter scoped shadow learning only after this human approval, so source promotion and model-weight promotion remain separate atomic operations.

## Automatic teacher generation, Live Auto, and weight updates

A connected external local LLM may propose examples. The internal model is never eligible to teach or verify itself. A distinct external verifier, trusted bundled reference, compiler, test suite, or user acceptance must establish positive evidence; otherwise generated output remains quarantined.

A transient reviewer cancellation, endpoint failure, or malformed JSON response is not negative evidence. Live Auto records a retry-pending audit entry, leaves the ledger cursor on that interaction, waits 30 seconds, and tries again when Chat is idle. Permanent rejection is reserved for a completed decision that says the material is not reusable.

A trusted or independently verified episode increments the corresponding skill's pending-evidence counter. The internal-base promotion microbatch is eight eligible episodes; external-base policy defaults to four. Duplicate queued requests for the same skill coalesce. The Live Auto and teacher loops pause while that queue or an active trainer exists, then resume after the weight work drains. The explicit retrain button can request a smaller nonempty pending batch.

The connected model's weights are never copied into the internal model. Only verified input/output evidence is loaded by the Python worker and used to train EngCoder's own tensors.

Live Auto also updates routing evidence for the model that produced the original answer. An accepted original raises quality gradually; a revised or rejected original lowers it; a needs-evidence outcome is neutral. Automatic foreground routing can therefore shift toward EngCoder only after demonstrated improvement.

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
