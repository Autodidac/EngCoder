# ThreadSpire 0.5.6 repair

## Failure corrected

0.5.5 deployed `curriculum_guard.py`, but its global `argparse` wrapper replaced
`pathlib.Path` values such as `--seed-dataset` with plain strings. The internal
trainer then called `.is_file()` on one of those strings and automatic recovery
failed with:

```text
AttributeError: 'str' object has no attribute 'is_file'
```

## Why the displayed generation is not an old model

A status such as:

```text
Generation g000000 • initialized-untrained • 0 verified target tokens
• bootstrap 0/1200 incomplete • external base weights: none
```

is the clean random generation created after the one-time English migration. It
contains no downloaded base weights and has not completed any training. 0.5.6
keeps that generation and starts its English bootstrap; another reset is not
required.

The active model, checkpoints, curriculum state, and quarantine are stored under:

```text
%LOCALAPPDATA%\ThreadSpire
```

Run `show-model-storage.cmd` to open the directory and print the lineage report.

## Corrective design

- The guard no longer monkey-patches `argparse` globally.
- `model_worker.py` invokes the guard explicitly after parsing one command.
- Only `train-internal` is curriculum-gated; filesystem arguments on unrelated
  commands are untouched.
- Replacement datasets retain `pathlib.Path` type.
- The loader defensively accepts either `Path` or string path values.
- `english-foundation` is mapped to the concrete `english-core` expert.
- Bootstrap data, accumulated episodes, and held-out anchors are English-only
  while the gate is closed. The internal loader independently rejects explicit
  or code-shaped C++ records whenever the optimizer scope is English.
- A promoted English generation writes `state\curriculum-state.json`; C++ is
  unlocked only after English evaluation passes and the bootstrap target is
  complete.
