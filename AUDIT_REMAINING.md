# EngCoder 0.6.8 — audit and carry-forward

See [new objective register](docs/FUTURE_MISSIONS.md) for P0–P2 missions and test acceptance, and [development guide](docs/DEVELOPMENT_0.6.8.md) for architecture decisions and live diagnostic steps. The original 0.6.5 audit remains in `docs/BASELINE_2026-09-19.md` and earlier release artifacts. Source implementation does **not** imply hardware/runtime certification.

**Preserved 0.6.6 source-level additions:** bounded raw event output in right panel; streamed browser checkpoints/refresh resume; conservative routing; externally assigned small responder and LM Studio advisory addons; deterministic fallback + trainable numeric shadow; early MTP draft validation; C++ intent/predictor test fixture.

**Uncompleted:** genuine OS process sandbox and remote authenticated control plane; trained JEPA/JEV controller; real backend GGUF architecture qualification; browser-independent transcript persistence; tokenizer-ID stream; Windows native build/runtime checks; training-ban full enforcement; verified external subagent orchestration and CI.

**0.6.8 corrections:** classify bare greetings correctly; allow small-response routing independent of addon advisory routing; explain empty/unavailable small-model assignments; suppress empty attachment counter; keep Qwen GPU saved settings and all 24 unfinished missions. The Windows binary/CUDA acceptance still requires testing.


### 0.6.8 progress

Implemented bounded specialist → optional reviewer → Qwen verification, model-aware prompt analysis modes, safe small-response fallback, checkpoint path binding, trainer denylist checks, and remote bearer gating. Existing 24 missions remain open to their full acceptance criteria in `docs/FUTURE_MISSIONS.md`, especially Windows isolation, MTP hardware qualification, concurrency, permanent SQLite mission queue, durable server transcripts, and vision.

### 0.6.9 verification carry-forward

Role-aware Retry, immutable KeyMaker/addon delegation boundaries, separated Run info and generated Output, theme parity, and conservative math routing have source-level changes and offline tests. **Not certified:** full Windows MSVC/CTest; live three-model residency and resource budgeting with simultaneous LM Studio/Qwen; GPU oversubscription/recovery; Windows OS sandbox/confinement; end-to-end served authentication and refresh; MTP draft compatibility; independent-model capability detection. Keep the canonical 24-mission register intact until each full acceptance criterion is verified.
