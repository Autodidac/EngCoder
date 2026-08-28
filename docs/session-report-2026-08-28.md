# EngCoder UI and Activity Verification — 2026-08-28

## Scope

This pass responds to the supplied Workspace and Memory & Learning captures in which Send or Retry could leave labels, selectors, status text, and the prompt row partially painted over. It also makes current work visible from every page.

No Epoch source, checkout, process, or application data was touched. EngCoder application data was preserved. The Site task received coordination instructions but no public EngCoder record was changed or published.

## Repairs

- Removed visible owner-drawn card child windows from Workspace and Capabilities. Those windows could repaint after sibling controls and cover them.
- Kept hidden geometry markers and moved rounded card rendering into each clipped page host's paint pass. Cards now remain behind native child controls by construction.
- Added subtle card accent rails and consistent 12-DPI-scaled-pixel inner spacing around the session row, AI selectors, conversation, per-reply review controls, composer, action buttons, and activity sidebar.
- Closed the layout transaction before showing the active page. Send, Retry, source changes, tab changes, and resize can no longer expose a frame whose controls still have their previous bounds.
- Split the previously clipped neural-training row into a short **Train neural weights** heading with the explicit **Compile data and retrain model** action and a full-width plain-language explanation.

## Always-visible activity

The new two-line strip remains visible below every page and refreshes immediately after commands and every periodic state update. It reports:

- message answering;
- repository work;
- image/audio generation;
- AI discovery;
- optional tool connection;
- add-on changes;
- safe storage cleanup;
- built-in file preparation, checking, startup, and bounded candidate-training progress/ETA;
- verified-practice-example creation;
- persistent test-model retraining and queued improvements;
- disk/RAM measurement;
- automatic retry countdowns and bounded quality-limit stops;
- built-in/connected AI readiness while idle.

When training is active, the strip states that the working AI remains available and only the isolated candidate changes. Foreground work states that background learning is waiting.

## Verification

- Clean Visual Studio 2022 Release build: passed.
- Debug CTest: 5/5 passed.
- Release CTest: 5/5 passed.
- Release footprint check: 5,312,512 bytes (5.06 MiB), below the 100 MiB base-executable budget.
- Release executable: `out/build/windows-vs2022/bin/Release/EngCoder.exe`.

The Windows Computer Use visual harness was attempted with its required retry/reset sequence, but its Node kernel could not start because the host sandbox failed while applying deny-read ACLs. This was a host automation failure, not an EngCoder process failure. No fresh automated screenshot is claimed for this pass; the code, strict warning-as-error builds, regression contracts, workbench tests, inference contract, curriculum guard, and footprint gate were verified.
