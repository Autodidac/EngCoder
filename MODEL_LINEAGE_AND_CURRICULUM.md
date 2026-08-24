# English Foundation lineage policy

Internal learned state is stored under `%LOCALAPPDATA%\ThreadSpire`, not in the source folder.
Run `SHOW_MODEL_STORAGE.cmd` to print the active report and open that directory.

On the first 0.5.4-or-later build or run, prior internal weights, checkpoints, optimizer/resume state,
pending candidates, and old curriculum progress are moved to
`%LOCALAPPDATA%\ThreadSpire\quarantine\legacy-mixed-<timestamp>`.
They are preserved for inspection but are **not reused**. The next startup creates fresh random
weights and bootstrap-trains only `english-foundation`.

The Python training boundary independently rewrites `all`, C++, and unknown targets to
`english-foundation` while the English gate is closed, and removes C++-shaped records from
JSONL evidence. The gate opens only when English-specific `evaluation_passed` and
`established` flags are both persisted. After that, selecting C++ works normally.
