> Historical GPU scale migration record (0.6.5). **EngCoder 0.7.1 changes the default to 50% without changing the 0–100 control or its GPU-layer cap.** Existing saved profiles are preserved. Details below are historical.

# EngCoder 0.6.5 — GPU offload scale v9

**Latest user requirement:** new 100% equals the old 60% model-layer offload; default new 95% (old 57%).

| New setting | Old-scale equivalent | Effective `-ngl` for 48-layer hint |
| ---: | ---: | ---: |
| 0% | 0% | 0 |
| 30% | 18% | 9 |
| 60% | 36% | 17 |
| 95% (default) | 57% | 27 |
| 100% (maximum) | 60% | 29 |

The cap also applies when the exact override is `all` or a larger integer. The calculation is enforced in `effective_gpu_layers()` and in the Web UI's model card, with the same old-scale equivalence shown in the Windows diagnostic GUI. `gpu_offload_percent` is still a 0..100 integer representing the **new scale**. It controls GPU layer offload, **not GPU usage/VRAM percentage**, because KV, MoE cache and CUDA workspace allocations vary separately. The model layer count is derived from the configured layer hint; verify against llama.cpp's actual layer counts on the target GPU.

## Saved model profile migration

Revision 9 is applied once on app startup. Old saved custom percentages are scaled by `round(old * 100 / 60)` and capped at new 100. Old 55% becomes new 92%, so a customized model keeps approximately the same `-ngl` within the new ceiling; old 70% or 90% becomes 100% as those exceeded the new maximum. A recognizable untouched built-in 36% profile adopts the new 95% default. Other custom model fields are retained. The next app restart sees revision 9 and does not migrate twice.

The settings form, per-setting Default control, full reset, C++ configuration default, native GUI form, runtime JSON fallback, and model registration defaults all use 95%. Explicit **Reload / Apply settings** is still required for changes to affect a running model. Edits do not trigger automatic model restarts.

## Verification

- Web JavaScript syntax, Python training-worker syntax, and eight JavaScript GPU-target mapping checks: passed in this sandbox.
- C++ Catch2 tests updated and migration regression tests added, but **not executed here**. CMake cannot configure without the project's `nlohmann_json` package (use the pinned Windows vcpkg environment).
- Qwen GGUF reload, actual GPU layer residency, Windows GUI/Edge scrolling and More-button visual checks are **not live-verified in this sandbox**.

For remaining objectives and acceptance criteria, see `AUDIT_REMAINING.md`.
