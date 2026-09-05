# Minimal Curriculum Compiler

## Goal

EngCoder should preserve enough evidence to explain every learning decision while training its compact internal model on the smallest practical set of distinct, reusable behaviors. Saving a record and training on it are therefore separate operations.

The compiler implements a conservative bounded-selection policy. It removes known redundancy and contamination before a neural candidate starts. It does not claim to solve the global minimum-dataset problem, and it never deletes the evidence that justified a decision.

## Three data layers

```text
append-only evidence pool
  -> compact canonical curriculum
      -> immutable candidate snapshot

held-out anchors -----------------> evaluation only
```

### Append-only evidence pool

This is the inspectable history. It may contain:

- bundled source records;
- user approvals and hard corrections;
- generated foundation and Live Auto attempts;
- independently verified references;
- rejected, superseded, privacy-excluded, and retry-pending records;
- compiler and test evidence for coding behaviors.

An evidence row is not automatically a gradient row. Keeping negative and superseded material in the audit pool makes decisions explainable without teaching those records to the model.

### Compact canonical curriculum

The compiler reads eligible source ledgers and produces a deterministic derived JSONL file. It filters unsafe or irrelevant records, excludes direct held-out prompt overlap, collapses duplication, and selects an authoritative representative for each remaining behavior under a supervised-token budget.

This file can be regenerated. The compiler never edits or truncates the input ledgers.

### Immutable candidate snapshot

The selected JSONL and its manifest are stored inside the isolated neural candidate. They do not change while training, evaluation, or promotion is running. A later correction or teaching belongs to a later candidate instead of mutating an in-flight experiment.

For an incremental update, the snapshot contains:

- the newest eligible **delta** first; and
- a small, coverage-balanced **replay** selection from older canonical behaviors.

The full accumulated episode ledger is not replayed on every update.

## Compilation stages

The compiler performs these stages in a stable order:

1. resolve every input, anchor, and output path;
2. hash all input files;
3. parse JSONL while retaining invalid-line counts for the manifest;
4. clean training-only Unicode noise;
5. identify the newest hard correction for each exact normalized prompt;
6. reject unsafe, invalid, ineligible, or out-of-scope records;
7. compute held-out normalized prompt keys and exclude direct training overlap;
8. collapse exact prompt/answer duplicates;
9. collapse canonical behavior duplicates;
10. cap repeated identical answer targets while preserving competency coverage;
11. rank representatives by authority, quality, and concision;
12. select the fresh delta, then bounded coverage-balanced replay;
13. enforce the supervised-token budget;
14. atomically write the selected JSONL and manifest;
15. re-hash the inputs and fail if a ledger changed during compilation.

Source argument order does not determine output order. Equivalent stable inputs and policy values produce the same selected-record order and output hash.

## Eligibility and exclusion

The compiler excludes records that are:

- malformed or missing a usable prompt/answer pair;
- explicitly rejected or not weight-eligible;
- error cards, corrupt drafts, or control/status text;
- private, machine-specific, credential-bearing, or nonpublic;
- volatile current facts without durable tool/reference evidence;
- provider-branded or leaking a teacher identity;
- outside the requested English, planning, or evidence-backed coding scope;
- non-English where the active scope requires English;
- emoji-only after training cleanup;
- superseded by a newer hard correction;
- an exact normalized prompt overlap with a held-out anchor;
- redundant after exact or canonical clustering;
- beyond the token or replay budget.

Useful text containing emoji is cleaned and may remain eligible; emoji-only pairs are rejected. Original display text and provenance remain in the audit record. Coding records require the explicit coding/planning scope and positive evidence such as user acceptance, a trusted reference, compiler success, or test success.

## Behavior clustering and answer-target caps

Exact deduplication alone is insufficient. These prompts should not create four independent teachings:

```text
What's up?
What is up?
Whats up
What's going on?
```

The behavior signature conservatively normalizes:

- case, punctuation, and common contractions;
- polite filler and supported intent synonyms;
- numeric operands;
- paths and identifier-shaped values;
- quoted payloads;
- recognized arithmetic, definition, transformation, and conversational templates.

Constraint polarity and safety-sensitive answer polarity remain in the signature, so “must” and “must not,” or an allowed and refused action, cannot collapse into the same behavior.

This is deterministic semantic normalization, not an embedding-based claim that every possible paraphrase is equivalent. Unrecognized wording remains in a separate cluster and can be reduced only by a later reviewed policy change.

After one representative is selected per behavior, the compiler separately limits repeated identical normalized answer text. The default cap is four. It first preserves representatives from different competency labels, then applies authority and concision ranking. This prevents generic answers such as “No.” or “Use one clear sentence.” from dominating the gradient set without discarding all contexts in which a short answer is legitimate.

The answer control currently uses exact normalized target text as its safe template key. It does not guess that two materially different answers mean the same thing.

The same signature is used for human-diversity admission. Four surface variants of one behavior count as one behavior cluster. Four materially different approved behaviors, or independent deterministic/reference evidence, are required by the human-only weight gate.

## Representative selection

Within an equivalent cluster, selection prefers evidence in this order:

1. newest applicable hard correction;
2. human-approved material;
3. reference-verified material;
4. deterministic evidence;
5. compiler/test evidence;
6. other accepted, non-self-generated material;
7. higher recorded quality;
8. fewer supervised byte tokens;
9. stable identifiers and hashes as deterministic tie breakers.

The default maximum is one record per behavior cluster. The original, longer, or lower-authority alternatives remain visible in the evidence pool with their exclusion reason.

## Delta and replay policy

Foundation training has no historical replay phase. It selects one representative per behavior under a default budget of 131,072 supervised byte tokens.

An incremental update defaults to:

- 32,768 supervised byte tokens total;
- the newest eligible candidate delta requested by the queued job; and
- at most 32 older replay representatives.

Replay is selected in coverage-balanced rounds so one dense topic cannot consume the entire allowance. Explicit environment overrides remain clamped to hard limits. Candidate preparation fails rather than training if no safe, non-held-out behavior survives.

## No hidden row multiplication

Earlier evidence may contain `curriculum_weight`. The compiler strips this field from every selected record. The internal loader also treats one selected record as one physical training sequence.

This prevents a single saved phrase from silently becoming several duplicate gradient rows. Any optimization repetition is an explicit property of the trainer's step schedule and checkpoints, not hidden JSONL expansion.

## Corpus-boundary validation and early success

The fixed step value supplied for a run is a hard safety ceiling, not a requirement to replay a compact curriculum until that number is exhausted. The trainer derives a validation interval from the selected sequence count and batch size, then aligns it to a completed gradient-accumulation boundary. Each interval therefore covers every selected sequence at least once without evaluating partially accumulated gradients.

A newly initialized foundation becomes eligible for an early-success check after two complete selected-corpus intervals. An already-trained expert becomes eligible after one. At each later interval before the ceiling, the candidate is evaluated against the same held-out loss, generation-readiness, structural-output, identity, privacy, and foundation-anchor gates used for promotion. Training stops immediately when every applicable gate passes. If no boundary passes, training continues only to the safety ceiling; failure there preserves the prior active generation and saves the appropriate diagnostic or repair state.

This schedule makes the compiled curriculum the unit of coverage. Reducing redundant rows can shorten a corpus pass and reach a meaningful validation point sooner, but it does not weaken held-out isolation or turn the configured ceiling into a promised amount of optimization.

## Evaluation isolation

The fixed 320 anchors and accepted document holdouts are evaluation inputs only. The compiler excludes a training candidate with the same exact normalized prompt before selection. Anchor records never appear in `minimal-training.jsonl` and never contribute gradient labels.

Document-level partitioning still occurs before foundation examples are generated. Exact prompt exclusion is an additional direct-leakage barrier, not a substitute for document-level splitting. Related but differently worded training behavior remains necessary for held-out evaluation to measure generalization.

## Artifacts and manifest

For a candidate, the compiler writes:

```text
dataset/minimal-training.jsonl
dataset/curriculum-selection.json
```

The latest successfully compiled selection is also copied to:

```text
%LOCALAPPDATA%\EngCoder\state\curriculum-selection.json
```

The manifest records:

- policy and schema version;
- scope and exact source/anchor paths;
- per-input and combined input SHA-256 values;
- supervised-token, per-cluster, delta, and replay budgets;
- input, parsed, eligible, cluster, representative, and selected counts;
- selected supervised-token count;
- selected, delta, and replay record IDs;
- requested but unresolved delta IDs;
- coverage counts;
- every exclusion reason count;
- compiled-output SHA-256.

The interface uses the latest-state manifest to show the selection funnel. The candidate-local manifest remains the immutable record of what that exact candidate trained on.

## What minimization changes

Curriculum minimization reduces:

- repeated paraphrases and identical answer targets;
- accidental memorization pressure;
- stale-history replay;
- candidate preparation and training work;
- the amount of unrelated knowledge presented to a scoped expert.

It does not automatically reduce the size of a fixed neural-weight file. Weight-file size is principally:

```text
parameter count × bytes per stored parameter + metadata
```

Reducing tensor size requires architecture choices such as fewer parameters, lower precision or quantization, pruning, or a different expert layout. Those changes need their own evaluation and release evidence. They are not implied by a smaller dataset.

## Audit invariants

- Input evidence remains append-only and inspectable.
- Rejected and superseded rows never become positive gradients.
- A teacher cannot verify its own output.
- A held-out record cannot enter the training snapshot, and an exact normalized prompt overlap is excluded.
- A correction cannot resurrect the answer it replaced.
- One selected row cannot multiply through `curriculum_weight`.
- Incremental training cannot consume unbounded historical replay.
- Candidate inputs cannot change after snapshot creation.
- The manifest identifies every selected row and every class of exclusion.
- Promotion gates remain authoritative; compilation alone never promotes a model.
