# Qwen Flash native runtime

EngCoder treats Qwen Flash as a first-class native model runtime, not as an external OpenAI provider.

Pinned inference backend:

- repository: `GenerelSchwerz/llama.cpp`
- branch: `moe-cache`
- commit: `167742d9dfa01306786e7d3cfd31fcb8a8b185dc`
- verified build recipe: `qfr-vcpkg-2026.07.29`
- pinned vcpkg baseline: `9e593bb18ea69cc5095e012465dcd675a822ed0d`
- model transport: GGUF, local file or Hugging Face catalog/download

## Runtime ownership and repair

EngCoder keeps its own runtime under the application state directory:

```text
runtimes/qwen-flash/bin
runtimes/qwen-flash/runtime.json
```

The manifest records the backend commit and build recipe. A verified EngCoder runtime is preferred at launch.

If the earlier standalone QwenFlashRuntime development build is present, EngCoder looks for the exact pinned, known-good build path (`build-167742d9dfa0/bin/Release` on Windows) and can copy that runtime into its own state directory. It never moves, modifies, or deletes the standalone QFR files. This also repairs unverified 0.6.0-0.6.2 runtime copies without damaging either project.

On a clean machine, **Install runtime** builds the same pinned backend recipe used by the proven QFR runtime. Windows and Linux both bootstrap the pinned vcpkg baseline and validate the official verification-only `cuda` port before configuring llama.cpp through the vcpkg toolchain.

A compatible runtime can still be supplied explicitly with `ENGCODER_QWEN_FLASH_SERVER`.

## Model cache

Model weights are separate from the runtime. EngCoder honors the standard/shared llama.cpp/Hugging Face cache environment (`LLAMA_CACHE`, `HF_HUB_CACHE`, `HUGGINGFACE_HUB_CACHE`, `HF_HOME`) and `ENGCODER_MODEL_CACHE_ROOT`.

When a Hugging Face GGUF is already fully present, EngCoder resolves the complete split set and launches the first local GGUF shard with `-m`. This avoids re-entering repository/download resolution during ordinary model startup. If the cache is incomplete, EngCoder continues to use `-hf` so llama.cpp can finish the download.

## Startup recovery

Recommended/Automatic mode preserves the requested profile first. If initialization exits before `/health` becomes ready, EngCoder uses a bounded diagnostic/recovery ladder rather than retrying the same context twice:

1. 77,824-token hardware-aware profile,
2. 65,536-token diagnostic profile,
3. 32,768-token proven-QFR compatibility profile.

Manual mode is never silently changed. The active recovered context is reported to the EngCoder harness so context compaction and output budgeting use the real runtime window.
