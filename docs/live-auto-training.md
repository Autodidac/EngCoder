# Live Auto Training

## Purpose

Live Auto Training turns ordinary conversation into conservative, visible training candidates without making the user approve every reply. It is not a second foreground answer route, a hidden fallback, or permission to copy connected-model weights.

It also is not the one-time long-form foundation builder. **Build English Foundation** uses explicitly admitted source documents to construct the deep starting curriculum; Live Auto continues afterward and learns reusable conversational behavior from actual interactions. See [Autonomous Curriculum and Training](autonomous-curriculum-and-training.md).

One routed AI answers the user. After that answer is complete and Chat is idle, two explicitly authorized local LM Studio models may inspect the cleaned interaction in the background:

- `qwen/qwen3.8-27b`
- `nvidia/nemotron-3-nano-4b`

Both must be exposed through an OpenAI-compatible endpoint whose host is exactly `127.0.0.1`, `localhost`, or `[::1]`. A remote endpoint, look-alike host, different protocol, or differently named model is not eligible for this workflow.

## User controls

The **Live Auto Training** checkbox is placed after the selected reply's manual controls. It has its own persisted setting, `config/live-auto-training.txt`.

The controls remain independent:

- **Copy**, **Edit**, and **Retry** operate on the selected reply.
- **Accept + learn** and **Reject** are explicit human overrides.
- **Learn?** previews the exact cleaned manual record.
- **Live Auto Training** queues future successful conversations for automatic review.
- **Create verified practice examples while the app is idle** is the older seed-curriculum generator. It uses `config/automatic-training.txt` and is not changed by the Live Auto checkbox.
- **Compile data and retrain model** forces a smaller pending verified batch when the normal microbatch has not filled.

Disabling Live Auto stops an active live review pass but does not disable idle practice generation, delete accepted evidence, erase checkpoints, or change the selected foreground AI.

## End-to-end state flow

```text
one foreground AI completes a reply
  -> reject errors, deterministic calculations, local control receipts, and repository receipts
  -> apply the training-only sanitizer
  -> redact and exclude probable private, credential, nonpublic, or machine-specific material
  -> deduplicate the cleaned prompt/answer pair
  -> append one candidate to candidates/live-interactions.jsonl
  -> wait for foreground work to become idle
  -> require both authorized loopback model families
  -> model A reviews: accept, revise, reject, or needs-evidence
  -> model B independently confirms every acceptance or revision
  -> append the complete decision to logs/live-training-attempts.jsonl
  -> accepted behavior enters the normal admission classifier once
  -> a diverse variation may be proposed and cross-checked
  -> eligible episodes increment the skill microbatch
  -> the persistent shadow trainer queues automatically at threshold
  -> candidate trains, evaluates, starts in isolation, and is promoted or rejected
```

The live ledger is monotonic and non-wrapping. Its cursor is stored separately under the `live-interaction` key. A processed conversation cannot cycle back around and silently add weight again.

## Review decisions

Every candidate ends in one of these visible states:

| State | Result |
|---|---|
| Accepted | Both local reviewers accepted the reusable behavior at the configured score gate. |
| Revised and accepted | One reviewer corrected a useful but weak answer and the other reviewer accepted the corrected standalone answer. |
| Needs evidence | Current information lacks a tool trace, or coding material lacks compiler and test evidence. It does not enter weights. |
| Retry pending | A required local reviewer was cancelled, failed, or returned malformed output. The interaction stays at the ledger cursor and is retried after a bounded backoff; it is neither accepted nor rejected. |
| Rejected | The behavior is wrong, brittle, parroting, branded, noisy, malformed, or otherwise not reusable. |
| Excluded | A pre-review boundary detected private, credential, nonpublic, machine-specific, error, or empty material. Sensitive text is redacted from the audit. |

Two models agreeing is not proof that a factual claim is true. Reviewers are instructed not to infer truth from model agreement.

## Evidence rules

### Stable conversational behavior

Natural English, useful instruction following, concise clarification, and broadly reusable conversational behavior can enter the independent-review path. When a request is genuinely ambiguous, asking one concise question is preferred over guessing.

### Current or obscure facts

Current information needs an actual tool/research trace or direct user confirmation. Without that evidence the item remains **Needs evidence**. EngCoder does not ship a hidden web browser; online research requires an explicitly connected MCP/tool provider. Personal facts and current transient state belong in user memory or retrieval, never shared weights.

### Mathematics

Exact calculations use the deterministic calculator and are recomputed when needed. They are not stored as phrase pairs and are never trained into weights. General mathematical explanation may be reviewed as durable language behavior, but an unchecked numeric answer is not evidence.

### Coding

Code text remains adaptable, but positive coding weight evidence requires compiler and test success. Source self-iteration is a separate repository transaction and never follows from this checkbox.

## Staged curriculum

Live Auto advances capability in measured stages:

1. **Conversation:** `english-conversation` is admitted first.
2. **Planning:** opens after conversation has at least 24 accepted episodes, at least one promoted scoped generation, and validation score at least 0.86.
3. **Coding:** opens after planning has at least 16 accepted episodes, at least one promoted scoped generation, and validation score at least 0.86. Coding records still require compiler and test evidence.

Other volatile or unsupported task types remain outside the live weight path. This keeps a small model focused instead of filling weights with rarely reused facts.

## Concurrency and foreground isolation

Foreground Chat always uses one routed or manually selected AI. Live Auto does not invoke a second foreground answer and does not alter manual no-fallback selection.

Background variation work is bounded to two authorized model families and up to four workers per model. A batch can therefore run at most eight audited attempts in parallel. Review and confirmation preserve model identity so a model cannot approve itself as the only judge.

Chat, media generation, and repository work have priority. The live pass waits while they run. Weight training checkpoints and yields at safe boundaries.

## Weight behavior and routing behavior

EngCoder trains weighted token relationships, not a filing cabinet of phrases. Exact approved phrases are also available immediately through verified retrieval memory, which is why a response can improve before a neural update exists.

Independently accepted reusable episodes are classified for `lora-incubation` and count toward the configured promotion microbatch. The current internal-base policy queues a scoped shadow update after eight eligible episodes. The button can request a smaller pending batch; it never edits the active generation in place.

Live review and neural optimization are separate queues. A verified weight update waiting for resources or an explicitly installed training capability does not stop Qwen and Nemotron from reviewing later conversations. If the optional Internal PyTorch Training pack is absent, accepted evidence remains stored and inspectable while the interface states plainly that neural weights remain at 0 B. EngCoder does not silently install the multi-gigabyte trainer.

Live review also feeds the existing routing-quality metric:

- an accepted original answer raises that source model's quality gradually;
- a revised original or a rejected reusable-behavior attempt lowers it;
- a needs-evidence result does not lower it.

Automatic routing can therefore favor EngCoder more often only after its observed answers improve. Manual model selection remains authoritative.

## What never enters shared weights

- permanent user memory, preferences, identity, addresses, contact data, credentials, or machine paths;
- private, confidential, proprietary, internal-only, or nonpublic material;
- unsupported current facts and transient tool state;
- provider names, provider self-branding, emoji, private reasoning, errors, and corrupt drafts;
- deterministic calculator results;
- rejected, excluded, superseded, or duplicate records;
- held-out checks;
- repository changes that lack compiler, tests, and the separate source-review transaction.

## Visibility and audit

The always-visible Activity strip shows queued live work, readiness of both authorized model families, the current review operation message, user-taught reply count, queued neural-weight jobs, and the last completed operation. Fast operations remain visible until another activity begins. A retry-pending review remains named in the strip instead of disappearing into a generic ready state.

The Training Library has a dedicated **Live Auto Training** source filter. It shows the full cleaned prompt and answer, decision, reason, source model, reviewer, confirmer, destination, and weight eligibility. Private exclusions show a redacted placeholder rather than the sensitive input.

Important persistent files:

| Path | Meaning |
|---|---|
| `config/live-auto-training.txt` | Live interaction automation preference. |
| `config/automatic-training.txt` | Separate idle seed/practice generation preference. |
| `candidates/live-interactions.jsonl` | Append-only cleaned interaction ledger. |
| `logs/live-training-attempts.jsonl` | Accepted, revised, needs-evidence, retry-pending, rejected, and excluded decisions. |
| `memory/episodes.jsonl` | Admitted retrieval and weight-eligible episodes. |
| `experts/registry/*.ini` | Per-skill accepted counts, pending microbatch count, validation, and generations. |
| `jobs/` and `candidates/` | Persistent shadow-training state and candidate artifacts. |

## Failure behavior

- Missing Qwen or Nemotron: the candidate remains queued and Activity names the missing family.
- Reviewer cancellation, endpoint error, or malformed JSON: the audit records **retry pending**, the ledger cursor does not advance, and the same interaction is retried after a 30-second backoff. It is not trained or counted as rejected.
- Revision without a second confirmer: rejected.
- Current fact without evidence: held as needs evidence.
- Coding record without compiler and tests: held as needs evidence.
- Duplicate interaction: not appended again.
- Neural trainer absent: accepted evidence and the pending weight update remain stored; Live Auto review continues.
- Weight candidate fails evaluation or host checks: the working model and active pointer remain unchanged.
- App restart: the ledger cursor, episodes, expert counters, jobs, and checkpoints resume from disk.

## Security boundary

The local-model allowlist is deliberately narrow. It validates the URL scheme and exact loopback authority, rejects embedded user information, requires the OpenAI-compatible protocol, and accepts only the two configured model-name families. Live Auto does not transmit candidates to a remote service.
