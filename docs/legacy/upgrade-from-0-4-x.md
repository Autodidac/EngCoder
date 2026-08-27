# Upgrade from ThreadSpire 0.4.x

1. Extract this package to a new `ThreadSpire` source directory.
2. Run `build-and-run.cmd clean` once. The launcher closes an older running ThreadSpire instance, performs a strict Release build, runs the tests, and starts the replacement.
3. Do **not** delete `%LOCALAPPDATA%\ThreadSpire` and do not press the advanced setup buttons.

The English-first migration quarantines legacy mixed/C++ internal generations and checkpoints once, preserving them under `%LOCALAPPDATA%\ThreadSpire\quarantine` for inspection without reusing them. It then creates a clean random generation and trains only the English foundation. Upgrading from 0.5.4 or 0.5.5 to 0.5.6 keeps the already-clean generation and resumes it from the exact current state. If a stale `training.paused`, `inference.*`, or dead `training.lock` marker blocked an older build, foreground bootstrap removes it automatically.

When bootstrap is incomplete, the app opens directly on the dark Local AI progress dashboard. It shows the current phase, overall step/target, verified tokens, elapsed time, measured ETA, heartbeat, and exact failure. Manual recovery controls remain collapsed.

A worker that stops publishing heartbeats for 15 minutes is terminated and restarted from its latest exact checkpoint. Failed lifecycle stages retry indefinitely with bounded backoff. After bootstrap completes, the internal-model host starts automatically and is restarted after an unexpected exit.

## 0.5.6 behavior changes

- The automatic continual-learning target migrates once to English foundation. C++ specialization remains available but is no longer selected silently.
- Training target, automatic example generation, custom dataset selection, and weight-update queueing are independent controls.
- The Learning page now provides trusted custom JSONL import and a complete dark native-control theme.
- The native MSVC build now normalizes Win32 `LONG` text metrics and edit-rectangle dimensions before `std::max`, eliminating the C2672 GUI build failure at the scroll-preservation helper.
- Live conversation, Learning, and Logs updates no longer force the scrollbar away from the user's position. They follow the tail only when the view was already at the bottom.
- Trusted and independently verified teacher records now enter the Python internal trainer instead of stopping at the evidence ledger.
- Internal weight updates are queued immediately from newly admitted verified evidence and shown on the Learning page with progress and log details.
- The internal model is excluded from teacher and verifier selection, preventing self-reinforcing bad output.
- Obvious repetitive/corrupt internal drafts are blocked before display, retried once, and then rejected as low confidence. Automatic source mode may fall back to another connected model.
- During bootstrap, the UI now states that the host is intentionally paused for training and will resume automatically.

Do not delete the existing model, memory, expert, candidate, or checkpoint directories. The fixes operate on the current `%LOCALAPPDATA%\ThreadSpire` state.
