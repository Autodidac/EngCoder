# EngCoder native runtime

The native project is C++23 and owns the authoritative runtime: Architect routing, KeyMaker/addon selection, tools, policy, persistence, residency, learning, training scheduling, GUI/CLI, and optional HTTP serving.

The native namespace and targets use the EngCoder product naming directly.

Key services:

- `controller::ArchitectController`
- `addons::AddonRegistry`
- `models::ModelRegistry`
- `residency::ModelResidencyManager`
- `model::InternalModelHost`
- `training::TrainingService`
- `learning::LearningService`
- `agent::AgentRuntime`
- `runtime::RuntimeHost`

The runtime recognizes three distinct model classes: internal `engcoder://...` components, native `qwenflash://...` foundation profiles, and external provider model IDs. A Qwen Flash foundation enters the same AgentRuntime/Architect/addon/tool/memory harness; only its inference/residency driver differs. External providers remain usable as fallbacks/teachers.

Configuration uses `ENGCODER_*` environment variables. Important values include `ENGCODER_MODEL`, `ENGCODER_ARCHITECT_MODEL`, `ENGCODER_VRAM_BUDGET_GIB`, `ENGCODER_MAX_RESIDENT_ADDONS`, `ENGCODER_TRAINING_ENABLED`, and `ENGCODER_TRAINING_PREEMPT_FOR_INFERENCE`. Qwen Flash defaults are exposed through `ENGCODER_QWEN_FLASH_*` variables, including context, slots, compaction, KV placement, expert-cache budgets, batching, load/lazy modes, and runtime port.

## Native Model Library (0.6+)

Native downloadable runtimes are separate from both internal KeyMaker weights and external LM Studio/Bionic providers. `qwenflash://` profiles are managed by `NativeModelManager`; `QwenFlashRuntime` owns the sparse/MoE backend lifecycle and expert-cache policy. See `../../docs/MODEL_LIBRARY.md`.
