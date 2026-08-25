#!/usr/bin/env python3
"""End-to-end SSE smoke test for ThreadSpire's private internal-model host."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    arguments = parser.parse_args()
    source_root = arguments.source_root.resolve()
    sys.path.insert(0, str(source_root / "resources"))

    import internal_model as model  # pylint: disable=import-outside-toplevel

    temporary = Path(tempfile.mkdtemp(prefix="threadspire-server-smoke-"))
    process: subprocess.Popen[str] | None = None
    try:
        config = model.ModelConfig(
            name="ThreadSpire Server Smoke",
            context_length=128,
            d_model=64,
            n_layers=2,
            n_heads=4,
            ffn_hidden=128,
            expert_hidden=32,
        )
        model.initialize_model(temporary, config, force=True)
        original_readiness = model.evaluate_generation_readiness
        model.evaluate_generation_readiness = lambda *_args, **_kwargs: (
            model.GenerationReadiness(
                True,
                1.0,
                ({
                    "name": "server-smoke",
                    "passed": True,
                    "structural_quality": True,
                    "reason": "deterministic server smoke stub",
                    "output": "thread and checkpoint",
                },),
            )
        )
        trained = model.train_candidate(
            temporary,
            [source_root / "resources" / "bootstrap.jsonl"],
            [source_root / "resources" / "bootstrap_anchors.jsonl"],
            "english-foundation",
            2,
            3.0e-4,
            1,
            1,
            1,
            temporary / "control",
            False,
            bootstrap_target_steps=2,
            honor_inference_priority=False,
        )
        model.evaluate_generation_readiness = original_readiness
        assert trained["promoted_bool"] is True
        assert trained["training_scope"] == "all"
        assert model.status_payload(temporary)["ready"] is True

        ready = temporary / "state" / "server.ready"
        ready.parent.mkdir(parents=True, exist_ok=True)
        process = subprocess.Popen(
            [
                sys.executable,
                str(source_root / "resources" / "model_worker.py"),
                "serve",
                "--port",
                "0",
                "--ready-file",
                str(ready),
                "--state-root",
                str(temporary),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        for _ in range(200):
            if ready.is_file():
                break
            if process.poll() is not None:
                output = process.stdout.read() if process.stdout is not None else ""
                raise RuntimeError(f"model host exited early: {output}")
            time.sleep(0.025)
        if not ready.is_file():
            raise TimeoutError("model host did not publish its ready file")

        port = 0
        for line in ready.read_text(encoding="utf-8").splitlines():
            if line.startswith("port="):
                port = int(line[5:])
        assert port > 0

        body = json.dumps({
            "model": "threadspire://fast",
            "messages": [{"role": "user", "content": "Hello"}],
            "max_tokens": 8,
            "temperature": 0.0,
            "stream": True,
        }).encode("utf-8")
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/v1/chat/completions",
            data=body,
            headers={
                "Content-Type": "application/json",
                "Accept": "text/event-stream",
            },
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=60.0) as response:
            stream = response.read().decode("utf-8")
        assert "data:" in stream
        assert "[DONE]" in stream
        assert '"source": "threadspire-internal"' in stream
        assert '"external_base_weights": false' in stream

        shutdown = urllib.request.Request(
            f"http://127.0.0.1:{port}/shutdown",
            data=b"{}",
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(shutdown, timeout=5.0) as response:
            response.read()
        process.wait(timeout=10.0)
        process = None

        active_pointer = model.active_manifest_path(temporary)
        pointer_before = active_pointer.read_bytes()
        active_generation = model.resolve_active_generation(temporary).name
        shadow_generation = "g999999"
        active_directory = model.resolve_active_generation(temporary)
        shadow_directory = active_directory.parent / shadow_generation
        shutil.copytree(active_directory, shadow_directory)
        shadow_manifest_path = shadow_directory / "manifest.json"
        shadow_manifest = json.loads(shadow_manifest_path.read_text(encoding="utf-8"))
        shadow_manifest["generation"] = shadow_generation
        shadow_manifest["parent_generation"] = active_generation
        shadow_manifest["status"] = "trained-candidate"
        shadow_manifest["promoted"] = False
        shadow_manifest_path.write_text(
            json.dumps(shadow_manifest, indent=2) + "\n", encoding="utf-8"
        )

        shadow_ready = temporary / "state" / "shadow-server.ready"
        shadow_control = temporary / "shadow-control"
        process = subprocess.Popen(
            [
                sys.executable,
                str(source_root / "resources" / "model_worker.py"),
                "serve",
                "--port",
                "0",
                "--ready-file",
                str(shadow_ready),
                "--state-root",
                str(temporary),
                "--generation",
                shadow_generation,
                "--control-dir",
                str(shadow_control),
                "--max-loaded-models",
                "1",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        for _ in range(200):
            if shadow_ready.is_file():
                break
            if process.poll() is not None:
                output = process.stdout.read() if process.stdout is not None else ""
                raise RuntimeError(f"shadow model host exited early: {output}")
            time.sleep(0.025)
        if not shadow_ready.is_file():
            raise TimeoutError("shadow model host did not publish its ready file")
        shadow_port = next(
            int(line[5:])
            for line in shadow_ready.read_text(encoding="utf-8").splitlines()
            if line.startswith("port=")
        )
        with urllib.request.urlopen(
            f"http://127.0.0.1:{shadow_port}/health", timeout=10.0
        ) as response:
            health = json.loads(response.read().decode("utf-8"))
        assert health["shadow_candidate"] is True
        assert health["served_generation"] == shadow_generation

        shadow_body = json.dumps({
            "model": "threadspire://fast",
            "messages": [{"role": "user", "content": "Hello"}],
            "max_tokens": 8,
            "temperature": 0.0,
            "stream": False,
        }).encode("utf-8")
        shadow_request = urllib.request.Request(
            f"http://127.0.0.1:{shadow_port}/v1/chat/completions",
            data=shadow_body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(shadow_request, timeout=60.0) as response:
            shadow_result = json.loads(response.read().decode("utf-8"))
        assert shadow_result["generation"] == shadow_generation
        assert active_pointer.read_bytes() == pointer_before
        assert model.resolve_active_generation(temporary).name == active_generation
        assert shadow_control.is_dir()
        assert not (temporary / "control" / "inference.server").exists()

        shadow_shutdown = urllib.request.Request(
            f"http://127.0.0.1:{shadow_port}/shutdown",
            data=b"{}",
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(shadow_shutdown, timeout=5.0) as response:
            response.read()
        process.wait(timeout=10.0)
        print("ThreadSpire active and isolated shadow-model server smoke tests passed")
        return 0
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=5.0)
        shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
