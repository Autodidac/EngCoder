# Start Here — ThreadSpire 0.5.6

## 1. Build and launch once

Double-click `BUILD_AND_RUN.cmd`, or run it from PowerShell in the extracted `ThreadSpire` directory:

```powershell
.\BUILD_AND_RUN.cmd clean
```

It detects Visual Studio 2022 or 2026, performs a strict Release build, runs the tests, and launches ThreadSpire. The launcher closes an older running ThreadSpire instance before replacing it. Later runs may omit `clean` for an incremental rebuild:

```powershell
.\BUILD_AND_RUN.cmd
```

## 2. Launch it; do not perform a setup sequence

ThreadSpire now owns the complete internal-AI lifecycle. **Automatic setup and recovery** is enabled by default and persists across launches. It will:

1. create or repair the private Python runtime;
2. install and verify the pinned CUDA/PyTorch backend;
3. detect an existing internal generation;
4. resume bootstrap training from its exact checkpoint;
5. evaluate and promote the completed generation;
6. start the internal-model host; and
7. restart or retry failed stages with bounded backoff.

Manual setup and recovery controls are collapsed by default behind **Show advanced recovery controls**. They are not a required button sequence.

## 3. Read the live status instead of guessing

When bootstrap is incomplete, ThreadSpire opens directly on **Local AI**. That dashboard shows:

- the current lifecycle phase;
- overall bootstrap step and percentage;
- verified target-token count;
- elapsed time and measured ETA;
- worker heartbeat state; and
- the exact failure reason when a stage fails.

The ETA appears after enough work has completed to measure the current machine. A worker that stops reporting progress for 15 minutes is terminated automatically and resumed from its checkpoint instead of being left apparently running overnight.

The one-time English-first migration introduced in 0.5.4 quarantines legacy mixed/C++ internal weights under `%LOCALAPPDATA%\ThreadSpire\quarantine`. Once a clean generation such as `g000000` has been initialized, 0.5.6 keeps that generation and resumes its English bootstrap; it does not quarantine or recreate it again. Run `SHOW_MODEL_STORAGE.cmd` to open the exact model directory and lineage report.

## 4. Talk to the internal model

After the status reads **Ready**:

1. Open **Workspace**.
2. Set **AI source** to **ThreadSpire internal model**, or leave it on **Automatic**.
3. Enter a message and select **Send**.

The internal model begins from random weights created on this machine. It does not load Qwen, Llama, OpenAI, LM Studio, Ollama, or another pretrained checkpoint.

## 5. Let it improve

- **Accept + learn** admits a successful response as user-verified evidence.
- **Reject** stores negative evidence.
- Compiler/test-confirmed Coding Agent outcomes are stronger evidence.
- Background learning trains a shadow generation, yields to foreground inference, resumes exactly, evaluates, and promotes only a passing candidate.
- For the internal model, each independently admitted episode can queue a weight update immediately; same-skill work coalesces behind the active trainer.
- Connected local LLMs may generate or verify candidate examples, but their weights never become the ThreadSpire internal model.
- The internal model cannot teach or verify itself. Keep a capable external local LLM marked as **Teacher** and, preferably, a different model marked as **Verifier**.

Open **Learning**. Version 0.5.6 starts its continual-learning target on **English foundation — recommended first** rather than `cpp23-core`. C++ training remains available but is no longer silently selected. The target, automatic-generation switch, dataset, and weight-update command are independent and persist correctly.

For normal target-specific training:

1. Select the target.
2. Generate verified examples or import trusted JSONL.
3. Review the evidence and live pipeline report.
4. Queue the selected weight update.

**Stop current pass** ends evidence generation/import and pauses weight training at the next safe checkpoint. **Pause all background weight training** preserves resumable state. Disabling **Automatic verified examples while idle** does not change the selected target.

See [CUSTOM_TRAINING.md](CUSTOM_TRAINING.md) for the complete custom-skill and JSONL workflow.

Obvious repetitive or corrupt internal drafts are withheld and retried rather than dumped into the conversation. Automatic source mode can fall back to another connected local model. This is a structural corruption gate, not a guarantee that a fluent answer is factually correct.

Selecting **Stop** on the Local AI host controls intentionally disables automatic host recovery. Re-enable **Automatic setup and recovery** to return control to the lifecycle controller.
