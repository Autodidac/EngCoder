# Autonomous Curriculum and Training

## Purpose

EngCoder 0.9.0 separates four operations that had previously been easy to confuse:

1. **Build English Foundation** is an explicit, one-time, resumable curriculum build. Qwen designs the deep curriculum, Nemotron supplies breadth, and each model verifies material produced by the other.
2. **Live Auto Training** continuously reviews successful conversations and turns reusable behavior into verified microbatches.
3. **Compile data and retrain model** manually requests a shadow neural-weight job from already admitted evidence.
4. **Software self-iteration** changes application source in an isolated repository transaction. It is not model training and is outside this curriculum pipeline.

None of these operations imports Qwen or Nemotron weights. The teachers produce evidence. The optional Internal PyTorch Training pack uses that evidence to optimize EngCoder's own compact transformer.

## What is learned

A modern language model does not normally store a phrase table as its intelligence. Text is tokenized and training adjusts numeric parameters so that useful token relationships become more likely in context. EngCoder's internal tokenizer has fixed structural tokens plus UTF-8 byte tokens, so it can represent English and source code without a downloaded vocabulary.

EngCoder also keeps an exact verified-memory layer. That layer deliberately stores approved prompt/answer records for immediate retrieval. It is separate from neural weights and remains useful when the trainer is absent or neural weights are still 0 B. The interface and Training Library distinguish:

- bundled examples;
- exact verified memory;
- generated foundation curriculum;
- Live Auto decisions;
- held-out evaluation checks;
- queued weight jobs;
- trained neural-weight generations;
- rejected and retry-pending audit material.

## One-time foundation build

The Learning page's **Build English Foundation** section requires an explicit user action. Its source picker accepts UTF-8 text and Markdown only when the user marks the batch as public domain, permissively licensed, or material they own or have permission to use.

For every source EngCoder records:

- title and local origin;
- rights category and redistribution status;
- stable content hash;
- ingestion time;
- section/chapter boundaries and source spans.

Source text is normalized and deduplicated. Documents are divided by chapter or section, and entire sections are assigned to training or held-out partitions before generated examples exist. This prevents a paragraph from one chapter entering training while a near-identical paragraph from the same chapter is used as evaluation.

Raw books and documents remain under the local application-data curriculum directory. They are not copied into model release packages. A source may be redistributed only when its recorded license permits it; the current foundation release process does not package raw sources at all.

## Teacher roles

Qwen 3.8 27B is the deep curriculum architect. For each source section it produces a competency map and a difficult lesson covering such work as explanation, multi-turn instruction following, summarization, reasoning, and long-context reading. Its long-context concurrency is normally one and may be increased only when measured resources allow it.

Nemotron 3 Nano 4B is the high-throughput generator. It produces one to four short and medium variants per training lesson. Its concurrency adapts from one to four according to CPU use, available RAM, available GPU memory, and thermal pressure.

The generator cannot be its own independent verifier:

- Nemotron-generated records are verified by Qwen.
- Qwen-generated records are verified by Nemotron.
- a useful weak record may be revised once, after which a model independent of the reviser must confirm it;
- an unchanged live answer needs one review by a model independent of its author; it is never sent back to its author for a redundant second vote;
- endpoint failure, cancellation, or malformed review remains **retry pending** at the durable chunk cursor;
- a completed negative review remains a visible rejection.

Admission also checks source grounding, rights, deduplication, EngCoder identity, privacy, formatting, and copied source spans. Personal information, credentials, private or nonpublic facts, volatile current facts, provider branding, emoji noise, error text, copied passages, and low-value trivia cannot become weight evidence.

## Durable files and audit

Foundation state lives under `%LOCALAPPDATA%\EngCoder\curriculum`:

| File | Purpose |
|---|---|
| `sources.jsonl` | admitted source provenance and rights |
| `chunks.jsonl` | document-level split and semantic chunk manifest |
| `competency-map.jsonl` | Qwen lesson specifications |
| `foundation-attempts.jsonl` | every generated, revised, accepted, rejected, and retry-pending record |
| `foundation-training.jsonl` | accepted train-partition material |
| `foundation-heldout.jsonl` | accepted document-held-out material |
| `foundation-evaluation.jsonl` | fixed anchors plus generated document holdouts |
| `foundation-state.json` | operation, cursor, counts, tokens, checkpoint, candidate, and promotion state |

The attempt ledger records cleaned prompt and answer, exact source span, generator, verifier, decision, revision state, rejection reason, weight eligibility, and candidate generation. Resuming rewrites deterministic source/chunk manifests instead of duplicating them and continues from the durable chunk boundary.

## Neural training and promotion

The **Internal PyTorch Training** pack is EngCoder's custom-model trainer. The separate Transformers/PEFT pack is only for explicitly selected compatible external bases; it is never described as the internal trainer.

The first English foundation candidate:

1. compiles accepted training records into token sequences;
2. combines all 320 bundled anchors with independently accepted document-held-out records;
3. loads a random-initialized or repair-generation EngCoder model;
4. trains the complete shared backbone and experts in an isolated candidate;
5. checkpoints the model, optimizer, pending gradients, data cursor, and random state;
6. yields at a safe boundary whenever Chat requests resources;
7. evaluates every anchor record, not a small sample;
8. requires loss, structural-output, identity, privacy, and deterministic readiness gates;
9. saves a rejected or repair candidate without touching the active generation when any gate fails;
10. atomically promotes only a passing candidate whose parent is still current.

After the foundation is promoted, shared weights are frozen by default and bounded skill experts receive verified microbatches. This is scoped full-weight expert training, not LoRA. Qwen returns for an explicit rebuild, measured coverage gap, or difficult long-context lesson; Nemotron remains the normal conversational generator.

### Measured conversational gaps

Live Auto does not wake Qwen merely because one answer was rejected. An independent reviewer must classify a completed rejection as a reusable conversational coverage gap; privacy, identity, unsupported/current facts, junk, and formatting failures are excluded. EngCoder deduplicates the failed prompt/answer into a separate negative-evidence ledger. Once three unprocessed gaps exist, Qwen proposes repairs one at a time and Nemotron verifies them. Only the independently verified replacement enters normal admission. The failed answer itself never becomes trusted memory or positive weight data. The gap ledger and cursor survive restart independently of live-interaction and seed-curriculum cursors.

The base application can still show 0 B neural weights. That means no candidate has passed and been promoted. Verified memory and curriculum evidence may exist, and the native retrieval foundation may answer known prompts, but the interface must not call that a trained LLM.

## Progression and routing

Conversation is the first curriculum. Planning opens only after conversation has enough accepted evidence, a promoted scoped generation, and its validation threshold. Coding opens only after planning reaches the corresponding threshold; positive coding evidence also requires compiler and test results.

Automatic routing never favors EngCoder merely because a model file exists. It accumulates accepted outcome evidence and still applies response-quality gates. Exact manual model selection remains authoritative and never enables silent fallback.

## Interface contract

The foundation panel always exposes:

- current operation and durable stage;
- teacher and verifier;
- active source span;
- generated, accepted, revised, rejected, retry-pending, and held-out counts;
- source and chunk progress;
- input and output token counts;
- queued neural jobs;
- checkpoint and active candidate;
- current promoted neural-weight size;
- promotion result.

Build, Pause, Resume, and Rebuild are explicit. A Chat request pauses the foundation at a safe boundary and arms automatic resume. A user-initiated Pause stays paused. Live Auto remains an independent checkbox next to the manual per-reply controls and continues after the foundation build.

## Failure rules

- Missing or identical teachers: do not start.
- Unsupported rights: reject the source before ingestion.
- Partial bounded pass: pause at its checkpoint; never queue an incomplete curriculum.
- Missing independent review: retry pending; never accept by timeout.
- No accepted document holdouts: do not queue weights.
- Missing internal trainer: preserve evidence and show the pending job while weights remain 0 B.
- Measured-gap teacher/reviewer failure: leave that bounded batch retry-pending and keep its durable cursor; never treat the failed answer as a reference.
- Failed evaluation or host check: preserve the current working model and retain the candidate/audit result.
- Foreground Chat: checkpoint and yield; resume the same candidate afterward.
