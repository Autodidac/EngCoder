# EngCoder three-model setup (0.6.9 maintenance)

## What the models do

- **System 1 Architect** classifies and routes incoming tasks. An optional external controller suggests a capability; when it cannot respond with a valid decision, the deterministic native policy takes over. Architect is **not** the model providing the final reply.
- **Small response/specialist** is an external OpenAI-compatible model (for example a loaded LM Studio model). It answers clearly safe greetings/short text directly. On genuinely complex coding/planning work it may produce a *short, JSON-validated work handoff* for KeyMaker; it never provides authoritative code or tool use. Routine C++23 examples do not automatically run extra specialists. A dedicated specialist model assignment overrides this fallback.
- **Middle reviewer** is a different external model. It reviews the small model's checklist and/or selected specialist's advice. It cannot run tools or override KeyMaker; any unexpected provider tool-call response is discarded.
- **KeyMaker Foundation** is the selected large foundation model (often the internal Qwen Flash runtime). It solves math, security and sensitive questions directly, and handles final complex answers and tool requests regardless of any advisory handoff.

A math question will often involve **only KeyMaker**. That is intentional. Stage selection is based on reliability, not a guarantee that every request uses all resident models. Model-based Architect fallback is independent of specialist or reviewer outcomes.

## Five-minute setup — desktop and served browser

1. Start EngCoder with the existing working native Qwen model. In **EngCoder Lab → Models → System 1 Architect + KeyMaker**, leave **KeyMaker Foundation** on Qwen.
2. In **LM Studio on the machine running the EngCoder server**, download/load a small model (e.g., 9B) and a *different* middle model (e.g., 27B). Run LM Studio's local API server, commonly on `http://127.0.0.1:1234`. LM Studio owns those model weights; EngCoder does not automatically download or start these external models.
3. If LM Studio uses another address, set `ENGCODER_MODEL_SERVER_URL` before starting EngCoder (example in PowerShell: `$env:ENGCODER_MODEL_SERVER_URL='http://127.0.0.1:1234'`). Restart EngCoder and use **Refresh** under Architect + KeyMaker. The browser's hidden EngCoder endpoint is **not** the LM Studio configuration setting. EngCoder communicates with LM Studio from the server, not from the served browser.
4. Choose the small model and an independent middle model; or press **Assign small + middle** to fill those two dropdowns from discovered external models. The button does not replace KeyMaker. Assign an external Architect model only if you want model-proposed routing; otherwise native deterministic routing still works. Keep **System 1 Architect automatic routing** enabled. Keep **Enable capability addons** on for specialist checklists and middle-tier handoffs. Switching addons off skips these consultations; it does **not** prevent safe simple-response routing.
5. Send `hi`: the small responder should be selected. Send `Refactor the Vulkan renderer for multithreading`: small specialist checklist → middle reviewer (if configured and enabled) → KeyMaker final answer. Send `What is 12% of 180?`: KeyMaker should handle it alone. Open the **Run info** tab to see actual completed stages; the **Output** tab remains generated text only.

## Settings and failure behavior

- **Multi-model verification:** Automatic = bounded middle review; Deep = larger review output/time budget; Off = no middle review. Deep analysis on KeyMaker is a prompt-level preference, not an invented inference-provider flag.
- A preparatory model must return only `{"handoff":["short actionable check", "another check"]}` (one to five checks). Free-form replies, source code, leaked reasoning, tool calls, empty messages and output truncated by token limits are rejected and never forwarded as handoff. No automatic second attempt is made with the same tiny model. The original user prompt is always passed intact to KeyMaker.
- A missing or failed small/middle model does not automatically steal the foundation role. EngCoder continues on KeyMaker when safe. The Run info panel distinguishes *attempted*, *completed*, and *failed* operations.
- A dedicated C++/planning/docs addon assignment takes precedence. If no dedicated model is assigned to that addon, a configured external small responder can produce a limited checklist for a non-sensitive task, so three loaded models can be sufficient. Multiple skills do not trigger redundant small checklist calls on the same request, even after a failed preflight.
- **Served Web UI** uses the same backend router at port `14321`; external models still run wherever LM Studio is hosted, and the browser cannot make the backend select arbitrary local client-side weights. Remote EngCoder access needs its authentication configuration and should not be exposed publicly without network protection.
- **GPU layer offload** is only for EngCoder's native Qwen profile. `100%` means 100% of its configured safe GPU-layer budget, not 100% of GPU memory. Do not adjust model profiles just to configure LM Studio models.
- **MTP** is not the subagent mechanism. Remote LM Studio models are not compatible cross-process MTP draft weights. Only verified local draft GGUF combinations are eligible; unsupported options must stay unavailable.

## Verification checklist

- Confirm model discovery shows both external IDs and no role duplicates.
- Confirm `controller.model_selected` reports small for greetings and KeyMaker for math/coding final output.
- Confirm `addon.completed` / `handoff.draft_completed`, then `review.completed`, then `model.request` for KeyMaker on a qualifying complex task. If an intermediate step failed, confirm the Run info panel labels it as failed rather than completed.
- Confirm refresh/retry keeps the final responding model role and does not silently make the small model authoritative.
- Run `auto.bat` and all native CTest tests on Windows; Linux source-only checks cannot certify Windows deployment or live LM Studio behavior.

For a request such as **"show me a basic cpp23 app but don't write any files just display it"**, KeyMaker responds directly without wasting 9B/27B calls. The final responder, not the advisory model, must honor the no-write requirement.
