## Async UI 1 — nonblocking inspector and generation updates

- EngCoder Lab is intentionally non-modal: a modal backdrop intercepts sidebar clicks while a native model request is pending. Keep the six theme palettes, Lab position and dismissal behavior.
- Do not await native model operations before opening a UI drawer; switch the `open` CSS state synchronously, then schedule heavier layout and panel render on `requestAnimationFrame`.
- Coalesce provider-fragment paints. Persist completed and in-flight assistant state; preserve the native event cursor. When replaying many events, yield every 128 records so the sidebar, Stop button, and Qwen Live remain interactive.
- Do not disable right-panel controls while a model loads. The local web server runs the model load on a request worker; browser layout must not be forced by polling/model load state.
- This does not prevent OS-level stalls from insufficient physical memory or GPU/driver resource contention; those need separate runtime admission controls.
- Preserve Qwen Live 3 retention, the 50% default and unchanged offload scale, and all 25 missions.

# EngCoder 0.7.1 — release maintenance and acceptance

**Qwen Live 3 hotfix:** Keep the previous Qwen output in the right panel through completion and through a new model request that has not emitted text yet. Start/finish events must not erase output; the next provider fragment replaces it. The Web and native server share this retention contract, including the recovery snapshot used after page refresh. See `VALIDATION.md`.

## Scope

- **Default:** Qwen native model GPU layer offload **50%** when newly created or reset. The slider remains **0–100%** on the existing capped model-layer budget; no scale, ceiling, `-ngl` mapping or GPU memory policy changes were made.
- **Preservation:** Saved profile percentages are authoritative. Previously chosen 99%, custom 95%, and all other user values should not be overwritten on startup or version change. Explicitly clicking Default or Full Reset applies 50%; saving requires the existing Apply/Reload interaction.
- **Version:** Display, CMake project, vcpkg manifest, training worker's server version and MCP client version are 0.7.1. Historical version entries in old release documentation remain untouched.
- **UI:** Output / Run info / Qwen Live tabs, idle retention, mission footer, theme parity and the agent routing hierarchy are preserved.

## Regression requirements

1. Fresh settings/model catalog starts with 50% offload; native GUI and Web UI show 50%, including each reset control. Verify 0% and 100% endpoints yield the same `-ngl` values as 0.6.9. For the existing 48-layer hint, the 50% default resolves to `-ngl 14`.
2. Import/restart with an existing 99% saved model profile. Verify the saved value survives, the model is not reloaded without user action, and the range remains 0–100%.
3. Run full `auto.bat` on Windows; all Catch2 tests must pass. Verify CMake/CTest and GUI identify 0.7.1.
4. Verify the native server's `/api/health` version, web footer label, and training worker's protocol version agree where exposed.
5. Exercise an internal Qwen generation, an OpenAI-compatible `/v1` client request and Qwen Live idle/refresh recovery. Confirm raw text remains and no diagnostic JSON appears in the clean Output tab.
6. Verify external 9B/27B model routing, retries, and the preserved 25-mission list.

## Audit findings

- **Fixed:** Previously mismatched release identifiers (`0.6.8` in vcpkg and training-worker paths vs `0.6.9` in UI/CMake). All current release IDs are 0.7.1.
- **Known limitations:** This environment lacks nlohmann_json and Windows toolchain, so the native Windows build and complete Catch2 run cannot be certified here. Multi-model resource certification and operating-system sandboxing remain open missions.
- **Deferred:** Any unrelated routing, GPU scaling, API contract or UI redesign. Do not change these as part of a default/version maintenance release.
