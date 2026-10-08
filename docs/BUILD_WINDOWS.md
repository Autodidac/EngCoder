# Windows build and run

From the repository root:

```powershell
./auto.bat
```

Default behavior is Release build -> CTest -> **EngCoder desktop**. `EngCoder.exe` owns its native runtime and loopback HTTP adapter and opens the same shared EngCoder frontend used by Web mode in an isolated app-mode Edge profile. No separately launched AI daemon is required.

```powershell
./auto.bat -Configuration Debug
./auto.bat -Client Gui       # shared Desktop/Web surface; default
./auto.bat -Client Cli       # embedded runtime
./auto.bat -Client Web       # standalone server + normal browser
./auto.bat -Client Server    # standalone headless server
./auto.bat -Client None      # build/test only
./auto.bat -NoTests
./auto.bat -NoRun
./auto.bat -UseExternalVcpkg
./auto.bat -KeepDependencies
```

Run `EngCoder.exe --native-ui` only for the diagnostic ImGui fallback.

The C++ project is `projects/native/`; generated Visual Studio files remain under repository-level `out/build/`. `auto.bat` stops previously recorded standalone EngCoder server processes before GUI/CLI launch and refuses to launch when CTest fails.

## DPI and multi-monitor behavior

The shared frontend uses browser DPI handling. The diagnostic ImGui fallback explicitly enables Windows Per-Monitor V2 awareness, SDL high-pixel-density rendering, and live rescaling when moved across displays with different DPI/4K scaling.

## Outputs

- `EngCoder.exe`
- `EngCoderCLI.exe`
- `EngCoderServer.exe`
- `engcoder_tests.exe` when tests are enabled

## LM Studio external subagents on Windows (0.6.9)

The host-only LM Studio endpoint is configured in the native EngCoder process. In PowerShell **before** launching `auto.bat`, set `$env:ENGCODER_MODEL_SERVER_URL='http://127.0.0.1:1234'` if different from default. Start LM Studio's local API server and load two different external models there. EngCoder's served Web UI and desktop UI use the same native backend and model assignments. Do not enter the LM Studio URL into the browser's hidden EngCoder endpoint. After launch, open **EngCoder Lab → Models → System 1 Architect + KeyMaker**, click **Refresh**, choose Small + Middle, and keep Qwen as KeyMaker. See [SUBAGENTS_QUICKSTART.md](SUBAGENTS_QUICKSTART.md) for expected routing and actual-stage verification.
