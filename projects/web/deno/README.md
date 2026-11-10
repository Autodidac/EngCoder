# EngCoder Deno development adapter

This optional adapter serves the EngCoder Web UI during development and proxies `/health` and `/api/*` to the native loopback runtime. It does not host a model, run an agent, or own model-provider policy.

The normal native application does not require Deno.
