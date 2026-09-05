# EngCoder Learning and Retraining

EngCoder keeps six kinds of evidence separate so the interface can say exactly what exists and what can affect an answer.

| Source | Purpose | Can change neural weights? |
|---|---|---|
| Bundled curriculum | The 4,247 curated source records shipped with EngCoder | Only a compact canonical selection can enter a verified shadow-training batch |
| Your learning | Replies you approve, edit, correct, or reject | Eligible under the diversity/evidence policy; the button can force a smaller pending batch |
| Live Auto Training | Cleaned live interactions reviewed by the authorized local Qwen and Nemotron pair | Accepted reusable behavior enters the normal verified microbatch automatically |
| Automatic teacher attempts | Separate seed/practice examples made by connected local AI apps while idle | Only after independent verification and admission |
| Generated foundation curriculum | Qwen/Nemotron cross-reviewed examples derived from licensed or user-owned sources | Train-partition records only; held-out sections never train |
| Held-out checks | The 320 fixed evaluation examples plus accepted document holdouts | Never; they are tests, not training data |

The Training Library shows these sources separately. Its total is an audit-record count, not a count of examples already baked into weights or a promise that every row will be replayed.

## Evidence, curriculum, and candidate data

EngCoder deliberately keeps three layers:

| Layer | Contents | Mutability | Used for gradients? |
|---|---|---|---|
| Evidence pool | Accepted, corrected, rejected, superseded, generated, and retry-pending audit records | Append-only | No, not directly |
| Canonical curriculum | The best safe representative for each eligible behavior cluster, subject to scope and token budgets | Recompiled deterministically | Eligible records only |
| Candidate snapshot | The selected fresh delta plus bounded, coverage-balanced replay for one training attempt | Immutable for that candidate | Yes |

The 320 anchors and accepted document holdouts form a fourth, evaluation-only set. They never become gradient rows, and exact normalized prompt overlaps are removed from the training selection. Document/section splitting and separate held-out evaluation test whether the selected behaviors generalize to unseen wording.

The compiler clusters both exact duplicates and equivalent prompt behaviors, then caps repeated identical answer targets while preserving competency coverage. It abstracts variable operands, paths, identifiers, quoted payloads, polite wording, and supported intent aliases while preserving safety constraints and answer polarity. A hard correction supersedes prior positives. Within a cluster, EngCoder prefers correction, human, reference, deterministic, and compiler/test evidence, then the shorter representative. The original records remain intact in the evidence pool.

## What the built-in answer engine is

A fresh installation starts with the **native foundation**. It uses deterministic local retrieval over the bundled curriculum and approved user learning. It can answer exact items such as greetings while the Neural Model History card is still 0 B. That does not mean a hidden model was trained, an AI server was contacted, or a connected AI was used.

A **trained model** exists only after a candidate has completed neural weight retraining, passed the English Gate and held-out checks, started successfully in a separate host, and was promoted. The working answer engine remains available until that entire process succeeds.

## Per-reply controls

- **Copy** copies only the selected reply.
- **Edit** creates a correction for the selected user message and reply.
- **Retry** reruns the selected request. Error cards can be retried but cannot be approved as answers.
- **Accept + learn** stores that exact message/reply pair once. Repeating approval adds no copies and no extra weight; paraphrasing one behavior also does not satisfy the multi-behavior admission gate.
- **Reject** keeps the bad draft as negative audit evidence so retrieval and later training can exclude it.
- **Learn?** previews the exact cleaned record before anything is stored.

**Include earlier chat in next reply** affects the next inference request only. Learning remains attached to the selected message/reply pair and its explicitly shown bounded context; it does not silently train on the entire visible window.

Corrections supersede older positive answers for the same normalized prompt. The older records remain visible as audit history but are not trainable. Error text, control receipts, route details, and local calculations are not learning examples.

## Automatic learning

**Live Auto Training** sits beside the per-reply controls and has its own saved setting. It queues successful conversations once, removes training-only noise, redacts private/nonpublic material, and waits for both authorized loopback reviewers. One reviewer may accept, revise, reject, or request evidence; the other confirms every acceptance. All outcomes remain in the Training Library. See [Live Auto Training](docs/live-auto-training.md).

When **Create verified practice examples while the app is idle** is enabled, EngCoder may ask connected teacher models for diverse examples. A separate verifier must accept an example before it can enter approved evidence. Eligible evidence can fill the same automatic neural microbatch. Duplicate examples coalesce, Chat has priority, and collection never switches the selected AI.

The two automatic controls are independent. Neither changes the foreground model or the no-fallback meaning of a manual model selection.

## Build English Foundation

Use this explicit one-time action for a deep English curriculum. Select public-domain, permissively licensed, or user-owned UTF-8 text/Markdown sources and record the correct rights category. Qwen creates competency maps and difficult long-context lessons; Nemotron creates fast conversational variants; each independently verifies the other's output. The action is resumable, yields to Chat, exposes every decision in the Training Library, and queues EngCoder's own full-weight internal candidate only after accepted document-held-out checks exist. Raw source files are never placed in weights or release packages.

This is not phrase-table installation. Accepted examples are tokenized and used to adjust EngCoder's numeric parameters. Exact approved phrase retrieval remains a separate verified-memory facility.

## Neural weight retraining

An independently verified microbatch queues neural retraining automatically. Choose **Compile data and retrain model** only when you want to force a smaller pending verified batch. EngCoder:

1. leaves the currently working answer engine immutable and available;
2. reads the append-only evidence pool without modifying it;
3. compiles a scope-bounded canonical curriculum under a supervised-token budget;
4. snapshots the newest eligible delta with a small coverage-balanced replay set;
5. checkpoints training atomically so a stopped run can resume;
6. evaluates it against separate held-out checks and English Gate v2;
7. starts the candidate in a separate host and runs health/canary checks;
8. promotes it only if every check passes and the parent generation is still active.

Jobs are persistent and serialized through queued, preparing, training, evaluating, promoting, rejected, completed, or recoverable-failed states. Duplicate jobs coalesce. Interactive answers keep priority, and a failed candidate never replaces the working engine.

The Small, Medium, and Large bootstrap values—24,000, 36,000, and 50,000 steps—are hard safety ceilings, not mandatory training lengths. The trainer checks all promotion gates only at completed-gradient boundaries that cover the entire selected corpus: after at least two such intervals for a new foundation and one for an already-trained expert. It stops at the first successful boundary. Reaching the ceiling without passing produces diagnostics and keeps the prior working engine. Foundation candidates default to a 131,072 supervised-byte-token curriculum budget. Incremental candidates default to 32,768 supervised tokens and at most 32 replay representatives; bounded environment overrides remain subject to hard limits.

Old `curriculum_weight` metadata is accepted for audit compatibility but removed from compiled rows. It no longer expands one record into multiple identical gradient sequences. A selected teaching is presented once; deliberate repetition, if needed, belongs to the trainer's explicit optimization schedule rather than hidden dataset duplication.

## Learning cleanup

Before hashing, storage, retrieval, or neural training, EngCoder:

- strips emoji, emoji modifiers, variation selectors, joiners, and keycap sequences;
- normalizes connected-model self-branding to EngCoder while preserving the true provider/model in audit metadata;
- rejects empty, structurally corrupt, repetitive, and error-card text;
- deduplicates normalized message/reply pairs and semantic behaviors, then caps repeated identical answer targets;
- keeps hard corrections authoritative over earlier approvals.

The Learn preview is the source of truth for what will be stored in the evidence pool. The candidate's `curriculum-selection.json` manifest is the source of truth for what was actually selected for weight training. It records source hashes, selected IDs, fresh-delta and replay IDs, coverage, supervised-token use, exclusions by reason, and the compiled-output hash.

Dataset minimization controls scope, repetition, training time, and memorization pressure. It does not change the byte size of an already fixed parameter-count model. Smaller tensor files require a smaller architecture, lower precision or quantization, pruning, or a different expert layout.

## Trusted JSONL import

Advanced users can import trusted data through the backup controls. Use one JSON object per line:

```json
{"skill":"english-core","domain":"english","messages":[{"role":"user","content":"Representative input"},{"role":"assistant","content":"Correct answer"}]}
```

or:

```json
{"skill":"cpp23-core","domain":"cpp23","prompt":"Representative request","response":"Correct answer"}
```

Imported data enters the evidence pool and still passes normalization, behavior clustering, bounded selection, candidate isolation, evaluation, and promotion. Do not import held-out checks into training data.

See [Minimal Curriculum Compiler](docs/minimal-curriculum-compiler.md) for the selection policy and manifest contract.
