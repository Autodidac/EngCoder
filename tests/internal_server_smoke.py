#!/usr/bin/env python3
"""End-to-end SSE smoke test for EngCoder's private internal-model host."""
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


def native_completion(native_host: Path, state_root: Path) -> dict[str, object]:
    completed = subprocess.run(
        [
            str(native_host),
            "--state-root",
            str(state_root),
            "--self-test",
            "--prompt",
            "Hello",
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=30.0,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "native host self-test failed: "
            f"stdout={completed.stdout!r}, stderr={completed.stderr!r}"
        )
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("native host self-test returned no completion")
    return json.loads(lines[-1])


def native_health(native_host: Path, state_root: Path) -> dict[str, object]:
    ready_root = Path(tempfile.mkdtemp(prefix="threadspire-native-health-"))
    ready_file = ready_root / "server.ready"
    process = subprocess.Popen(
        [
            str(native_host),
            "--state-root",
            str(state_root),
            "--port",
            "0",
            "--ready-file",
            str(ready_file),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    port = 0
    try:
        for _ in range(400):
            if ready_file.is_file():
                port = next(
                    (
                        int(line[5:])
                        for line in ready_file.read_text(encoding="utf-8").splitlines()
                        if line.startswith("port=")
                    ),
                    0,
                )
                if port > 0:
                    break
            if process.poll() is not None:
                output = process.stdout.read() if process.stdout is not None else ""
                raise RuntimeError(f"native host exited before health check: {output}")
            time.sleep(0.025)
        if port <= 0:
            raise TimeoutError("native host did not publish a usable health-check port")
        with urllib.request.urlopen(
            f"http://127.0.0.1:{port}/health", timeout=10.0
        ) as response:
            return json.loads(response.read().decode("utf-8"))
    finally:
        if process.poll() is None and port > 0:
            try:
                shutdown = urllib.request.Request(
                    f"http://127.0.0.1:{port}/shutdown",
                    data=b"{}",
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                with urllib.request.urlopen(shutdown, timeout=5.0) as response:
                    response.read()
                process.wait(timeout=10.0)
            except (OSError, subprocess.TimeoutExpired):
                process.kill()
                process.wait(timeout=5.0)
        elif process.poll() is None:
            process.kill()
            process.wait(timeout=5.0)
        shutil.rmtree(ready_root, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--native-host", type=Path)
    arguments = parser.parse_args()
    source_root = arguments.source_root.resolve()
    native_host = arguments.native_host.resolve() if arguments.native_host else None
    if native_host is not None and not native_host.is_file():
        raise FileNotFoundError(f"native host executable is missing: {native_host}")
    sys.path.insert(0, str(source_root / "resources"))

    import internal_model as model  # pylint: disable=import-outside-toplevel

    temporary = Path(tempfile.mkdtemp(prefix="threadspire-server-smoke-"))
    process: subprocess.Popen[str] | None = None
    try:
        config = model.ModelConfig(
            name="EngCoder Server Smoke",
            context_length=128,
            d_model=64,
            n_layers=2,
            n_heads=4,
            ffn_hidden=128,
            expert_hidden=32,
        )
        model.initialize_model(temporary, config, force=True)
        original_readiness = model.evaluate_generation_readiness
        required_readiness_names = sorted({
            *model._REQUIRED_STRUCTURAL_READINESS_SAMPLES,  # pylint: disable=protected-access
            "identity",
            "privacy",
        })
        model.evaluate_generation_readiness = lambda *_args, **_kwargs: (
            model.GenerationReadiness(
                True,
                1.0,
                tuple({
                    "name": name,
                    "passed": True,
                    "structural_quality": True,
                    "reason": "deterministic server smoke stub",
                    "output": "thread and checkpoint",
                } for name in required_readiness_names),
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

        if native_host is not None:
            ready_completion = native_completion(native_host, temporary)
            assert ready_completion["generation"] == trained["generation"]
            assert ready_completion["origin"] == "random-initialized-local-model"
            ready_health = native_health(native_host, temporary)
            assert ready_health["status"] == "ok"
            assert ready_health["generation"] == trained["generation"]
            assert ready_health["requested_generation"] == trained["generation"]
            assert ready_health["degraded"] is False
            assert ready_health["fallback_reason"] == ""

            active_manifest_path = (
                model.resolve_active_generation(temporary) / "manifest.json"
            )
            active_manifest_bytes = active_manifest_path.read_bytes()
            try:
                for readiness_field in ("bootstrap_complete", "generation_ready"):
                    incomplete_manifest = json.loads(
                        active_manifest_bytes.decode("utf-8")
                    )
                    assert incomplete_manifest[readiness_field] is True
                    incomplete_manifest[readiness_field] = False
                    active_manifest_path.write_text(
                        json.dumps(incomplete_manifest, indent=2) + "\n",
                        encoding="utf-8",
                    )
                    fallback_completion = native_completion(native_host, temporary)
                    assert fallback_completion["generation"] == "foundation"
                    assert fallback_completion["origin"] == "verified-native-foundation"
                    degraded_health = native_health(native_host, temporary)
                    assert degraded_health["generation"] == "foundation"
                    assert degraded_health["requested_generation"] == trained["generation"]
                    assert degraded_health["degraded"] is True
                    assert "not ready for inference" in degraded_health["fallback_reason"]
            finally:
                active_manifest_path.write_bytes(active_manifest_bytes)

            restored_completion = native_completion(native_host, temporary)
            assert restored_completion["generation"] == trained["generation"]
            assert restored_completion["origin"] == "random-initialized-local-model"

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
        print("EngCoder active and isolated shadow-model server smoke tests passed")
        return 0
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=5.0)
        shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
