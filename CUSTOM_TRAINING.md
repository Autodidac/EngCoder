# Custom Training in ThreadSpire 0.5.6

ThreadSpire separates four decisions that were previously mixed together:

1. **Training target** — the internal skill expert that receives verified evidence.
2. **Evidence source** — built-in seed data, externally generated teacher examples, or a trusted local JSONL file.
3. **Automatic generation** — whether ThreadSpire creates more verified examples while idle.
4. **Weight update** — the explicit shadow-training job that evaluates a candidate before promotion.

Changing one setting does not silently change the others.

## Recommended first run

Open **Learning** and leave **Training target** on **English foundation — recommended first**. The first launch of 0.5.6 migrates the old default away from `cpp23-core`. C++ specialization is no longer automatically selected by the skill bootstrap control.

To stop automatic example generation without losing the selected target, clear **Automatic verified examples while idle**. To stop an active evidence pass and pause weight work at its next safe checkpoint, select **Stop current pass**. Use **Pause all background weight training** when you want the existing candidate state preserved but no background optimizer work running.

## Generate target-specific examples

1. Select a target such as **English conversation** or **C++23 debugging**.
2. Set **Teacher examples per batch**.
3. Select **Generate verified examples**.
4. Review the evidence and pipeline report. Verified records automatically queue a shadow update.
5. Use **Queue/retry selected weight update** only to force or coalesce another update for that target.

Teacher-generated examples require a connected external local model marked as a teacher. A different external verifier is preferred. The ThreadSpire internal model is never allowed to teach or verify itself.

## Import trusted custom JSONL

1. Select **Custom skill ID** from **Training target**.
2. Enter a stable skill identifier, for example `epoch-render-architecture`.
3. Select **Open JSONL template** and save a working copy outside the installed program directory.
4. Put one complete JSON object on each line. Do not wrap the records in a JSON array.
5. Select **Browse**, choose the file, then select **Import trusted JSONL**.
6. Review the evidence count and pipeline state. Verified records automatically queue a shadow update; use **Queue/retry selected weight update** only when you need to force or retry it.

Imported trusted records bypass teacher generation, but they still enter the normal shadow-candidate, evaluation, promotion, and rollback pipeline. Only import answers you consider correct.

## Accepted JSONL shapes

The clearest shape is a message sequence:

```json
{"skill":"custom-skill","domain":"custom","messages":[{"role":"system","content":"Follow the skill rules."},{"role":"user","content":"Representative input"},{"role":"assistant","content":"Correct target response"}]}
```

A compact prompt/response record is also accepted:

```json
{"skill":"custom-skill","domain":"custom","prompt":"Representative input","response":"Correct target response"}
```

When importing through the Learning page, the currently selected target is authoritative. Keep every record in a file focused on the same skill. Mixing unrelated English and C++ examples in one custom file weakens the resulting expert and makes evaluation harder to interpret.

## What each control means

- **Training target**: Chooses the evidence bucket and expert update scope. Automatic mode does not change it.
- **Custom skill ID**: Names a new custom target when the target selector is set to Custom.
- **Teacher examples per batch**: Limits teacher-generated records in one pass. A trusted JSONL import reads the whole selected file.
- **Automatic verified examples while idle**: Persists across launches and repeatedly works only on the selected target.
- **Generate verified examples**: Uses the selected built-in target dataset plus an external teacher/verifier.
- **Import trusted JSONL**: Imports the selected file as trusted reference evidence without teacher generation.
- **Queue/retry selected weight update**: Manually starts or coalesces another shadow candidate; verified evidence already queues normal updates automatically.
- **Stop current pass**: Cancels current evidence generation/import and pauses active weight training at a safe checkpoint.
- **Pause all background weight training**: Preserves checkpoints while preventing background optimizer work.

## State and migration

The selected target is stored under `%LOCALAPPDATA%\ThreadSpire\config`. The automatic-training switch is stored separately, so disabling automation no longer resets the target. Dataset cursors are keyed by target and file, so switching between English, C++, and custom files does not reuse another target's position.

Do not delete `%LOCALAPPDATA%\ThreadSpire` when upgrading. The clean active generation, candidates, accepted evidence, and checkpoints are retained. Legacy mixed/C++ generations quarantined by the one-time English migration remain available for inspection but are not reused.
