# EngCoder Learning and Retraining

EngCoder keeps five kinds of data separate so the interface can say exactly what exists and what can affect an answer.

| Source | Purpose | Can change neural weights? |
|---|---|---|
| Bundled curriculum | The 4,247 curated examples shipped with EngCoder | When admitted into a verified shadow-training batch |
| Your learning | Replies you approve, edit, correct, or reject | Eligible under the diversity/evidence policy; the button can force a smaller pending batch |
| Live Auto Training | Cleaned live interactions reviewed by the authorized local Qwen and Nemotron pair | Accepted reusable behavior enters the normal verified microbatch automatically |
| Automatic teacher attempts | Separate seed/practice examples made by connected local AI apps while idle | Only after independent verification and admission |
| Held-out checks | The 320 fixed evaluation examples | Never; they are tests, not training data |

The Training Library shows these sources separately. Its total is an audit-record count, not a count of examples already baked into weights.

## What the built-in answer engine is

A fresh installation starts with the **native foundation**. It uses deterministic local retrieval over the bundled curriculum and approved user learning. It can answer exact items such as greetings while the Neural Model History card is still 0 B. That does not mean a hidden model was trained, an AI server was contacted, or a connected AI was used.

A **trained model** exists only after a candidate has completed neural weight retraining, passed the English Gate and held-out checks, started successfully in a separate host, and was promoted. The working answer engine remains available until that entire process succeeds.

## Per-reply controls

- **Copy** copies only the selected reply.
- **Edit** creates a correction for the selected user message and reply.
- **Retry** reruns the selected request. Error cards can be retried but cannot be approved as answers.
- **Accept + learn** stores that exact message/reply pair once. Repeating approval adds no copies and no extra weight.
- **Reject** keeps the bad draft as negative audit evidence so retrieval and later training can exclude it.
- **Learn?** previews the exact cleaned record before anything is stored.

**Include earlier chat in next reply** affects the next inference request only. Learning remains attached to the selected message/reply pair and its explicitly shown bounded context; it does not silently train on the entire visible window.

Corrections supersede older positive answers for the same normalized prompt. The older records remain visible as audit history but are not trainable. Error text, control receipts, route details, and local calculations are not learning examples.

## Automatic learning

**Live Auto Training** sits beside the per-reply controls and has its own saved setting. It queues successful conversations once, removes training-only noise, redacts private/nonpublic material, and waits for both authorized loopback reviewers. One reviewer may accept, revise, reject, or request evidence; the other confirms every acceptance. All outcomes remain in the Training Library. See [Live Auto Training](docs/live-auto-training.md).

When **Create verified practice examples while the app is idle** is enabled, EngCoder may ask connected teacher models for diverse examples. A separate verifier must accept an example before it can enter approved evidence. Eligible evidence can fill the same automatic neural microbatch. Duplicate examples coalesce, Chat has priority, and collection never switches the selected AI.

The two automatic controls are independent. Neither changes the foreground model or the no-fallback meaning of a manual model selection.

## Neural weight retraining

An independently verified microbatch queues neural retraining automatically. Choose **Compile data and retrain model** only when you want to force a smaller pending verified batch. EngCoder:

1. leaves the currently working answer engine immutable and available;
2. prepares a separate candidate from approved, deduplicated data;
3. checkpoints it atomically so a stopped run can resume;
4. evaluates it against held-out checks and English Gate v2;
5. starts the candidate in a separate host and runs health/canary checks;
6. promotes it only if every check passes and the parent generation is still active.

Jobs are persistent and serialized through queued, preparing, training, evaluating, promoting, rejected, completed, or recoverable-failed states. Duplicate jobs coalesce. Interactive answers keep priority, and a failed candidate never replaces the working engine.

Bootstrap targets are bounded at 24,000 steps for Small, 36,000 for Medium, and 50,000 for Large. Reaching the cap without passing produces diagnostics and keeps the prior working engine.

## Learning cleanup

Before hashing, storage, retrieval, or neural training, EngCoder:

- strips emoji, emoji modifiers, variation selectors, joiners, and keycap sequences;
- normalizes connected-model self-branding to EngCoder while preserving the true provider/model in audit metadata;
- rejects empty, structurally corrupt, repetitive, and error-card text;
- deduplicates normalized message/reply pairs;
- keeps hard corrections authoritative over earlier approvals.

The Learn preview is the source of truth for what will be stored.

## Trusted JSONL import

Advanced users can import trusted data through the backup controls. Use one JSON object per line:

```json
{"skill":"english-core","domain":"english","messages":[{"role":"user","content":"Representative input"},{"role":"assistant","content":"Correct answer"}]}
```

or:

```json
{"skill":"cpp23-core","domain":"cpp23","prompt":"Representative request","response":"Correct answer"}
```

Imported data still passes normalization, deduplication, candidate isolation, evaluation, and promotion. Do not import held-out checks into training data.
