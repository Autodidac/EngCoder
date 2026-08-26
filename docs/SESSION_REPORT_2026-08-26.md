# EngCoder unattended improvement session — 2026-08-26

## Initial repository state

- Permanent worktree: `C:\Users\iammi\.codex\worktrees\c2e6\ThreadSpire`
- Branch: `codex/threadspire-unified-workbench`
- Initial commit: `8d82dca` (`Fix failed reply retry and exact learned recall`)
- Initial Git status: clean
- Repository-local `AGENTS.md`: not present
- Other retained checkout: the original detached Downloads checkout at `ad94ad4`; it was not modified.

## Baseline build and tests

Official commands from `BUILDING.md`:

```powershell
cmake --fresh --preset windows-vs2022
cmake --build --preset windows-vs2022-release --clean-first -- /m:1
ctest --preset windows-vs2022-release
```

Baseline result on Visual Studio 2022 / MSVC 19.44.35227.0: clean Release build passed. CTest passed 4/4 (`threadspire_ai_tests`, `threadspire_workbench_tests`, `threadspire_base_footprint`, and `threadspire_curriculum_guard_smoke`).

## Problems discovered

### Arithmetic is routed and learned as conversation

`TaskAnalyzer` has no calculation task. A prompt such as `what's 1 + 1?` falls through to the short-conversation path. Accepting that reply can therefore create an `english-conversation` memory instead of executing and verifying an arithmetic operation. The active 22.5M-parameter internal model has English, C++23, debugging, and planning experts, but no mathematics expert.

Planned correction: a strict local arithmetic parser/evaluator, a truthful deterministic-calculation route that contacts no model, and a learning-admission rule that prevents calculator outputs from entering retrieval memory or weights.

### Verified local arithmetic implemented

- Added a bounded native arithmetic parser for `+`, `-`, `*`, `/`, `^`, unary signs, parentheses, decimals, and scientific notation.
- Added the `calculation` task class and a deterministic router stage. Arithmetic works with every AI disabled and never sends the prompt to an internal or connected model.
- Added clean chat labeling (`ENGCODER — VERIFIED CALCULATION`), removed fake model-speed reporting, and disabled Accept/Reject learning controls for calculated results.
- Local arithmetic does not stop automatic model setup or background teacher generation.
- Added a classifier-level hard boundary: calculator results are rejected from retrieval memory and model-weight training even if submitted through another code path.
- Added regressions for `what's 1 + 1?`, precedence, parentheses, right-associative exponentiation, division by zero, `What's up?` remaining conversation, execution with all models disabled, and learning rejection.

### Installed-size terminology hides the real dependency split

The installed internal PyTorch pack occupies 4,301.61 MiB; 4,188.04 MiB is `torch`, predominantly CUDA/cuDNN libraries. The active model weights are 85.95 MiB and the base executable is 4.80 MiB. Current built-in inference still launches the pack's Python `model_worker.py`, so the documented compact-native-inference boundary is not yet realized for the operational internal model.

### Model generation retention is expensive

Six full F32 generations occupy 515.74 MiB. The active generation is `g000011`; later candidates remain unpromoted. Safe cleanup must retain the active generation and a deliberate rollback policy rather than treating every generation as the current model.

## Verification record

This report is updated as changes are implemented. Claims about other platforms remain unverified until their configure/build/test paths are actually exercised.

- Incremental Visual Studio 2022 Release build after the arithmetic implementation: passed.
- Release CTest after the arithmetic implementation: 4/4 passed, including the expanded core suite and the 100 MiB base-footprint check.

## Work completed after the user handoff timestamp

### Storage, model, and RAM transparency

- Replaced repeated secondary directory walks with a single-pass classifier during the five-second storage sample. The UI no longer rescans the 17,000-file private runtime several times per refresh.
- Added exact accounting for active weights, every stored model generation, inactive candidate/rollback generations, the internal PyTorch pack, PyTorch itself, compiled CPU/GPU libraries, PyTorch program files, and private Python/support files.
- Parsed the promoted generation's real manifest and config through the existing JSON parser. The live report now shows the exact parameter count, completed steps, verified target-token count, context length, layer count, training-data focus, and learned areas.
- Changed the installed-pack card to say explicitly that the 4.20 GiB is training software, not model weights or learned knowledge. The active-model card now shows 85.95 MiB of weights and 22.5M parameters; all-model storage shows six retained versions separately.
- Preserved and visually verified EngCoder-owned live RAM reporting, including app RAM, helper-process RAM, private committed memory, current activity, growth, and the explicit exclusion of LM Studio and other unrelated AI applications.
- Brought the legacy storage declaration into parity with the named `threadspire.storage` module so stale declarations cannot silently disagree with module consumers.
- Added a synthetic exact-byte regression fixture covering Windows-style private-runtime paths, PyTorch library/code separation, active and inactive generations, manifest metadata, cards, and file totals.

### Autonomous visual QA and interface corrections

- Launched the built Release executable under a DPI-aware QA harness, captured Chat and Files & Activity at the minimum supported 980x680 logical size (1470x1020 at the active 150% display scale) and at 2100x1250, inspected the images, and closed only the spawned QA process.
- Removed the tab-strip overflow that created navigation arrows and hid the first tab after selecting Files & Activity. All five primary tabs now remain visible at the minimum size.
- Reclaimed a full row of conversation height at the minimum size by fitting per-reply controls into one responsive toolbar and reducing the compact prompt ceiling. The message area, prompt, learning controls, and bottom actions remain distinct and usable.
- Removed the storage report's horizontal scrollbar, enabled wrapping, and increased its share of the Files & Activity page while reserving usable space for the live log.
- Corrected the contradictory startup text. A trained model whose answer service is launching is now reported as `MODEL READY — answer service starting automatically`; it is no longer simultaneously called ready and not ready.
- Visually verified the dark card treatment, tab visibility, storage cards, model facts, live RAM, and startup wording at both tested sizes.

### Post-handoff verification

- Visual Studio 2022 Release build after the storage and first visual-QA pass: passed.
- Release CTest after the storage and first visual-QA pass: 4/4 passed.
- Final Visual Studio 2022 Release rebuild after the compact-toolbar polish: passed. Final Release CTest: 4/4 passed, including the base-footprint and curriculum-guard checks.
