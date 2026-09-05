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
                      -> minimal curriculum compiler
                          -> fresh delta + bounded replay snapshot
                              -> scoped internal skill candidate
                                  -> preempted/resumable
                                  -> rejected
                                  -> promoted generation
```

The active generation is never the training target. The append-only evidence ledger is never used directly as the gradient dataset.

## Positive evidence admission

A live episode may enter positive weight training only when it has:

- nonempty prompt and response after training-only Unicode cleanup;
- no emoji, keycap sequences, joiners, or variation selectors in stored learning text;
- EngCoder assistant identity rather than an underlying teacher/provider persona;
- unique exact content and a distinct canonical behavior cluster;
- no human rejection;
- deterministic verification, trusted bundled provenance, or at least four distinct human-approved behavior clusters for the skill;
- durable-skill classification rather than volatile state;
- a supported skill label.

Compiler and tests can provide deterministic verification. A model judging its own answer correct cannot.

Chat display is intentionally lossless, but the learning boundary is not: emoji are removed from prompts, replies, and corrections before hashing or storage. Assistant self-identification from an underlying provider is normalized to EngCoder while the original provider/model ID remains separate provenance metadata. The Learn preview shows the exact post-cleanup text. Both Python loaders repeat the cleanup so historical or directly imported JSONL cannot bypass it.

Human diversity is behavior-based, not phrase-based. Case, punctuation, contractions, polite filler, supported intent aliases, and variable literal slots do not turn one teaching into several independent approvals. Constraint polarity and safety-sensitive answer polarity remain distinct so canonicalization cannot merge opposite behaviors.

## Bootstrap data

The bundled files are message-disjoint:

```text
bootstrap.jsonl          4,247 source evidence records
bootstrap_anchors.jsonl    320 held-out anchors
```

They cover English, exact instruction following, multilingual-to-English reading, C++23, debugging, architecture, tests, and tool trajectories. They are a starting evidence pool, not a mature language pretraining corpus and not 4,247 mandatory gradient rows. The compiler selects a smaller scope-appropriate set for each candidate.

## Minimal curriculum compiler

Every internal neural job derives a separate training view from its eligible evidence. The compiler:

1. reads source JSONL and records stable hashes without rewriting the ledgers;
2. excludes rejected, ineligible, error, private, branded, non-English, out-of-scope, and emoji-only material;
3. applies the newest hard correction and excludes all superseded positives for that exact normalized prompt;
4. excludes exact normalized prompt overlap with a held-out anchor;
5. collapses exact duplicates;
6. clusters paraphrases under one conservative behavior signature;
7. chooses at most one authoritative, concise representative per behavior;
8. caps repeated identical answer targets while preserving competency coverage;
9. selects the fresh eligible delta first for an incremental update;
10. fills only a bounded replay allowance in coverage-balanced rounds;
11. writes the selected JSONL and selection manifest atomically, then confirms the input hashes did not change.

The default foundation budget is 131,072 supervised byte tokens and has no historical replay allowance. The default incremental budget is 32,768 supervised byte tokens with at most 32 older replay representatives. An identical normalized answer target contributes at most four representatives by default, selected across distinct competency coverage first. Environment overrides are bounded. `curriculum_weight` is removed from compiled records, so one selected row produces one gradient sequence rather than being physically duplicated.

The compiled dataset and `curriculum-selection.json` are stored with the candidate. A latest-state copy lets the interface report evidence rows, behavior clusters, selected records/tokens, fresh delta, replay, coverage, exclusion reasons, and hashes. Held-out rows never appear in the compiled JSONL.

This is a conservative bounded selection policy, not a mathematical proof of the globally smallest possible corpus. Promotion evaluation determines whether the selected evidence is sufficient. Dataset minimization reduces repetition, memorization pressure, and compute; it cannot change the byte size of an architecture with fixed parameter count and precision. See [Minimal Curriculum Compiler](minimal-curriculum-compiler.md).

The trainer treats configured step counts as safety ceilings. It derives an optimizer-aligned validation interval large enough to present every selected sequence at least once. A new foundation must complete two intervals before early success is possible; an already-trained expert must complete one. At each eligible boundary it runs the applicable promotion gates and stops as soon as all pass. It never evaluates partially accumulated gradients, and reaching the ceiling without passing cannot replace the active model.

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

Training records are selected by explicit skill, domain, and relevant tags, then canonicalized and budgeted before the worker sees them.

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

Live Auto Training is a third, separately persisted pipeline. It turns successful live conversations into a one-pass ledger. The model family that authored a reply cannot review that reply: the other authorized local family performs the independent check. If that reviewer changes the answer, the other distinct family must confirm the revision. Every accepted, revised, needs-evidence, retry-pending, rejected, or privacy-excluded decision remains visible. Three independently reviewed reusable conversation failures wake a separate measured-gap pass: Qwen repairs one negative example at a time, Nemotron verifies it, and only the replacement may enter admission. It never changes foreground AI selection. See [Live Auto Training](live-auto-training.md).

Verified evidence automatically queues one coalesced shadow update per topic when its configured microbatch fills. `Compile data and retrain model` is a separate explicit action that can force a smaller pending verified batch. Candidate preparation compiles the new eligible delta plus bounded replay; it does not replay the complete episode history. The persistent worker trains, evaluates, and promotes or rejects each candidate while the last promoted model continues serving. Neither pathway is automatic software self-improvement and neither edits application source.

Each compile, training, evaluation, and promotion attempt is finite because model size, supervised-token budget, replay allowance, step safety ceiling, corpus-boundary early-success checks, checkpoints, and serialized candidates are bounded. These are work and resource limits, not a wall-clock promise or countdown. The attempt ends when every applicable quality gate passes at an eligible boundary or the safety ceiling is reached; failure at the ceiling preserves the active generation. Foreground inference still requests a safe checkpoint and recovery code `75`; the same candidate resumes afterward.

The visible report separates what can change: permanent verified memory is immediately retrievable but never weights; internal weights require the optional PyTorch trainer; Transformers/PEFT applies only to an explicitly compatible external base; image and audio engines are separate optional packs and are not trained by the text-weight workflow. Selecting or installing any pack remains isolated from both automatic learning and explicit training.

## Evaluation and promotion

The worker evaluates the active parent and shadow candidate on held-out anchors. The compiler excludes exact normalized prompt overlap, and anchors are supplied only to evaluation—not to the gradient loader. Bootstrap may promote the first finite trained child of an untrained parent. Later candidates must not worsen the held-out loss under the current initial policy.

Promotion also requires the C++ candidate parent generation to match the active skill registry. This blocks stale candidates from overwriting newer work.

Published generations include a SafeTensors checksum and metrics. `active.json` is replaced atomically only after publication.

## Immediate learning versus weight learning

Immediate memory is the fastest learning path and remains active even when background weight training is paused. It stores user corrections, accepted context, and verified outcomes for retrieval in later prompts. Short conversation prompts may use a unique conservative spelling match; code, tools, and repository prompts require exact verified matches. The original prompt is never rewritten and a typo match never creates synthetic training evidence.

Conversational `remember`, `forget`, memory-list, `teach`, and `correct` controls are intercepted before provider routing. They print exact receipts and never contact a model. Permanent user facts remain in the editable user-memory file and are never weight data. Taught replies and corrections enter the same admission service used by the per-reply controls. A correction supersedes every earlier positive answer for that normalized prompt in exact and semantic retrieval; historical drafts remain negative audit evidence only. Both optional Python loaders independently keep only the newest hard correction and exclude stale positives before batching.
The visible Training Library applies the same rule: it presents the correction as the approved expected answer, shows the replaced draft as excluded audit material, and labels older approvals for the prompt as superseded.

Weight learning is slower and more conservative. One human-approved phrase remains memory-only. Four distinct human-approved behavior clusters for the skill, or independent compiler/test/reference evidence, are required before a record can enter a shadow candidate. Four paraphrases or template substitutions for the same behavior still count as one. The C++ admission service and Python data loaders enforce that rule, including for historical records. Turning off Live Auto or automatic idle learning stops only that source; already admitted evidence, queued candidates, explicit training, context, and memory remain independent.

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

A transient reviewer cancellation, endpoint failure, or malformed JSON response is not negative evidence. Live Auto records a retry-pending audit entry, leaves the ledger cursor on that interaction, waits 30 seconds, and tries again when Chat is idle. Permanent rejection is reserved for a completed decision. Only a completed rejection explicitly classified as a reusable conversation gap enters the separate negative-evidence ledger; that failed answer is never a positive example.

A trusted or independently verified episode increments the corresponding skill's pending-evidence counter. The internal-base promotion microbatch is eight eligible episodes; external-base policy defaults to four. Duplicate queued requests for the same skill coalesce. Before training, exact and canonical duplicates collapse to one representative, so the queue count is not the eventual gradient-row count. The Live Auto and teacher loops pause only while a trainer is genuinely active, then resume after the active candidate yields or finishes. A merely queued or recoverable job—such as one waiting for the optional trainer—does not stop evidence generation. The explicit retrain button can request a smaller nonempty pending batch.

The connected model's weights are never copied into the internal model. Only verified input/output evidence is loaded by the Python worker and used to train EngCoder's own tensors.

Live Auto also updates routing evidence for the model that produced the original answer. An accepted original raises quality gradually; a revised or rejected original lowers it; a needs-evidence outcome is neutral. Automatic foreground routing can therefore shift toward EngCoder only after demonstrated improvement.

The Learning page reports both sides of this pipeline:

```text
teacher/reference admission
  -> append-only evidence and audit pool
  -> behavior-cluster evidence counter
  -> queued skill update
  -> compact delta + bounded replay snapshot
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
