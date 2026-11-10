#!/usr/bin/env python3
"""Private PyTorch backend for EngCoder.

The C++23 application owns routing, scheduling, accepted-memory storage, the
skill lifecycle, user interaction, and process lifetime. This worker owns the
random-initialized EngCoder transformer, generation, full-weight internal
candidate training, and optional external-model LoRA/FFN workflows.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager, nullcontext
import gc
import hashlib
import json
import math
import os
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from collections import OrderedDict
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Iterable

try:
    from . import curriculum_guard as curriculum
    from . import curriculum_compiler as curriculum_compiler
except ImportError:
    import curriculum_guard as curriculum
    import curriculum_compiler as curriculum_compiler

import internal_model as internal

os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

PREEMPTED_EXIT_CODE = 75


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="EngCoder private backend")
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("doctor")

    serve = commands.add_parser("serve")
    serve.add_argument("--port", type=int, default=0)
    serve.add_argument("--ready-file", type=Path, required=True)
    serve.add_argument("--state-root", type=Path, required=True)
    serve.add_argument("--generation", default="")
    serve.add_argument("--control-dir", type=Path)
    serve.add_argument("--max-loaded-models", type=int, default=2)
    serve.add_argument("--max-resident-addons", type=int, default=2)
    serve.add_argument("--vram-budget-gib", type=float, default=0.0)

    train = commands.add_parser("train-lora")
    train.add_argument("--base-model", required=True)
    train.add_argument("--episodes", type=Path, required=True)
    train.add_argument("--skill", required=True)
    train.add_argument("--output", type=Path, required=True)
    train.add_argument("--job-dir", type=Path, required=True)
    train.add_argument("--control-dir", type=Path, required=True)
    train.add_argument("--seed-dataset", type=Path)
    train.add_argument("--seed-replay-limit", type=int, default=64)
    train.add_argument("--anchors", type=Path)
    train.add_argument("--module", type=Path)
    train.add_argument("--resume-adapter", type=Path)
    train.add_argument("--rank", type=int, default=16)
    train.add_argument("--alpha", type=int, default=32)
    train.add_argument("--max-steps", type=int, default=12)
    train.add_argument("--learning-rate", type=float, default=1.0e-4)
    train.add_argument("--max-sequence-tokens", type=int, default=2048)
    train.add_argument("--checkpoint-steps", type=int, default=3)
    train.add_argument("--require-cuda", action="store_true")

    consolidate = commands.add_parser("consolidate-module")
    consolidate.add_argument("--base-model", required=True)
    consolidate.add_argument("--adapter", type=Path, required=True)
    consolidate.add_argument("--skill", required=True)
    consolidate.add_argument("--output", type=Path, required=True)
    consolidate.add_argument("--job-dir", type=Path, required=True)
    consolidate.add_argument("--control-dir", type=Path, required=True)
    consolidate.add_argument("--episodes", type=Path, required=True)
    consolidate.add_argument("--seed-dataset", type=Path)
    consolidate.add_argument("--seed-replay-limit", type=int, default=32)
    consolidate.add_argument("--anchors", type=Path)
    consolidate.add_argument("--prior-module", type=Path)
    consolidate.add_argument("--max-steps", type=int, default=6)
    consolidate.add_argument("--learning-rate", type=float, default=5.0e-6)
    consolidate.add_argument("--max-sequence-tokens", type=int, default=1536)
    consolidate.add_argument("--checkpoint-steps", type=int, default=1)
    consolidate.add_argument("--layers-per-pass", type=int, default=1)
    consolidate.add_argument("--require-cuda", action="store_true")

    initialize = commands.add_parser("init-internal")
    initialize.add_argument("--state-root", type=Path, required=True)
    initialize.add_argument(
        "--profile",
        choices=("micro", "standard", "large-local", "desktop-250m", "desktop-16gb"),
        default="micro",
    )
    initialize.add_argument("--force", action="store_true")
    initialize.add_argument("--seed", type=int, default=1337)
    initialize.add_argument("--progress-file", type=Path)

    train_internal = commands.add_parser("train-internal")
    train_internal.add_argument("--state-root", type=Path, required=True)
    train_internal.add_argument("--seed-dataset", type=Path, required=True)
    train_internal.add_argument("--episodes", type=Path)
    train_internal.add_argument("--anchors", type=Path)
    train_internal.add_argument(
        "--delta-limit",
        type=int,
        default=0,
        help=(
            "Number of newest authoritative live episodes in this candidate. "
            "The curriculum compiler adds only bounded representative replay."
        ),
    )
    train_internal.add_argument("--scope", default="all")
    train_internal.add_argument("--max-steps", type=int, default=500)
    train_internal.add_argument("--learning-rate", type=float, default=3.0e-4)
    train_internal.add_argument("--batch-size", type=int, default=4)
    train_internal.add_argument("--accumulation-steps", type=int, default=4)
    train_internal.add_argument("--checkpoint-steps", type=int, default=50)
    train_internal.add_argument("--control-dir", type=Path)
    train_internal.add_argument("--metrics-output", type=Path)
    train_internal.add_argument("--progress-file", type=Path)
    train_internal.add_argument("--foreground-bootstrap", action="store_true")
    train_internal.add_argument("--bootstrap-target-steps", type=int, default=0)
    train_internal.add_argument("--slot-timeout-seconds", type=float, default=300.0)
    train_internal.add_argument("--require-cuda", action="store_true")
    train_internal.add_argument("--seed", type=int, default=1337)
    train_internal.add_argument("--defer-promotion", action="store_true")

    promote_internal = commands.add_parser("promote-internal")
    promote_internal.add_argument("--state-root", type=Path, required=True)
    promote_internal.add_argument("--generation", required=True)
    promote_internal.add_argument("--expected-parent", required=True)

    activate_internal = commands.add_parser("activate-internal")
    activate_internal.add_argument("--state-root", type=Path, required=True)
    activate_internal.add_argument("--generation", required=True)
    activate_internal.add_argument("--expected-current", required=True)

    internal_status = commands.add_parser("internal-status")
    internal_status.add_argument("--state-root", type=Path, required=True)

    repair = commands.add_parser("repair")
    repair.add_argument("--repository", type=Path, required=True)
    repair.add_argument("--task-file", type=Path, required=True)
    repair.add_argument("--diagnostics", type=Path, required=True)
    repair.add_argument("--phase", required=True)
    repair.add_argument("--iteration", type=int, required=True)
    repair.add_argument("--base-model", required=True)
    repair.add_argument("--adapter", type=Path)
    repair.add_argument("--module", type=Path)
    repair.add_argument("--output-diff", type=Path, required=True)
    repair.add_argument("--max-context-chars", type=int, default=180000)
    repair.add_argument("--max-new-tokens", type=int, default=4096)

    inspect = commands.add_parser("inspect-module")
    inspect.add_argument("--module", type=Path, required=True)

    mcp_stdio_parser = commands.add_parser("mcp-stdio")
    mcp_stdio_parser.add_argument("--request", type=Path, required=True)
    return curriculum.enforce_namespace(parser.parse_args())


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(path)


def doctor() -> int:
    import importlib.metadata

    import torch
    import safetensors

    def optional_version(distribution: str) -> str | None:
        try:
            return importlib.metadata.version(distribution)
        except importlib.metadata.PackageNotFoundError:
            return None

    payload = {
        "python": sys.version.split()[0],
        "torch": torch.__version__,
        "cuda": torch.cuda.is_available(),
        "gpu": torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,
        "vram_gib": (
            round(torch.cuda.get_device_properties(0).total_memory / (1024**3), 2)
            if torch.cuda.is_available()
            else 0.0
        ),
        "transformers": optional_version("transformers"),
        "peft": optional_version("peft"),
        "safetensors": safetensors.__version__,
        "internal_training_ready": True,
    }
    print(json.dumps(payload, indent=2), flush=True)
    return 0


def _messages_to_prompt(tokenizer: Any, messages: list[dict[str, str]]) -> str:
    if hasattr(tokenizer, "apply_chat_template") and tokenizer.chat_template:
        return tokenizer.apply_chat_template(
            messages,
            tokenize=False,
            add_generation_prompt=True,
        )
    parts: list[str] = []
    for message in messages:
        parts.append(f"{message['role'].upper()}: {message['content']}")
    parts.append("ASSISTANT:")
    return "\n\n".join(parts)


def _torch_dtype(torch: Any) -> Any:
    if torch.cuda.is_available() and torch.cuda.is_bf16_supported():
        return torch.bfloat16
    if torch.cuda.is_available():
        return torch.float16
    return torch.float32


def _is_ffn_key(name: str) -> bool:
    lowered = name.lower()
    return any(marker in lowered for marker in (".mlp.", ".feed_forward.", ".ffn."))


def _module_manifest(module_path: Path) -> dict[str, Any]:
    manifest_path = module_path / "module.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"module manifest not found: {manifest_path}")
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _validate_module_storage(module_path: Path) -> dict[str, Any]:
    from safetensors import safe_open

    manifest = _module_manifest(module_path)
    if manifest.get("format") != "threadspire.external-skill-delta.v1":
        raise ValueError(f"unsupported module format: {manifest.get('format')}")
    weights_name = str(manifest.get("weights_file", "")).strip()
    if not weights_name or Path(weights_name).name != weights_name:
        raise ValueError("module weights_file must be one local filename")
    weights_path = module_path / weights_name
    if not weights_path.is_file():
        raise FileNotFoundError(f"module weights not found: {weights_path}")
    expected_hash = str(manifest.get("weights_sha256", "")).strip()
    actual_hash = _sha256_file(weights_path)
    if not expected_hash or actual_hash != expected_hash:
        raise ValueError("module weights checksum mismatch")

    tensor_count = 0
    parameter_count = 0
    with safe_open(str(weights_path), framework="pt", device="cpu") as tensors:
        for name in tensors.keys():
            if not _is_ffn_key(name):
                raise ValueError(f"module contains a non-FFN tensor: {name}")
            shape = tensors.get_slice(name).get_shape()
            tensor_count += 1
            parameter_count += math.prod(int(value) for value in shape)
    if tensor_count != int(manifest.get("tensor_count", -1)):
        raise ValueError("module tensor count does not match its manifest")
    if parameter_count != int(manifest.get("parameter_count", -1)):
        raise ValueError("module parameter count does not match its manifest")
    return manifest


def _apply_module_delta(
    model: Any,
    module_path: Path,
    expected_base_model: str | None = None,
) -> None:
    import torch
    from safetensors.torch import load_file

    manifest = _validate_module_storage(module_path)
    if expected_base_model is not None:
        if manifest.get("base_model") != expected_base_model or manifest.get(
            "base_fingerprint"
        ) != _model_fingerprint(expected_base_model):
            raise ValueError("module was built for a different base model")
    tensors = load_file(str(module_path / manifest["weights_file"]), device="cpu")
    parameters = dict(model.named_parameters())
    missing: list[str] = []
    mismatched: list[str] = []
    with torch.no_grad():
        for name, delta in tensors.items():
            parameter = parameters.get(name)
            if parameter is None:
                missing.append(name)
                continue
            if tuple(parameter.shape) != tuple(delta.shape):
                mismatched.append(name)
                continue
            parameter.add_(delta.to(device=parameter.device, dtype=parameter.dtype))
    if missing or mismatched:
        raise ValueError(
            "module is incompatible; "
            f"{len(missing)} tensors missing and {len(mismatched)} tensor shapes mismatched"
        )


def _model_fingerprint(model_name: str) -> str:
    return hashlib.sha256(model_name.encode("utf-8")).hexdigest()


@dataclass(slots=True)
class LoadedModel:
    tokenizer: Any
    model: Any
    lock: threading.Lock
    model_name: str
    adapter_path: str
    module_path: str


class ModelCache:
    """Optional external-model cache used only by Model Lab/LoRA workflows.

    EngCoder internal inference never enters this cache and never calls
    ``from_pretrained``.
    """

    def __init__(self, maximum: int) -> None:
        self._maximum = max(1, maximum)
        self._models: OrderedDict[tuple[str, str, str], LoadedModel] = OrderedDict()
        self._lock = threading.RLock()

    def get(self, model_name: str, adapter_path: str, module_path: str) -> LoadedModel:
        key = (model_name, adapter_path, module_path)
        with self._lock:
            cached = self._models.get(key)
            if cached is not None:
                self._models.move_to_end(key)
                return cached
            loaded = self._load(model_name, adapter_path, module_path)
            self._models[key] = loaded
            while len(self._models) > self._maximum:
                _, victim = self._models.popitem(last=False)
                self._dispose(victim)
            return loaded

    @staticmethod
    def _dispose(loaded: LoadedModel) -> None:
        del loaded.model
        del loaded.tokenizer
        gc.collect()
        try:
            import torch

            if torch.cuda.is_available():
                torch.cuda.empty_cache()
        except Exception:
            pass

    @staticmethod
    def _load(model_name: str, adapter_path: str, module_path: str) -> LoadedModel:
        import torch
        from transformers import AutoModelForCausalLM, AutoTokenizer, BitsAndBytesConfig

        tokenizer = AutoTokenizer.from_pretrained(model_name, use_fast=True)
        if tokenizer.pad_token_id is None:
            tokenizer.pad_token = tokenizer.eos_token

        use_full_precision = bool(module_path)
        kwargs: dict[str, Any] = {
            "low_cpu_mem_usage": True,
            "torch_dtype": _torch_dtype(torch),
        }
        if torch.cuda.is_available():
            kwargs["device_map"] = "auto"
            if not use_full_precision:
                kwargs["quantization_config"] = BitsAndBytesConfig(
                    load_in_4bit=True,
                    bnb_4bit_quant_type="nf4",
                    bnb_4bit_use_double_quant=True,
                    bnb_4bit_compute_dtype=_torch_dtype(torch),
                )

        model = AutoModelForCausalLM.from_pretrained(model_name, **kwargs)
        if module_path:
            _apply_module_delta(model, Path(module_path), model_name)
        if adapter_path:
            from peft import PeftModel

            model = PeftModel.from_pretrained(model, adapter_path)
        model.eval()
        return LoadedModel(
            tokenizer=tokenizer,
            model=model,
            lock=threading.Lock(),
            model_name=model_name,
            adapter_path=adapter_path,
            module_path=module_path,
        )

    def clear(self) -> None:
        with self._lock:
            values = list(self._models.values())
            self._models.clear()
        for value in values:
            self._dispose(value)


class ServerState:
    def __init__(
        self,
        state_root: Path,
        maximum_models: int,
        generation: str = "",
        control_dir: Path | None = None,
        maximum_resident_addons: int = 2,
        vram_budget_gib: float = 0.0,
    ) -> None:
        self.state_root = state_root
        self.control_dir = control_dir or (state_root / "control")
        self.control_dir.mkdir(parents=True, exist_ok=True)
        self.cache = ModelCache(maximum_models)
        self.shutdown_requested = threading.Event()
        self.started = time.monotonic()
        self.generation_slots = threading.Semaphore(maximum_models)
        self._generation_lock = threading.Lock()
        self._active_generations = 0
        self._internal_lock = threading.RLock()
        self._internal_model: internal.LoadedInternalModel | None = None
        self._internal_generation = ""
        self.requested_internal_generation = generation.strip()
        self.maximum_resident_addons = max(0, int(maximum_resident_addons))
        self.vram_budget_gib = max(0.0, float(vram_budget_gib))

    def begin_inference(self) -> None:
        with self._generation_lock:
            self._active_generations += 1
            atomic_write_text(self.control_dir / "inference.server", str(self._active_generations))

    def end_inference(self) -> None:
        with self._generation_lock:
            self._active_generations = max(0, self._active_generations - 1)
            marker = self.control_dir / "inference.server"
            if self._active_generations == 0:
                marker.unlink(missing_ok=True)
            else:
                atomic_write_text(marker, str(self._active_generations))

    def active_generations(self) -> int:
        with self._generation_lock:
            return self._active_generations

    def wait_for_training_yield(self) -> None:
        marker = self.control_dir / "training.active"
        while marker.exists():
            if self.shutdown_requested.is_set():
                raise RuntimeError("model server is shutting down")
            time.sleep(0.05)

    def get_internal(self) -> internal.LoadedInternalModel:
        generation = self.requested_internal_generation
        if not generation:
            pointer = internal.active_manifest_path(self.state_root)
            if not pointer.is_file():
                raise FileNotFoundError(
                    "EngCoder internal model is not initialized. Use Create internal model first."
                )
            payload = json.loads(pointer.read_text(encoding="utf-8"))
            generation = str(payload.get("generation", ""))
        with self._internal_lock:
            if self._internal_model is not None and generation == self._internal_generation:
                return self._internal_model
            self._clear_internal_locked()
            loaded = (
                internal.load_generation_model(
                    self.state_root, generation, lazy_experts=True,
                    maximum_resident_addons=self.maximum_resident_addons,
                    vram_budget_gib=self.vram_budget_gib,
                )
                if self.requested_internal_generation else
                internal.load_active_model(
                    self.state_root, lazy_experts=True,
                    maximum_resident_addons=self.maximum_resident_addons,
                    vram_budget_gib=self.vram_budget_gib,
                )
            )
            allowed_status = (
                {"trained-candidate"}
                if self.requested_internal_generation else
                {"trained"}
            )
            if not (
                loaded.manifest.get("status") in allowed_status
                and loaded.manifest.get("bootstrap_complete", False)
                and loaded.manifest.get("full_backbone_trained", False)
                and loaded.manifest.get("generation_ready", False)
                and internal.manifest_uses_current_bootstrap_policy(loaded.manifest)
            ):
                raise RuntimeError(
                    "EngCoder internal weights have not passed full-backbone bootstrap and generation readiness"
                )
            self._internal_model = loaded
            self._internal_generation = loaded.generation
            return loaded

    def served_generation(self) -> str:
        if self.requested_internal_generation:
            return self.requested_internal_generation
        try:
            return internal.resolve_active_generation(self.state_root).name
        except Exception:
            return ""

    def _clear_internal_locked(self) -> None:
        if self._internal_model is not None:
            del self._internal_model.model
            self._internal_model = None
            self._internal_generation = ""
            gc.collect()
            try:
                import torch

                if torch.cuda.is_available():
                    torch.cuda.empty_cache()
            except Exception:
                pass

    def clear_internal(self) -> None:
        with self._internal_lock:
            self._clear_internal_locked()

    def release_cache_for_training(self) -> bool:
        with self._generation_lock:
            if self._active_generations != 0:
                return False
            self.cache.clear()
            self.clear_internal()
            return True

    def residency_status(self, load_if_needed: bool = False) -> dict[str, Any]:
        with self._internal_lock:
            if self._internal_model is None and load_if_needed:
                self.get_internal()
            if self._internal_model is None:
                try:
                    import torch
                    cuda = {
                        "available": bool(torch.cuda.is_available()),
                        "device": torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,
                        "total_bytes": int(torch.cuda.get_device_properties(0).total_memory) if torch.cuda.is_available() else 0,
                        "allocated_bytes": int(torch.cuda.memory_allocated(0)) if torch.cuda.is_available() else 0,
                        "reserved_bytes": int(torch.cuda.memory_reserved(0)) if torch.cuda.is_available() else 0,
                    }
                except Exception:
                    cuda = {"available": False}
                return {
                    "format": "engcoder.residency.v1",
                    "generation": self.served_generation(),
                    "loaded": False,
                    "policy": "pinned-keymaker+architect;ram-warm-addons;usage-lru-vram",
                    "maximum_resident_addons": self.maximum_resident_addons,
                    "resident_addons": [],
                    "components": [],
                    "cuda": cuda,
                }
            payload = self._internal_model.residency_payload()
            payload["loaded"] = True
            return payload

    def activate_lane(self, lane: str) -> dict[str, Any]:
        with self._internal_lock:
            loaded = self.get_internal()
            loaded.activate_lane(lane)
            return loaded.residency_payload()

    def release_idle_addons(self) -> dict[str, Any]:
        with self._internal_lock:
            if self._internal_model is not None:
                self._internal_model.release_idle_addons()
                return self._internal_model.residency_payload()
            return self.residency_status(load_if_needed=False)


def _inference_requested(control_dir: Path) -> bool:
    if (control_dir / "inference.priority").exists():
        return True
    try:
        return any(control_dir.glob("inference.*"))
    except OSError:
        return True


def _server_port(control_dir: Path) -> int:
    ready_file = control_dir.parent / "state" / "server.ready"
    try:
        for line in ready_file.read_text(encoding="utf-8").splitlines():
            if line.startswith("port="):
                return int(line[5:])
    except (OSError, ValueError):
        return 0
    return 0


def _release_server_cache(control_dir: Path) -> bool:
    port = _server_port(control_dir)
    if port <= 0:
        return True
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/release-cache",
        data=b"{}",
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=2.0) as response:
            return int(response.status) == int(HTTPStatus.OK)
    except urllib.error.HTTPError as exception:
        if exception.code == int(HTTPStatus.CONFLICT):
            return False
        return False
    except OSError:
        return True


def _process_is_alive(process_id: int) -> bool:
    if process_id <= 0:
        return False
    try:
        import psutil  # type: ignore[import-not-found]

        return bool(psutil.pid_exists(process_id))
    except Exception:
        try:
            os.kill(process_id, 0)
            return True
        except OSError:
            return False


def _lock_owner(path: Path) -> int:
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("pid="):
                return int(line[4:])
    except (OSError, ValueError):
        return 0
    return 0


def _foreground_training_cleanup(control_dir: Path) -> None:
    """Remove stale cooperative blockers after the native host has been stopped."""

    (control_dir / "training.paused").unlink(missing_ok=True)
    for marker in control_dir.glob("inference.*"):
        marker.unlink(missing_ok=True)


def _slot_blockers(control_dir: Path, lock_path: Path) -> list[str]:
    blockers: list[str] = []
    owner = _lock_owner(lock_path)
    if owner > 0 and _process_is_alive(owner):
        blockers.append(f"training lock owned by process {owner}")
    if (control_dir / "training.paused").exists():
        blockers.append("training.paused")
    try:
        blockers.extend(sorted(marker.name for marker in control_dir.glob("inference.*")))
    except OSError:
        blockers.append("unreadable inference markers")
    if _server_port(control_dir) > 0:
        blockers.append("internal-model host cache")
    return blockers


@contextmanager
def _training_slot(
    control_dir: Path,
    *,
    foreground: bool = False,
    progress_file: Path | None = None,
    timeout_seconds: float = 300.0,
) -> Iterable[None]:
    control_dir.mkdir(parents=True, exist_ok=True)
    requested = control_dir / "training.requested"
    active = control_dir / "training.active"
    lock_path = control_dir / "training.lock"
    lock_descriptor: int | None = None
    deadline = time.monotonic() + max(1.0, timeout_seconds)
    last_progress = 0.0

    if foreground:
        _foreground_training_cleanup(control_dir)

    try:
        while lock_descriptor is None:
            try:
                lock_descriptor = os.open(
                    lock_path,
                    os.O_CREAT | os.O_EXCL | os.O_WRONLY,
                    0o600,
                )
                os.write(lock_descriptor, f"pid={os.getpid()}\n".encode("ascii"))
                os.fsync(lock_descriptor)
            except FileExistsError:
                owner = _lock_owner(lock_path)
                if owner == 0 or not _process_is_alive(owner):
                    lock_path.unlink(missing_ok=True)
                    continue
                now = time.monotonic()
                if now >= deadline:
                    raise TimeoutError(
                        f"timed out waiting for the training lock held by process {owner}"
                    )
                if now - last_progress >= 1.0:
                    internal.write_progress(
                        progress_file,
                        phase="waiting-for-training-slot",
                        message=f"Waiting for another training process ({owner}) to finish",
                        blocker=f"training lock owned by process {owner}",
                        elapsed_seconds=max(0.0, timeout_seconds - (deadline - now)),
                    )
                    last_progress = now
                time.sleep(0.2)

        atomic_write_text(requested, f"pid={os.getpid()}\n")
        while True:
            if foreground:
                _foreground_training_cleanup(control_dir)
            else:
                _wait_for_priority(control_dir, deadline=deadline)

            if not _release_server_cache(control_dir):
                now = time.monotonic()
                if now >= deadline:
                    raise TimeoutError("timed out waiting for the internal-model host to release GPU memory")
                if now - last_progress >= 1.0:
                    internal.write_progress(
                        progress_file,
                        phase="waiting-for-training-slot",
                        message="Waiting for the internal-model host to release GPU memory",
                        blockers=_slot_blockers(control_dir, lock_path),
                        elapsed_seconds=max(0.0, timeout_seconds - (deadline - now)),
                    )
                    last_progress = now
                time.sleep(0.1)
                continue

            if not foreground and _inference_requested(control_dir):
                now = time.monotonic()
                if now >= deadline:
                    blockers = ", ".join(_slot_blockers(control_dir, lock_path)) or "unknown blocker"
                    raise TimeoutError(f"timed out waiting for foreground inference to clear: {blockers}")
                if now - last_progress >= 1.0:
                    internal.write_progress(
                        progress_file,
                        phase="waiting-for-training-slot",
                        message="Waiting for foreground inference to finish",
                        blockers=_slot_blockers(control_dir, lock_path),
                        elapsed_seconds=max(0.0, timeout_seconds - (deadline - now)),
                    )
                    last_progress = now
                time.sleep(0.1)
                continue

            atomic_write_text(active, f"pid={os.getpid()}\n")
            if not foreground and _inference_requested(control_dir):
                active.unlink(missing_ok=True)
                time.sleep(0.1)
                continue
            requested.unlink(missing_ok=True)
            break
        yield
    finally:
        active.unlink(missing_ok=True)
        requested.unlink(missing_ok=True)
        if lock_descriptor is not None:
            os.close(lock_descriptor)
        if _lock_owner(lock_path) == os.getpid():
            lock_path.unlink(missing_ok=True)


def _is_internal_identifier(model_name: str) -> bool:
    return model_name.startswith("engcoder://") or model_name.startswith("threadspire://") or model_name.startswith("internal://")


def _internal_lane(model_name: str) -> str:
    if "://" not in model_name:
        return "fast"
    lane = model_name.split("://", 1)[1].split("?", 1)[0].strip("/")
    return lane or "fast"


@dataclass(frozen=True, slots=True)
class InternalDraft:
    content: str
    completion_tokens: int
    elapsed_seconds: float
    quality: internal.GenerationQuality
    attempts: int


def _collect_internal_attempt(
    loaded: internal.LoadedInternalModel,
    messages: list[dict[str, str]],
    lane: str,
    maximum: int,
    temperature: float,
    on_progress: Callable[[int], None] | None = None,
    trusted_dataset: Path | None = None,
) -> tuple[str, int]:
    pieces: list[str] = []
    completion_tokens = 0
    for piece, token_id in internal.generate_tokens(
        loaded,
        messages,
        lane,
        maximum,
        temperature,
        trusted_dataset=trusted_dataset,
    ):
        pieces.append(piece)
        if token_id >= 0:
            completion_tokens += 1
            if on_progress is not None and completion_tokens % 32 == 0:
                on_progress(completion_tokens)
    return "".join(pieces).strip(), completion_tokens


def _internal_draft(
    loaded: internal.LoadedInternalModel,
    messages: list[dict[str, str]],
    lane: str,
    maximum: int,
    temperature: float,
    on_progress: Callable[[int], None] | None = None,
    trusted_dataset: Path | None = None,
) -> InternalDraft:
    started = time.perf_counter()
    temperatures = [temperature]
    if temperature > 0.0:
        temperatures.append(0.0)

    verified_content = internal.verified_local_response(messages, trusted_dataset)
    last_content = ""
    last_tokens = 0
    last_quality = internal.GenerationQuality(False, "no internal-model draft was generated")
    for attempt, attempt_temperature in enumerate(temperatures, start=1):
        last_content, last_tokens = _collect_internal_attempt(
            loaded,
            messages,
            lane,
            maximum,
            attempt_temperature,
            on_progress,
            trusted_dataset,
        )
        last_quality = internal.assess_generation_quality(
            last_content,
            trusted_dataset,
            messages,
            verified_response=verified_content is not None and last_content == verified_content,
        )
        if last_quality.accepted:
            return InternalDraft(
                content=last_content,
                completion_tokens=last_tokens,
                elapsed_seconds=max(time.perf_counter() - started, 1.0e-6),
                quality=last_quality,
                attempts=attempt,
            )

    return InternalDraft(
        content=last_content,
        completion_tokens=last_tokens,
        elapsed_seconds=max(time.perf_counter() - started, 1.0e-6),
        quality=last_quality,
        attempts=len(temperatures),
    )


def _generate_internal(state: ServerState, payload: dict[str, Any], messages: list[dict[str, str]]) -> dict[str, Any]:
    model_name = str(payload.get("model", "engcoder://keymaker"))
    lane = _internal_lane(model_name)
    maximum = max(1, min(int(payload.get("max_tokens", 256)), 2048))
    temperature = max(0.0, min(float(payload.get("temperature", 0.2)), 2.0))
    prompt_tokens = len(internal.ByteTokenizer.prompt_tokens(messages))
    with state._internal_lock:
        loaded = state.get_internal()
        served_generation = loaded.generation
        with state.generation_slots:
            draft = _internal_draft(
                loaded,
                messages,
                lane,
                maximum,
                temperature,
                trusted_dataset=state.state_root / "data" / "bootstrap.jsonl",
            )
    accepted = draft.quality.accepted
    content = draft.content if accepted else ""
    finish_reason = "stop" if accepted else "low_confidence"
    if not accepted:
        print(
            f"Internal draft rejected before display: {draft.quality.reason} "
            f"(attempts={draft.attempts})",
            flush=True,
        )
    return {
        "id": f"engcoder-{time.time_ns()}",
        "object": "chat.completion",
        "created": int(time.time()),
        "model": model_name,
        "source": "engcoder-internal",
        "generation": served_generation,
        "origin": "random-initialized-local-model",
        "external_base_weights": False,
        "choices": [{
            "index": 0,
            "message": {"role": "assistant", "content": content},
            "finish_reason": finish_reason,
        }],
        "usage": {
            "prompt_tokens": prompt_tokens,
            "completion_tokens": draft.completion_tokens,
            "total_tokens": prompt_tokens + draft.completion_tokens,
        },
        "quality": {
            "accepted": accepted,
            "reason": draft.quality.reason,
            "attempts": draft.attempts,
        },
        "content": content,
        "prompt_tokens": prompt_tokens,
        "completion_tokens": draft.completion_tokens,
        "tokens_per_second": draft.completion_tokens / draft.elapsed_seconds,
        "elapsed_ms": draft.elapsed_seconds * 1000.0,
    }


def _stream_internal(
    handler: "RequestHandler",
    state: ServerState,
    payload: dict[str, Any],
    messages: list[dict[str, str]],
) -> None:
    model_name = str(payload.get("model", "engcoder://keymaker"))
    lane = _internal_lane(model_name)
    maximum = max(1, min(int(payload.get("max_tokens", 256)), 2048))
    temperature = max(0.0, min(float(payload.get("temperature", 0.2)), 2.0))

    state.begin_inference()
    try:
        state.wait_for_training_yield()
        handler.send_response(int(HTTPStatus.OK))
        handler.send_header("Content-Type", "text/event-stream; charset=utf-8")
        handler.send_header("Cache-Control", "no-cache")
        handler.send_header("Connection", "close")
        handler.end_headers()

        def heartbeat(completion_tokens: int) -> None:
            handler.wfile.write(
                f": validating internal draft at token {completion_tokens}\n\n".encode("utf-8")
            )
            handler.wfile.flush()

        with state._internal_lock:
            loaded = state.get_internal()
            served_generation = loaded.generation
            with state.generation_slots:
                draft = _internal_draft(
                    loaded,
                    messages,
                    lane,
                    maximum,
                    temperature,
                    heartbeat,
                    trusted_dataset=state.state_root / "data" / "bootstrap.jsonl",
                )

        if draft.quality.accepted:
            for offset in range(0, len(draft.content), 64):
                piece = draft.content[offset : offset + 64]
                event = {
                    "id": f"engcoder-{time.time_ns()}",
                    "object": "chat.completion.chunk",
                    "created": int(time.time()),
                    "model": model_name,
                    "source": "engcoder-internal",
                    "generation": served_generation,
                    "origin": "random-initialized-local-model",
                    "external_base_weights": False,
                    "choices": [{
                        "index": 0,
                        "delta": {"content": piece},
                        "finish_reason": None,
                    }],
                }
                handler.wfile.write(
                    ("data: " + json.dumps(event, ensure_ascii=False) + "\n\n").encode("utf-8")
                )
                handler.wfile.flush()
        else:
            print(
                f"Internal streaming draft rejected before display: {draft.quality.reason} "
                f"(attempts={draft.attempts})",
                flush=True,
            )

        finish_reason = "stop" if draft.quality.accepted else "low_confidence"
        final_event = {
            "id": f"engcoder-{time.time_ns()}",
            "object": "chat.completion.chunk",
            "created": int(time.time()),
            "model": model_name,
            "source": "engcoder-internal",
            "generation": served_generation,
            "origin": "random-initialized-local-model",
            "external_base_weights": False,
            "choices": [{
                "index": 0,
                "delta": {},
                "finish_reason": finish_reason,
            }],
            "usage": {"completion_tokens": draft.completion_tokens},
            "quality": {
                "accepted": draft.quality.accepted,
                "reason": draft.quality.reason,
                "attempts": draft.attempts,
            },
        }
        handler.wfile.write(
            ("data: " + json.dumps(final_event) + "\n\ndata: [DONE]\n\n").encode("utf-8")
        )
        handler.wfile.flush()
        handler.close_connection = True
    except (BrokenPipeError, ConnectionResetError):
        handler.close_connection = True
    except Exception as exception:
        try:
            error_event = {
                "error": {
                    "message": f"{type(exception).__name__}: {exception}",
                },
            }
            handler.wfile.write(
                ("data: " + json.dumps(error_event) + "\n\ndata: [DONE]\n\n").encode("utf-8")
            )
            handler.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        handler.close_connection = True
    finally:
        state.end_inference()


def _generate_external(state: ServerState, payload: dict[str, Any], messages: list[dict[str, str]]) -> dict[str, Any]:
    import torch

    model_name = str(payload.get("model", "")).strip()
    maximum = max(1, min(int(payload.get("max_tokens", 256)), 8192))
    temperature = max(0.0, min(float(payload.get("temperature", 0.2)), 2.0))
    adapter_path = str(payload.get("adapter_path", "")).strip()
    module_path = str(payload.get("module_path", "")).strip()
    loaded = state.cache.get(model_name, adapter_path, module_path)
    with state.generation_slots, loaded.lock, torch.inference_mode():
        prompt = _messages_to_prompt(loaded.tokenizer, messages)
        encoded = loaded.tokenizer(prompt, return_tensors="pt")
        device = next(loaded.model.parameters()).device
        encoded = {key: value.to(device) for key, value in encoded.items()}
        prompt_tokens = int(encoded["input_ids"].shape[-1])
        started = time.perf_counter()
        generated = loaded.model.generate(
            **encoded,
            max_new_tokens=maximum,
            do_sample=temperature > 0.0,
            temperature=max(temperature, 1.0e-5),
            top_p=0.92,
            repetition_penalty=1.04,
            use_cache=True,
            pad_token_id=loaded.tokenizer.pad_token_id,
            eos_token_id=loaded.tokenizer.eos_token_id,
        )
        elapsed = max(time.perf_counter() - started, 1.0e-6)
        completion_ids = generated[0, prompt_tokens:]
        content = loaded.tokenizer.decode(completion_ids, skip_special_tokens=True)
        completion_tokens = int(completion_ids.shape[-1])
    return {
        "id": f"external-model-lab-{time.time_ns()}",
        "object": "chat.completion",
        "created": int(time.time()),
        "model": model_name,
        "source": "optional-external-model-lab",
        "choices": [{
            "index": 0,
            "message": {"role": "assistant", "content": content},
            "finish_reason": "stop",
        }],
        "usage": {
            "prompt_tokens": prompt_tokens,
            "completion_tokens": completion_tokens,
            "total_tokens": prompt_tokens + completion_tokens,
        },
        "content": content,
        "prompt_tokens": prompt_tokens,
        "completion_tokens": completion_tokens,
        "tokens_per_second": completion_tokens / elapsed,
        "elapsed_ms": elapsed * 1000.0,
    }


def _generate(state: ServerState, payload: dict[str, Any]) -> dict[str, Any]:
    model_name = str(payload.get("model", "")).strip()
    if not model_name:
        raise ValueError("model is required")
    messages = payload.get("messages")
    if not isinstance(messages, list) or not messages:
        raise ValueError("messages must be a non-empty array")
    clean_messages = [
        {"role": str(item.get("role", "user")), "content": str(item.get("content", ""))}
        for item in messages
        if isinstance(item, dict)
    ]

    state.begin_inference()
    try:
        state.wait_for_training_yield()
        if _is_internal_identifier(model_name):
            return _generate_internal(state, payload, clean_messages)
        return _generate_external(state, payload, clean_messages)
    finally:
        state.end_inference()


class RequestHandler(BaseHTTPRequestHandler):
    server_version = "EngCoder/0.7.4"

    @property
    def state(self) -> ServerState:
        return self.server.threadspire_state  # type: ignore[attr-defined]

    def log_message(self, format: str, *args: Any) -> None:
        print(f"HTTP {self.client_address[0]} {format % args}", flush=True)

    def _write_json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(int(status))
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/health":
            try:
                import torch

                payload = {
                    "status": "ready",
                    "cuda": torch.cuda.is_available(),
                    "gpu": torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,
                    "uptime_seconds": time.monotonic() - self.state.started,
                    "active_generations": self.state.active_generations(),
                    "served_generation": self.state.served_generation(),
                    "shadow_candidate": bool(self.state.requested_internal_generation),
                    "internal_model": internal.status_payload(self.state.state_root),
                }
            except Exception as exception:
                payload = {"status": "degraded", "error": str(exception)}
            self._write_json(HTTPStatus.OK, payload)
            return
        if self.path == "/internal/status":
            self._write_json(HTTPStatus.OK, internal.status_payload(self.state.state_root))
            return
        if self.path == "/residency/status":
            self._write_json(HTTPStatus.OK, self.state.residency_status(load_if_needed=False))
            return
        self._write_json(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        try:
            if self.path == "/v1/chat/completions":
                payload = self._read_json()
                model_name = str(payload.get("model", "")).strip()
                messages = payload.get("messages")
                clean_messages = [
                    {
                        "role": str(item.get("role", "user")),
                        "content": str(item.get("content", "")),
                    }
                    for item in messages
                    if isinstance(item, dict)
                ] if isinstance(messages, list) else []
                if bool(payload.get("stream", False)) and _is_internal_identifier(model_name):
                    if not clean_messages:
                        raise ValueError("messages must be a non-empty array")
                    _stream_internal(self, self.state, payload, clean_messages)
                else:
                    self._write_json(HTTPStatus.OK, _generate(self.state, payload))
                return
            if self.path == "/residency/activate":
                payload = self._read_json()
                lane = str(payload.get("lane", "keymaker")).strip() or "keymaker"
                self._write_json(HTTPStatus.OK, self.state.activate_lane(lane))
                return
            if self.path == "/residency/release-addons":
                self._write_json(HTTPStatus.OK, self.state.release_idle_addons())
                return
            if self.path == "/release-cache":
                if self.state.release_cache_for_training():
                    self._write_json(HTTPStatus.OK, {"status": "released"})
                else:
                    self._write_json(
                        HTTPStatus.CONFLICT,
                        {"status": "busy", "active_generations": self.state.active_generations()},
                    )
                return
            if self.path == "/shutdown":
                self._write_json(HTTPStatus.OK, {"status": "stopping"})
                self.state.shutdown_requested.set()
                threading.Thread(target=self.server.shutdown, daemon=True).start()
                return
            self._write_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
        except torch_out_of_memory_error() as exception:
            self._write_json(
                HTTPStatus.INSUFFICIENT_STORAGE,
                {"error": f"GPU memory exhausted: {exception}"},
            )
        except Exception as exception:
            print(f"request failed: {type(exception).__name__}: {exception}", flush=True)
            self._write_json(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                {"error": f"{type(exception).__name__}: {exception}"},
            )


def torch_out_of_memory_error() -> type[BaseException]:
    try:
        import torch

        return torch.OutOfMemoryError
    except Exception:
        return MemoryError


def serve(arguments: argparse.Namespace) -> int:
    arguments.state_root.mkdir(parents=True, exist_ok=True)
    control_dir = arguments.control_dir or (arguments.state_root / "control")
    control_dir.mkdir(parents=True, exist_ok=True)
    (control_dir / "inference.server").unlink(missing_ok=True)
    if arguments.generation:
        candidate_path = internal.internal_root(arguments.state_root) / "generations" / arguments.generation
        manifest = internal.generation_manifest(candidate_path)
        if not (
            manifest.get("status") == "trained-candidate"
            and bool(manifest.get("bootstrap_complete", False))
            and bool(manifest.get("full_backbone_trained", False))
            and bool(manifest.get("generation_ready", False))
            and internal.manifest_uses_current_bootstrap_policy(manifest)
        ):
            raise RuntimeError(
                "refusing to shadow-serve a candidate that has not passed all readiness gates"
            )
    else:
        status = internal.status_payload(arguments.state_root)
        if not bool(status.get("ready", False)):
            raise RuntimeError(
                "refusing to serve an internal generation that has not passed "
                "full-backbone bootstrap and generation readiness"
            )
    server = ThreadingHTTPServer(("127.0.0.1", arguments.port), RequestHandler)
    server.daemon_threads = True
    state = ServerState(
        arguments.state_root,
        arguments.max_loaded_models,
        generation=arguments.generation,
        control_dir=arguments.control_dir,
        maximum_resident_addons=arguments.max_resident_addons,
        vram_budget_gib=arguments.vram_budget_gib,
    )
    server.threadspire_state = state  # type: ignore[attr-defined]
    port = int(server.server_address[1])
    atomic_write_text(arguments.ready_file, f"port={port}\npid={os.getpid()}\n")
    print(f"EngCoder model server listening on 127.0.0.1:{port}", flush=True)
    try:
        server.serve_forever(poll_interval=0.25)
    finally:
        state.cache.clear()
        state.clear_internal()
        (state.control_dir / "inference.server").unlink(missing_ok=True)
        server.server_close()
        try:
            arguments.ready_file.unlink(missing_ok=True)
        except Exception:
            pass
    return 0


_MINIMUM_DIVERSE_HUMAN_APPROVALS = 4
_WEIGHT_TRAINING_SKILLS = frozenset({
    "keymaker-foundation",
    "english-foundation",
    "english-conversation",
    "cpp23-core",
    "cpp23-debugging",
    "planning",
})


def _strip_training_emoji(value: str) -> str:
    """Independent worker boundary; chat rendering never calls this."""

    return internal.strip_training_emoji(value)


def _sanitize_training_response(value: str, source_model_id: str = "") -> str:
    return internal.sanitize_training_response(value, source_model_id)


def _normalized_learning_prompt(record: dict[str, Any]) -> str:
    prompt = _strip_training_emoji(str(record.get("prompt", "")))
    return re.sub(r"[^a-z0-9+_]+", " ", prompt.lower()).strip()


def _human_only_training_record(record: dict[str, Any]) -> bool:
    return bool(
        record.get("human_accepted", False)
        and not record.get("human_rejected", False)
        and not record.get("deterministic_verified", False)
        and not record.get("reference_verified", False)
    )


def _hard_correction_record(record: dict[str, Any]) -> bool:
    return bool(
        record.get("human_rejected", False)
        and str(record.get("provenance", "")) == "interactive-hard-correction"
        and str(record.get("correction", "")).strip()
    )


def _fnv_content_hash(skill: str, prompt: str, response: str) -> str:
    """Mirror EpisodeStore::make_content_hash, including 0xff separators."""
    value = 1469598103934665603
    for part in (skill, prompt, response):
        for octet in part.encode("utf-8"):
            value = ((value ^ octet) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        value = ((value ^ 0xFF) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def _learning_denylist(episodes: Path) -> tuple[set[str], set[str]]:
    """Read the authoritative denylist at ingestion time, not at model startup."""
    path = episodes.parent / "denylist.json"
    if not path.exists():
        return set(), set()
    try:
        items = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(items, list):
            raise ValueError("learning denylist is not an array")
        return (
            {entry["prompt_hash"] for entry in items if isinstance(entry, dict) and isinstance(entry.get("prompt_hash"), str)},
            {entry["response_hash"] for entry in items if isinstance(entry, dict) and isinstance(entry.get("response_hash"), str)},
        )
    except (OSError, json.JSONDecodeError, ValueError, TypeError, KeyError) as exc:
        # Fail closed. A corrupt/unreadable denylist must never silently enable training.
        raise RuntimeError(f"learning denylist cannot be verified: {path}") from exc


def _is_denied_learning(record: dict[str, Any], hashes: tuple[set[str], set[str]]) -> bool:
    prompt = str(record.get("prompt", ""))
    response = str(record.get("correction") if _hard_correction_record(record) else record.get("response", ""))
    return _fnv_content_hash("*", prompt, "") in hashes[0] or _fnv_content_hash("*", "", response) in hashes[1]


def _filter_banned_learning(records: list[dict[str, Any]], episodes: Path) -> list[dict[str, Any]]:
    hashes = _learning_denylist(episodes)
    return [record for record in records if not _is_denied_learning(record, hashes)]


def _load_episodes(path: Path, skill: str) -> list[dict[str, Any]]:
    # Verify denylist integrity even for unsupported skill scopes.
    if skill not in _WEIGHT_TRAINING_SKILLS:
        _learning_denylist(path)
        return []
    records: list[dict[str, Any]] = []
    diverse_human_behaviors: set[str] = set()
    ban_hashes = _learning_denylist(path)
    with path.open("r", encoding="utf-8") as stream:
        for line in stream:
            if not line.strip():
                continue
            record = json.loads(line)
            if _is_denied_learning(record, ban_hashes):
                continue
            if record.get("skill") != skill:
                continue
            normalized_prompt = _normalized_learning_prompt(record)
            human_only = _human_only_training_record(record)
            hard_correction = _hard_correction_record(record)
            if (human_only or hard_correction) and normalized_prompt:
                diverse_human_behaviors.add(
                    curriculum_compiler.behavior_signature(record)
                )
            if not record.get("eligible_for_training", False) and not hard_correction and not human_only:
                continue
            if not hard_correction and not (
                record.get("human_accepted", False)
                or record.get("deterministic_verified", False)
                or record.get("reference_verified", False)
            ):
                continue
            prompt = _strip_training_emoji(str(record.get("prompt", "")))
            response = _sanitize_training_response(
                str(record.get("correction" if hard_correction else "response", "")),
                str(record.get("model_id", "")),
            )
            if prompt and response:
                cleaned = dict(record)
                cleaned["prompt"] = prompt
                cleaned["response"] = response
                if hard_correction:
                    cleaned["hard_correction"] = True
                if "correction" in cleaned:
                    cleaned["correction"] = _sanitize_training_response(
                        str(cleaned.get("correction", "")),
                        str(cleaned.get("model_id", "")),
                    )
                cleaned["content_hash"] = hashlib.sha256(
                    (prompt + "\0" + response).encode("utf-8")
                ).hexdigest()
                records.append(cleaned)

    latest_corrections: dict[str, dict[str, Any]] = {}
    for record in records:
        normalized_prompt = _normalized_learning_prompt(record)
        if record.get("hard_correction", False) and normalized_prompt:
            latest_corrections[normalized_prompt] = record
    if latest_corrections:
        records = [
            record
            for record in records
            if (
                _normalized_learning_prompt(record) not in latest_corrections
                or record is latest_corrections[_normalized_learning_prompt(record)]
            )
        ]

    unique: dict[str, dict[str, Any]] = {}
    for record in records:
        fingerprint = str(record.get("content_hash", "")).strip()
        if not fingerprint:
            fingerprint = hashlib.sha256(
                (str(record.get("prompt", "")) + "\0" + str(record.get("response", ""))).encode("utf-8")
            ).hexdigest()
        unique[fingerprint] = record
    records = list(unique.values())
    behavior_unique: dict[str, dict[str, Any]] = {}
    for record in records:
        signature = curriculum_compiler.behavior_signature(record)
        existing = behavior_unique.get(signature)
        if existing is None:
            behavior_unique[signature] = record
            continue
        rank = (
            int(bool(record.get("hard_correction", False))),
            int(bool(record.get("human_accepted", False))),
            int(bool(record.get("reference_verified", False))),
            int(bool(record.get("deterministic_verified", False))),
            -len(str(record.get("response", ""))),
        )
        existing_rank = (
            int(bool(existing.get("hard_correction", False))),
            int(bool(existing.get("human_accepted", False))),
            int(bool(existing.get("reference_verified", False))),
            int(bool(existing.get("deterministic_verified", False))),
            -len(str(existing.get("response", ""))),
        )
        if rank > existing_rank:
            behavior_unique[signature] = record
    records = [behavior_unique[key] for key in sorted(behavior_unique)]
    if len(diverse_human_behaviors) < _MINIMUM_DIVERSE_HUMAN_APPROVALS:
        records = [
            record
            for record in records
            if not _human_only_training_record(record) and not record.get("hard_correction", False)
        ]
    return records

def _seed_domains_for_skill(skill: str) -> set[str]:
    normalized = skill.lower()
    if normalized.startswith("cpp23-") or normalized in {
        "build-systems", "windows-graphics", "compiler-debugging",
    }:
        return {"cpp23"}
    if normalized.startswith("english-") or normalized in {
        "keymaker", "explanation", "planning", "translation", "tool-use", "general",
    }:
        return {"keymaker"}
    return {"keymaker", "cpp23"}


def _load_seed_records(path: Path | None, skill: str, limit: int) -> list[dict[str, Any]]:
    if path is None or not path.is_file() or limit <= 0:
        return []
    domains = _seed_domains_for_skill(skill)
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as stream:
        for line in stream:
            if not line.strip():
                continue
            item = json.loads(line)
            if not item.get("accepted", False) or str(item.get("domain", "")) not in domains:
                continue
            messages = item.get("messages", [])
            system = next((str(m.get("content", "")) for m in messages if m.get("role") == "system"), "")
            user = next((str(m.get("content", "")) for m in messages if m.get("role") == "user"), "")
            assistant = next(
                (str(m.get("content", "")) for m in reversed(messages) if m.get("role") == "assistant"),
                "",
            )
            if not user or not assistant:
                continue
            prompt = f"System instruction: {system}\n\nUser request: {user}" if system else user
            records.append({
                "prompt": prompt,
                "response": assistant,
                "content_hash": hashlib.sha256((item.get("id", "") + "\0" + prompt + "\0" + assistant).encode("utf-8")).hexdigest(),
                "source": "seed",
            })
    records.sort(key=lambda item: str(item.get("content_hash", "")))
    return records[:limit]


def _load_anchor_records(path: Path | None) -> list[dict[str, Any]]:
    if path is None or not path.is_file():
        return []
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as stream:
        for line in stream:
            if not line.strip():
                continue
            item = json.loads(line)
            messages = item.get("messages", [])
            if len(messages) < 2:
                continue
            user = next((m.get("content", "") for m in messages if m.get("role") == "user"), "")
            assistant = next(
                (m.get("content", "") for m in reversed(messages) if m.get("role") == "assistant"),
                "",
            )
            if user and assistant:
                records.append({"prompt": user, "response": assistant})
    return records


def _format_training_example(
    tokenizer: Any,
    prompt: str,
    response: str,
    maximum_tokens: int,
) -> dict[str, list[int]]:
    messages = [
        {
            "role": "system",
            "content": "You are a precise local specialist. Follow the request and preserve verified behavior.",
        },
        {"role": "user", "content": prompt},
    ]
    prompt_text = _messages_to_prompt(tokenizer, messages)
    prompt_ids = tokenizer(prompt_text, add_special_tokens=True)["input_ids"]
    response_ids = tokenizer(response, add_special_tokens=False)["input_ids"]
    if tokenizer.eos_token_id is not None:
        response_ids = response_ids + [tokenizer.eos_token_id]
    maximum_tokens = max(256, maximum_tokens)
    if len(prompt_ids) + len(response_ids) > maximum_tokens:
        response_budget = max(64, min(len(response_ids), maximum_tokens // 2))
        prompt_budget = maximum_tokens - response_budget
        prompt_ids = prompt_ids[-prompt_budget:]
        response_ids = response_ids[:response_budget]
    input_ids = prompt_ids + response_ids
    labels = [-100] * len(prompt_ids) + response_ids
    return {"input_ids": input_ids, "labels": labels, "attention_mask": [1] * len(input_ids)}


class CausalCollator:
    def __init__(self, tokenizer: Any) -> None:
        self.tokenizer = tokenizer

    def __call__(self, features: list[dict[str, list[int]]]) -> dict[str, Any]:
        import torch

        maximum = max(len(item["input_ids"]) for item in features)
        pad = self.tokenizer.pad_token_id
        input_ids: list[list[int]] = []
        attention: list[list[int]] = []
        labels: list[list[int]] = []
        for item in features:
            missing = maximum - len(item["input_ids"])
            input_ids.append(item["input_ids"] + [pad] * missing)
            attention.append(item["attention_mask"] + [0] * missing)
            labels.append(item["labels"] + [-100] * missing)
        return {
            "input_ids": torch.tensor(input_ids, dtype=torch.long),
            "attention_mask": torch.tensor(attention, dtype=torch.long),
            "labels": torch.tensor(labels, dtype=torch.long),
        }


class PriorityCallback:
    def __init__(self, control_dir: Path) -> None:
        self.control_dir = control_dir
        self.preempted = False

    def _request_preemption(self, control: Any) -> None:
        if (self.control_dir / "training.paused").exists() or _inference_requested(self.control_dir):
            self.preempted = True
            control.should_save = True
            control.should_training_stop = True

    def on_step_begin(self, args: Any, state: Any, control: Any, **kwargs: Any) -> None:
        del args, state, kwargs
        self._request_preemption(control)

    def on_step_end(self, args: Any, state: Any, control: Any, **kwargs: Any) -> None:
        del args, state, kwargs
        self._request_preemption(control)


def _discover_ffn_targets(model: Any) -> list[str]:
    candidates: set[str] = set()
    for name, module in model.named_modules():
        if not _is_ffn_key(name):
            continue
        if hasattr(module, "weight") and name:
            candidates.add(name.rsplit(".", 1)[-1])
    if not candidates:
        raise ValueError("could not discover FFN linear modules for LoRA")
    return sorted(candidates)


def _dataset_from_records(
    tokenizer: Any,
    records: Iterable[dict[str, Any]],
    maximum_tokens: int,
) -> Any:
    from datasets import Dataset

    formatted = [
        _format_training_example(
            tokenizer,
            str(item["prompt"]),
            str(item["response"]),
            maximum_tokens,
        )
        for item in records
    ]
    return Dataset.from_list(formatted)


def _evaluate_loss(trainer: Any, dataset: Any) -> float | None:
    if dataset is None or len(dataset) == 0:
        return None
    metrics = trainer.evaluate(eval_dataset=dataset)
    value = metrics.get("eval_loss")
    return float(value) if value is not None and math.isfinite(float(value)) else None


def _train_lora_active(arguments: argparse.Namespace) -> int:
    import torch
    from peft import LoraConfig, PeftModel, get_peft_model, prepare_model_for_kbit_training
    from transformers import (
        AutoModelForCausalLM,
        AutoTokenizer,
        BitsAndBytesConfig,
        Trainer,
        TrainerCallback,
        TrainingArguments,
    )
    from transformers.trainer_utils import get_last_checkpoint

    if arguments.require_cuda and not torch.cuda.is_available():
        raise RuntimeError("CUDA is required but unavailable")
    arguments.output.mkdir(parents=True, exist_ok=True)
    arguments.job_dir.mkdir(parents=True, exist_ok=True)
    arguments.control_dir.mkdir(parents=True, exist_ok=True)

    episodes = _load_episodes(arguments.episodes, arguments.skill)
    seed_records = _load_seed_records(
        arguments.seed_dataset,
        arguments.skill,
        max(0, arguments.seed_replay_limit),
    )
    combined_records = _filter_banned_learning(episodes + seed_records, arguments.episodes)
    if not combined_records:
        raise ValueError(
            f"no weight-eligible live episodes or seed records found for skill {arguments.skill!r}"
        )
    # Deterministic split keeps reruns and crash resumes reproducible. Live episodes are
    # listed first so fresh verified experience is never displaced by seed replay.
    split = max(1, int(len(combined_records) * 0.8))
    train_records = combined_records[:split]
    eval_records = combined_records[split:] or combined_records[-min(2, len(combined_records)) :]
    anchor_records = _load_anchor_records(arguments.anchors)

    tokenizer = AutoTokenizer.from_pretrained(arguments.base_model, use_fast=True)
    if tokenizer.pad_token_id is None:
        tokenizer.pad_token = tokenizer.eos_token

    use_module = arguments.module is not None and arguments.module.is_dir()
    model_kwargs: dict[str, Any] = {
        "low_cpu_mem_usage": True,
        "torch_dtype": _torch_dtype(torch),
    }
    if torch.cuda.is_available():
        model_kwargs["device_map"] = "auto"
        if not use_module:
            model_kwargs["quantization_config"] = BitsAndBytesConfig(
                load_in_4bit=True,
                bnb_4bit_quant_type="nf4",
                bnb_4bit_use_double_quant=True,
                bnb_4bit_compute_dtype=_torch_dtype(torch),
            )

    model = AutoModelForCausalLM.from_pretrained(arguments.base_model, **model_kwargs)
    if use_module:
        _apply_module_delta(model, arguments.module, arguments.base_model)
    else:
        model = prepare_model_for_kbit_training(model, use_gradient_checkpointing=True)

    if arguments.resume_adapter is not None and arguments.resume_adapter.is_dir():
        model = PeftModel.from_pretrained(model, arguments.resume_adapter, is_trainable=True)
    else:
        targets = _discover_ffn_targets(model)
        model = get_peft_model(
            model,
            LoraConfig(
                r=arguments.rank,
                lora_alpha=arguments.alpha,
                lora_dropout=0.05,
                target_modules=targets,
                bias="none",
                task_type="CAUSAL_LM",
            ),
        )

    train_dataset = _dataset_from_records(tokenizer, train_records, arguments.max_sequence_tokens)
    eval_dataset = _dataset_from_records(tokenizer, eval_records, arguments.max_sequence_tokens)
    anchor_dataset = (
        _dataset_from_records(tokenizer, anchor_records, arguments.max_sequence_tokens)
        if anchor_records
        else None
    )

    class BridgeCallback(TrainerCallback, PriorityCallback):
        def __init__(self, control_dir: Path) -> None:
            PriorityCallback.__init__(self, control_dir)

        def on_step_begin(self, args: Any, state: Any, control: Any, **kwargs: Any) -> None:
            PriorityCallback.on_step_begin(self, args, state, control, **kwargs)

        def on_log(self, args: Any, state: Any, control: Any, logs: Any = None, **kwargs: Any) -> None:
            del args, control, kwargs
            payload = {
                "step": state.global_step,
                "max_steps": state.max_steps,
                "metrics": logs or {},
                "updated_unix_ms": int(time.time() * 1000),
            }
            atomic_write_text(arguments.job_dir / "progress.json", json.dumps(payload, indent=2))
            print("THREADSPIRE_PROGRESS " + json.dumps(payload), flush=True)

    training_args = TrainingArguments(
        output_dir=str(arguments.job_dir / "trainer"),
        max_steps=max(1, arguments.max_steps),
        per_device_train_batch_size=1,
        per_device_eval_batch_size=1,
        gradient_accumulation_steps=4,
        learning_rate=arguments.learning_rate,
        logging_steps=1,
        save_strategy="steps",
        save_steps=max(1, arguments.checkpoint_steps),
        save_total_limit=2,
        eval_strategy="no",
        report_to=[],
        remove_unused_columns=False,
        bf16=bool(torch.cuda.is_available() and torch.cuda.is_bf16_supported()),
        fp16=bool(torch.cuda.is_available() and not torch.cuda.is_bf16_supported()),
        gradient_checkpointing=True,
        optim="paged_adamw_8bit" if torch.cuda.is_available() else "adamw_torch",
    )
    priority_callback = BridgeCallback(arguments.control_dir)
    trainer = Trainer(
        model=model,
        args=training_args,
        train_dataset=train_dataset,
        eval_dataset=eval_dataset,
        data_collator=CausalCollator(tokenizer),
        callbacks=[priority_callback],
    )

    baseline_skill_loss = _evaluate_loss(trainer, eval_dataset)
    baseline_anchor_loss = _evaluate_loss(trainer, anchor_dataset)
    checkpoint_root = arguments.job_dir / "trainer"
    last_checkpoint = get_last_checkpoint(str(checkpoint_root)) if checkpoint_root.is_dir() else None
    train_result = trainer.train(resume_from_checkpoint=last_checkpoint)
    if priority_callback.preempted or _inference_requested(arguments.control_dir):
        trainer.save_state()
        payload = {
            "state": "preempted",
            "step": trainer.state.global_step,
            "checkpoint": get_last_checkpoint(str(checkpoint_root)) if checkpoint_root.is_dir() else None,
            "updated_unix_ms": int(time.time() * 1000),
        }
        atomic_write_text(arguments.job_dir / "progress.json", json.dumps(payload, indent=2))
        print("THREADSPIRE_PREEMPTED " + json.dumps(payload), flush=True)
        return PREEMPTED_EXIT_CODE
    post_skill_loss = _evaluate_loss(trainer, eval_dataset)
    post_anchor_loss = _evaluate_loss(trainer, anchor_dataset)

    model.save_pretrained(arguments.output, safe_serialization=True)
    tokenizer.save_pretrained(arguments.output)

    if baseline_skill_loss is not None and post_skill_loss is not None and baseline_skill_loss > 0:
        improvement = (baseline_skill_loss - post_skill_loss) / baseline_skill_loss
    else:
        improvement = 0.0
    if baseline_anchor_loss is not None and post_anchor_loss is not None and baseline_anchor_loss > 0:
        regression = max(0.0, (post_anchor_loss - baseline_anchor_loss) / baseline_anchor_loss)
    else:
        regression = 0.0
    validation_score = max(0.0, min(1.0, 0.82 + max(-0.20, min(0.60, improvement)) * 0.25))

    metrics = {
        "skill": arguments.skill,
        "episodes": len(episodes),
        "seed_replay_records": len(seed_records),
        "baseline_skill_loss": baseline_skill_loss,
        "post_skill_loss": post_skill_loss,
        "baseline_anchor_loss": baseline_anchor_loss,
        "post_anchor_loss": post_anchor_loss,
        "skill_improvement": improvement,
        "validation_score": validation_score,
        "regression_score": regression,
        "training_loss": float(train_result.training_loss),
        "lora_rank": arguments.rank,
        "lora_alpha": arguments.alpha,
        "resumed_from_checkpoint": last_checkpoint,
        "checkpoint_steps": max(1, arguments.checkpoint_steps),
    }
    atomic_write_text(arguments.job_dir / "metrics.json", json.dumps(metrics, indent=2))
    print(json.dumps(metrics, indent=2), flush=True)
    return 0


def train_lora(arguments: argparse.Namespace) -> int:
    with _training_slot(arguments.control_dir):
        return _train_lora_active(arguments)


def _wait_for_priority(control_dir: Path, *, deadline: float | None = None) -> None:
    while (control_dir / "training.paused").exists() or _inference_requested(control_dir):
        if deadline is not None and time.monotonic() >= deadline:
            blockers = []
            if (control_dir / "training.paused").exists():
                blockers.append("training.paused")
            try:
                blockers.extend(sorted(marker.name for marker in control_dir.glob("inference.*")))
            except OSError:
                blockers.append("unreadable inference markers")
            raise TimeoutError(
                "timed out waiting for cooperative training blockers: " +
                (", ".join(blockers) if blockers else "unknown blocker")
            )
        time.sleep(0.1)


def _ffn_group(name: str) -> str:
    lowered = name.lower()
    for marker in (".mlp.", ".feed_forward.", ".ffn."):
        position = lowered.find(marker)
        if position >= 0:
            return name[: position + len(marker) - 1]
    return name.rsplit(".", 1)[0]


def _mean_dataset_loss(
    model: Any,
    dataset: Any,
    collator: Any,
    device: Any,
    dtype: Any,
    maximum_examples: int = 8,
) -> float | None:
    import torch

    if dataset is None or len(dataset) == 0:
        return None
    was_training = model.training
    model.eval()
    losses: list[float] = []
    with torch.no_grad():
        for index in range(min(len(dataset), max(1, maximum_examples))):
            batch = collator([dataset[index]])
            batch = {name: value.to(device) for name, value in batch.items()}
            context = (
                torch.autocast(device_type="cuda", dtype=dtype)
                if device.type == "cuda"
                else nullcontext()
            )
            with context:
                output = model(**batch)
            losses.append(float(output.loss.detach().float().cpu().item()))
    if was_training:
        model.train()
    return sum(losses) / len(losses) if losses else None


def _save_refinement_checkpoint(
    job_dir: Path,
    parameters: dict[str, Any],
    selected_names: list[str],
    optimizer: Any,
    completed_steps: int,
) -> None:
    import torch
    from safetensors.torch import save_file

    checkpoint_dir = job_dir / "full-rank-checkpoint"
    checkpoint_dir.mkdir(parents=True, exist_ok=True)
    weight_path = checkpoint_dir / "weights.safetensors"
    temporary_weight = checkpoint_dir / "weights.safetensors.tmp"
    save_file(
        {
            name: parameters[name].detach().cpu().to(torch.float16).contiguous()
            for name in selected_names
        },
        str(temporary_weight),
    )
    temporary_weight.replace(weight_path)

    state_path = checkpoint_dir / "state.pt"
    temporary_state = checkpoint_dir / "state.pt.tmp"
    payload: dict[str, Any] = {
        "selected_names": selected_names,
        "completed_steps": completed_steps,
        "optimizer": optimizer.state_dict(),
        "cpu_rng_state": torch.get_rng_state(),
    }
    if torch.cuda.is_available():
        payload["cuda_rng_state"] = torch.cuda.get_rng_state_all()
    torch.save(payload, temporary_state)
    temporary_state.replace(state_path)
    atomic_write_text(
        checkpoint_dir / "progress.json",
        json.dumps(
            {
                "completed_steps": completed_steps,
                "selected_parameter_count": len(selected_names),
                "updated_unix_ms": int(time.time() * 1000),
            },
            indent=2,
        ),
    )


def _restore_refinement_checkpoint(
    job_dir: Path,
    parameters: dict[str, Any],
    selected_names: list[str],
    optimizer: Any,
) -> int:
    import torch
    from safetensors.torch import load_file

    checkpoint_dir = job_dir / "full-rank-checkpoint"
    weight_path = checkpoint_dir / "weights.safetensors"
    state_path = checkpoint_dir / "state.pt"
    if not weight_path.is_file() or not state_path.is_file():
        return 0
    payload = torch.load(state_path, map_location="cpu", weights_only=False)
    stored_names = [str(value) for value in payload.get("selected_names", [])]
    if stored_names != selected_names:
        return 0
    tensors = load_file(str(weight_path), device="cpu")
    with torch.no_grad():
        for name in selected_names:
            value = tensors.get(name)
            if value is None or tuple(value.shape) != tuple(parameters[name].shape):
                raise ValueError(f"full-rank checkpoint is incompatible with {name}")
            parameters[name].copy_(
                value.to(device=parameters[name].device, dtype=parameters[name].dtype)
            )
    optimizer.load_state_dict(payload["optimizer"])
    torch.set_rng_state(payload["cpu_rng_state"])
    if torch.cuda.is_available() and "cuda_rng_state" in payload:
        torch.cuda.set_rng_state_all(payload["cuda_rng_state"])
    return max(0, int(payload.get("completed_steps", 0)))


def _consolidate_module_active(arguments: argparse.Namespace) -> int:
    import torch
    from peft import PeftModel
    from safetensors.torch import save_file
    from transformers import AutoModelForCausalLM, AutoTokenizer

    if arguments.require_cuda and not torch.cuda.is_available():
        raise RuntimeError("CUDA is required but unavailable")
    _wait_for_priority(arguments.control_dir)
    arguments.output.mkdir(parents=True, exist_ok=True)
    arguments.job_dir.mkdir(parents=True, exist_ok=True)

    dtype = _torch_dtype(torch)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    tokenizer = AutoTokenizer.from_pretrained(arguments.base_model, use_fast=True)
    if tokenizer.pad_token_id is None:
        tokenizer.pad_token = tokenizer.eos_token

    # The immutable base snapshot is retained on CPU so the published expert is a
    # cumulative delta against a stable neural ABI, not against a transient LoRA.
    model = AutoModelForCausalLM.from_pretrained(
        arguments.base_model,
        low_cpu_mem_usage=True,
        torch_dtype=dtype,
    )
    base_ffn = {
        name: parameter.detach().cpu().to(torch.float16).clone()
        for name, parameter in model.named_parameters()
        if _is_ffn_key(name)
    }
    if not base_ffn:
        raise ValueError("base model exposes no recognized FFN parameters")
    if arguments.prior_module is not None and arguments.prior_module.is_dir():
        _apply_module_delta(model, arguments.prior_module, arguments.base_model)

    adapted = PeftModel.from_pretrained(model, arguments.adapter)
    merged = adapted.merge_and_unload(progressbar=True)
    del adapted
    merged.to(device)
    merged.config.use_cache = False
    if hasattr(merged, "gradient_checkpointing_enable"):
        merged.gradient_checkpointing_enable()
    if hasattr(merged, "enable_input_require_grads"):
        merged.enable_input_require_grads()

    parameters = dict(merged.named_parameters())
    group_scores: dict[str, float] = {}
    with torch.no_grad():
        for name, parameter in parameters.items():
            baseline = base_ffn.get(name)
            if baseline is None:
                continue
            delta = parameter.detach().cpu().to(torch.float32) - baseline.to(torch.float32)
            group = _ffn_group(name)
            group_scores[group] = group_scores.get(group, 0.0) + float(
                torch.sum(delta * delta).item()
            )
    selected_groups = [
        group
        for group, _ in sorted(
            group_scores.items(), key=lambda item: (-item[1], item[0])
        )[: max(1, arguments.layers_per_pass)]
    ]
    selected_names = sorted(
        name
        for name in parameters
        if _is_ffn_key(name) and _ffn_group(name) in selected_groups
    )
    if not selected_names:
        raise ValueError("no FFN parameter group was selected for full-rank refinement")

    for parameter in merged.parameters():
        parameter.requires_grad_(False)
    trainable_parameters = []
    for name in selected_names:
        parameters[name].requires_grad_(True)
        trainable_parameters.append(parameters[name])

    episodes = _load_episodes(arguments.episodes, arguments.skill)
    seed_records = _load_seed_records(
        arguments.seed_dataset,
        arguments.skill,
        max(0, arguments.seed_replay_limit),
    )
    records = _filter_banned_learning(episodes + seed_records, arguments.episodes)
    if not records:
        raise ValueError("full-rank refinement requires verified episodes or seed replay")
    split = max(1, int(len(records) * 0.8))
    train_records = records[:split]
    eval_records = records[split:] or records[-min(2, len(records)) :]
    anchor_records = _load_anchor_records(arguments.anchors)
    train_dataset = _dataset_from_records(
        tokenizer, train_records, arguments.max_sequence_tokens
    )
    eval_dataset = _dataset_from_records(
        tokenizer, eval_records, arguments.max_sequence_tokens
    )
    anchor_dataset = (
        _dataset_from_records(tokenizer, anchor_records, arguments.max_sequence_tokens)
        if anchor_records
        else None
    )
    collator = CausalCollator(tokenizer)

    baseline_skill_loss = _mean_dataset_loss(
        merged, eval_dataset, collator, device, dtype
    )
    baseline_anchor_loss = _mean_dataset_loss(
        merged, anchor_dataset, collator, device, dtype
    )

    optimizer = torch.optim.AdamW(
        trainable_parameters,
        lr=max(1.0e-8, arguments.learning_rate),
        betas=(0.9, 0.95),
        weight_decay=0.01,
    )
    completed_steps = _restore_refinement_checkpoint(
        arguments.job_dir, parameters, selected_names, optimizer
    )
    accumulation_steps = 2
    scaler = torch.amp.GradScaler(
        "cuda",
        enabled=bool(device.type == "cuda" and dtype == torch.float16),
    )
    merged.train()
    optimizer.zero_grad(set_to_none=True)
    losses: list[float] = []
    requested_steps = max(1, arguments.max_steps)

    while completed_steps < requested_steps:
        if (arguments.control_dir / "training.paused").exists() or _inference_requested(
            arguments.control_dir
        ):
            _save_refinement_checkpoint(
                arguments.job_dir,
                parameters,
                selected_names,
                optimizer,
                completed_steps,
            )
            payload = {
                "state": "preempted",
                "step": completed_steps,
                "selected_groups": selected_groups,
                "updated_unix_ms": int(time.time() * 1000),
            }
            atomic_write_text(
                arguments.job_dir / "progress.json", json.dumps(payload, indent=2)
            )
            print("THREADSPIRE_PREEMPTED " + json.dumps(payload), flush=True)
            return PREEMPTED_EXIT_CODE

        for micro_step in range(accumulation_steps):
            sample_index = (
                completed_steps * accumulation_steps + micro_step
            ) % len(train_dataset)
            batch = collator([train_dataset[sample_index]])
            batch = {name: value.to(device) for name, value in batch.items()}
            context = (
                torch.autocast(device_type="cuda", dtype=dtype)
                if device.type == "cuda"
                else nullcontext()
            )
            with context:
                output = merged(**batch)
                loss = output.loss / accumulation_steps
            if scaler.is_enabled():
                scaler.scale(loss).backward()
            else:
                loss.backward()
            losses.append(float(output.loss.detach().float().cpu().item()))

        if scaler.is_enabled():
            scaler.unscale_(optimizer)
        torch.nn.utils.clip_grad_norm_(trainable_parameters, 1.0)
        if scaler.is_enabled():
            scaler.step(optimizer)
            scaler.update()
        else:
            optimizer.step()
        optimizer.zero_grad(set_to_none=True)
        completed_steps += 1

        payload = {
            "state": "full-rank-refinement",
            "step": completed_steps,
            "max_steps": requested_steps,
            "loss": losses[-1] if losses else None,
            "selected_groups": selected_groups,
            "updated_unix_ms": int(time.time() * 1000),
        }
        atomic_write_text(
            arguments.job_dir / "progress.json", json.dumps(payload, indent=2)
        )
        print("THREADSPIRE_PROGRESS " + json.dumps(payload), flush=True)
        if completed_steps % max(1, arguments.checkpoint_steps) == 0:
            _save_refinement_checkpoint(
                arguments.job_dir,
                parameters,
                selected_names,
                optimizer,
                completed_steps,
            )

    post_skill_loss = _mean_dataset_loss(
        merged, eval_dataset, collator, device, dtype
    )
    post_anchor_loss = _mean_dataset_loss(
        merged, anchor_dataset, collator, device, dtype
    )
    skill_change = (
        (baseline_skill_loss - post_skill_loss) / baseline_skill_loss
        if baseline_skill_loss is not None
        and post_skill_loss is not None
        and baseline_skill_loss > 0
        else 0.0
    )
    regression = (
        max(0.0, (post_anchor_loss - baseline_anchor_loss) / baseline_anchor_loss)
        if baseline_anchor_loss is not None
        and post_anchor_loss is not None
        and baseline_anchor_loss > 0
        else 0.0
    )
    retention_score = max(0.0, min(1.0, 1.0 + min(0.0, skill_change)))

    deltas: dict[str, Any] = {}
    merge_max_abs_error = 0.0
    with torch.no_grad():
        for name, parameter in merged.named_parameters():
            baseline = base_ffn.get(name)
            if baseline is None:
                continue
            merged_value = parameter.detach().cpu().to(torch.float32)
            delta = merged_value - baseline.to(torch.float32)
            if torch.count_nonzero(delta).item() != 0:
                stored = delta.to(torch.float16).contiguous()
                reconstructed = baseline.to(torch.float32) + stored.to(torch.float32)
                merge_max_abs_error = max(
                    merge_max_abs_error,
                    float(torch.max(torch.abs(merged_value - reconstructed)).item()),
                )
                deltas[name] = stored
    if not deltas:
        raise ValueError("incubated adapter and refinement produced no FFN deltas")

    weights_file = "expert.safetensors"
    weights_path = arguments.output / weights_file
    save_file(deltas, str(weights_path))
    metrics = {
        "skill": arguments.skill,
        "episodes": len(episodes),
        "seed_replay_records": len(seed_records),
        "baseline_skill_loss": baseline_skill_loss,
        "post_skill_loss": post_skill_loss,
        "baseline_anchor_loss": baseline_anchor_loss,
        "post_anchor_loss": post_anchor_loss,
        "skill_change": skill_change,
        "retention_score": retention_score,
        "regression_score": regression,
        "full_rank_refinement_steps": completed_steps,
        "selected_groups": selected_groups,
        "selected_parameter_count": sum(
            parameters[name].numel() for name in selected_names
        ),
        "mean_training_loss": sum(losses) / len(losses) if losses else None,
    }
    atomic_write_text(arguments.job_dir / "metrics.json", json.dumps(metrics, indent=2))
    manifest = {
        "format": "threadspire.external-skill-delta.v1",
        "skill": arguments.skill,
        "base_model": arguments.base_model,
        "base_fingerprint": _model_fingerprint(arguments.base_model),
        "weights_file": weights_file,
        "weights_sha256": _sha256_file(weights_path),
        "tensor_count": len(deltas),
        "parameter_count": sum(tensor.numel() for tensor in deltas.values()),
        "dtype": "float16",
        "source_adapter": str(arguments.adapter),
        "prior_module": str(arguments.prior_module) if arguments.prior_module else None,
        "created_unix_ms": int(time.time() * 1000),
        "routing_granularity": "request",
        "training_method": "lora-incubation-plus-full-rank-ffn-refinement",
        "full_rank_refinement_steps": completed_steps,
        "refined_groups": selected_groups,
        "refined_parameter_count": metrics["selected_parameter_count"],
        "merge_max_abs_error": merge_max_abs_error,
        "notes": (
            "Request-routed cumulative FFN delta pack. The selected FFN matrices were "
            "updated directly after LoRA incubation; shared embeddings and attention remain "
            "in the immutable base core."
        ),
    }
    atomic_write_text(arguments.output / "module.json", json.dumps(manifest, indent=2))
    print(json.dumps({"manifest": manifest, "metrics": metrics}, indent=2), flush=True)
    return 0


def consolidate_module(arguments: argparse.Namespace) -> int:
    with _training_slot(arguments.control_dir):
        return _consolidate_module_active(arguments)


_TEXT_EXTENSIONS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ixx", ".cppm",
    ".cmake", ".txt", ".json", ".toml", ".yaml", ".yml", ".md", ".bat", ".cmd",
    ".ps1", ".py", ".js", ".ts", ".html", ".css", ".xml", ".ini",
}


def _read_limited(path: Path, maximum: int) -> str:
    if not path.is_file():
        return ""
    data = path.read_bytes()
    if len(data) > maximum:
        data = data[-maximum:]
    return data.decode("utf-8", errors="replace")


def _tracked_files(repository: Path) -> list[Path]:
    process = subprocess.run(
        ["git", "-C", str(repository), "ls-files", "-z"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    result: list[Path] = []
    for item in process.stdout.split(b"\0"):
        if not item:
            continue
        relative = Path(item.decode("utf-8", errors="replace"))
        if relative.suffix.lower() in _TEXT_EXTENSIONS or relative.name in {
            "CMakeLists.txt", "CMakePresets.json", "Makefile",
        }:
            result.append(relative)
    return result


def _diagnostic_paths(diagnostics: str, candidates: list[Path]) -> list[Path]:
    lowered = diagnostics.lower().replace("\\", "/")
    scored: list[tuple[int, Path]] = []
    for path in candidates:
        normalized = path.as_posix().lower()
        score = 0
        if normalized in lowered:
            score += 100
        if path.name.lower() in lowered:
            score += 30
        if path.name in {"CMakeLists.txt", "CMakePresets.json"}:
            score += 10
        if path.suffix.lower() in {".cpp", ".cxx", ".cc", ".hpp", ".h", ".ixx", ".cppm"}:
            score += 5
        scored.append((score, path))
    scored.sort(key=lambda item: (-item[0], item[1].as_posix()))
    return [path for _, path in scored]


def _repository_context(repository: Path, diagnostics: str, maximum_chars: int) -> str:
    pieces: list[str] = []
    used = 0
    for relative in _diagnostic_paths(diagnostics, _tracked_files(repository)):
        absolute = repository / relative
        try:
            text = absolute.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        header = f"\n===== FILE: {relative.as_posix()} =====\n"
        remaining = maximum_chars - used - len(header)
        if remaining <= 0:
            break
        if len(text) > remaining:
            text = text[:remaining]
        pieces.append(header + text)
        used += len(header) + len(text)
        if used >= maximum_chars:
            break
    return "".join(pieces)


def _extract_unified_diff(text: str) -> str:
    fenced = re.search(r"```(?:diff|patch)?\s*\n(.*?)```", text, flags=re.DOTALL | re.IGNORECASE)
    candidate = fenced.group(1) if fenced else text
    starts = [position for marker in ("diff --git ", "--- a/", "--- ") if (position := candidate.find(marker)) >= 0]
    if not starts:
        raise ValueError("model response contained no unified diff")
    candidate = candidate[min(starts):].strip()
    if "\n+++ " not in candidate or "\n@@ " not in candidate:
        raise ValueError("model response was not a complete unified diff")
    return candidate + "\n"


def repair(arguments: argparse.Namespace) -> int:
    task = _read_limited(arguments.task_file, 32_000).strip()
    diagnostics = _read_limited(arguments.diagnostics, 64_000)
    if not task:
        raise ValueError("task file is empty")
    context_budget = max(32_000, arguments.max_context_chars - len(diagnostics) - len(task))
    context = _repository_context(arguments.repository, diagnostics, context_budget)
    if not context:
        raise ValueError("repository contains no tracked text files suitable for repair")

    cache = ModelCache(1)
    loaded = cache.get(
        arguments.base_model,
        str(arguments.adapter) if arguments.adapter else "",
        str(arguments.module) if arguments.module else "",
    )
    messages = [
        {
            "role": "system",
            "content": (
                "You are a C++23 repository repair engine. Return only one complete unified diff "
                "that applies at the repository root. Do not use markdown prose. Preserve existing "
                "architecture unless the task requires a change. Do not fabricate compiler results."
            ),
        },
        {
            "role": "user",
            "content": (
                f"Task:\n{task}\n\nPhase: {arguments.phase}\nIteration: {arguments.iteration}"
                f"\n\nCompiler or test diagnostics:\n{diagnostics}"
                f"\n\nTracked repository context:\n{context}"
                "\n\nProduce the smallest complete repair as a unified diff."
            ),
        },
    ]
    payload = {
        "model": arguments.base_model,
        "messages": messages,
        "max_tokens": max(256, min(arguments.max_new_tokens, 8192)),
        "temperature": 0.05,
        "adapter_path": str(arguments.adapter) if arguments.adapter else "",
        "module_path": str(arguments.module) if arguments.module else "",
    }
    state = ServerState(arguments.repository, 1)
    state.cache = cache
    generated = _generate(state, payload)
    patch = _extract_unified_diff(str(generated["content"]))
    atomic_write_text(arguments.output_diff, patch)
    print(
        json.dumps(
            {
                "phase": arguments.phase,
                "iteration": arguments.iteration,
                "patch": str(arguments.output_diff),
                "characters": len(patch),
                "tokens_per_second": generated.get("tokens_per_second", 0.0),
            },
            indent=2,
        ),
        flush=True,
    )
    cache.clear()
    return 0


def _mcp_write(process: subprocess.Popen[str], message: dict[str, Any]) -> None:
    if process.stdin is None:
        raise RuntimeError("MCP server stdin is unavailable")
    process.stdin.write(json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n")
    process.stdin.flush()


def _mcp_read_response(
    process: subprocess.Popen[str],
    request_id: int,
    timeout_seconds: float = 30.0,
) -> dict[str, Any]:
    if process.stdout is None:
        raise RuntimeError("MCP server stdout is unavailable")
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        line = process.stdout.readline()
        if not line:
            if process.poll() is not None:
                raise RuntimeError(f"MCP server exited with code {process.returncode}")
            time.sleep(0.01)
            continue
        try:
            message = json.loads(line)
        except json.JSONDecodeError as exception:
            raise RuntimeError(f"MCP server wrote non-JSON data to stdout: {exception}") from exception
        if isinstance(message, dict) and message.get("id") == request_id:
            return message
    raise TimeoutError(f"MCP request {request_id} timed out")


def _mcp_content_text(result: dict[str, Any]) -> str:
    pieces: list[str] = []
    content = result.get("content", [])
    if isinstance(content, list):
        for item in content:
            if not isinstance(item, dict):
                continue
            if item.get("type") == "text" and item.get("text"):
                pieces.append(str(item["text"]))
            elif item.get("type") in {"resource", "resource_link", "image", "audio"}:
                pieces.append(json.dumps(item, ensure_ascii=False))
    return "\n".join(pieces) if pieces else json.dumps(result, ensure_ascii=False)


def mcp_stdio(arguments: argparse.Namespace) -> int:
    request = json.loads(arguments.request.read_text(encoding="utf-8"))
    executable = str(request.get("executable", "")).strip()
    command_arguments = request.get("arguments", [])
    if not executable:
        raise ValueError("stdio MCP executable is empty")
    if not isinstance(command_arguments, list):
        raise ValueError("stdio MCP arguments must be an array")
    output = Path(str(request.get("output", "")))
    if not output:
        raise ValueError("stdio MCP output path is empty")

    process = subprocess.Popen(
        [executable, *[str(value) for value in command_arguments]],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="strict",
        bufsize=1,
    )
    try:
        _mcp_write(
            process,
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-11-25",
                    "capabilities": {},
                    "clientInfo": {"name": "EngCoder", "version": "0.7.4"},
                },
            },
        )
        initialized = _mcp_read_response(process, 1)
        if "error" in initialized:
            raise RuntimeError(str(initialized["error"]))
        protocol_version = str(
            initialized.get("result", {}).get("protocolVersion", "2025-11-25")
        )
        _mcp_write(
            process,
            {
                "jsonrpc": "2.0",
                "method": "notifications/initialized",
                "params": {},
            },
        )

        action = str(request.get("action", "list-tools"))
        if action == "list-tools":
            _mcp_write(
                process,
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "tools/list",
                    "params": {},
                },
            )
            response = _mcp_read_response(process, 2)
            if "error" in response:
                raise RuntimeError(str(response["error"]))
            tools = []
            for item in response.get("result", {}).get("tools", []):
                if not isinstance(item, dict) or not item.get("name"):
                    continue
                tools.append(
                    {
                        "name": str(item["name"]),
                        "description": str(item.get("description", "")),
                        "input_schema": item.get("inputSchema", {}),
                    }
                )
            payload = {
                "succeeded": True,
                "message": f"{len(tools)} MCP tool(s) available",
                "protocol_version": protocol_version,
                "tools": tools,
            }
        elif action == "call-tool":
            _mcp_write(
                process,
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "tools/call",
                    "params": {
                        "name": str(request.get("tool_name", "")),
                        "arguments": request.get("tool_arguments", {}),
                    },
                },
            )
            response = _mcp_read_response(process, 3, 120.0)
            if "error" in response:
                payload = {
                    "succeeded": False,
                    "content": "",
                    "error": str(response["error"]),
                    "is_error": True,
                }
            else:
                result = response.get("result", {})
                is_error = bool(result.get("isError", False))
                payload = {
                    "succeeded": not is_error,
                    "content": _mcp_content_text(result),
                    "error": "MCP tool reported an error" if is_error else "",
                    "is_error": is_error,
                }
        else:
            raise ValueError(f"unsupported MCP stdio action: {action}")
        atomic_write_text(output, json.dumps(payload, ensure_ascii=False, indent=2))
        return 0
    finally:
        try:
            process.terminate()
            process.wait(timeout=2.0)
        except Exception:
            process.kill()
            process.wait(timeout=2.0)


def inspect_module(arguments: argparse.Namespace) -> int:
    manifest = _validate_module_storage(arguments.module)
    print(json.dumps({**manifest, "validated": True}, indent=2), flush=True)
    return 0



def _internal_profile(name: str) -> internal.ModelConfig:
    if name == "standard":
        return internal.ModelConfig(
            name="EngCoder Standard",
            context_length=1536,
            d_model=512,
            n_layers=10,
            n_heads=8,
            ffn_hidden=1536,
            expert_hidden=256,
        )
    if name == "large-local":
        return internal.ModelConfig(
            name="EngCoder Large",
            context_length=2048,
            d_model=640,
            n_layers=12,
            n_heads=10,
            ffn_hidden=1920,
            expert_hidden=320,
        )
    if name == "desktop-250m":
        return internal.ModelConfig(
            name="EngCoder Desktop 250M",
            context_length=2048,
            d_model=896,
            n_layers=18,
            n_heads=14,
            ffn_hidden=2688,
            expert_hidden=192,
        )
    if name == "desktop-16gb":
        # Target the user's single 16 GiB GPU plan: roughly a 260M KeyMaker
        # Foundation plus small independently routed capability lanes. The
        # complete generation remains comfortably below a monolithic 1B model
        # while still being large enough for a serious month-scale scratch run.
        return internal.ModelConfig(
            name="EngCoder Desktop 16GB",
            context_length=2048,
            d_model=1024,
            n_layers=18,
            n_heads=16,
            ffn_hidden=3072,
            expert_hidden=256,
        )
    return internal.ModelConfig(name="EngCoder Micro")


def init_internal(arguments: argparse.Namespace) -> int:
    progress_file = arguments.progress_file or (
        arguments.state_root / "state" / "internal-model-progress.json"
    )
    internal.write_progress(
        progress_file,
        phase="initializing",
        message=f"Creating random weights for the {arguments.profile} internal model",
        profile=arguments.profile,
        percent=0.0,
    )
    try:
        payload = internal.initialize_model(
            arguments.state_root,
            _internal_profile(arguments.profile),
            seed=arguments.seed,
            force=arguments.force,
        )
    except Exception as exception:
        internal.write_progress(
            progress_file,
            phase="failed",
            message=f"{type(exception).__name__}: {exception}",
            error_type=type(exception).__name__,
            error=str(exception),
            resumable=False,
        )
        raise
    internal.write_progress(
        progress_file,
        phase="initialized",
        message="Random internal-model weights are ready for resumable bootstrap training",
        generation=str(payload.get("generation", "")),
        parameter_count=int(payload.get("parameter_count", 0)),
        total_steps=int(payload.get("steps", 0)),
        tokens_seen=int(payload.get("tokens_seen", 0)),
        bootstrap_target_steps=int(payload.get("bootstrap_target_steps", 0)),
        bootstrap_complete=bool(payload.get("bootstrap_complete", False)),
        percent=0.0,
    )
    print(json.dumps(payload, indent=2), flush=True)
    return 0


def train_internal(arguments: argparse.Namespace) -> int:
    # Normalize path-like parser values defensively at the worker boundary.
    data_paths = [Path(arguments.seed_dataset)]
    if arguments.episodes is not None:
        data_paths.append(Path(arguments.episodes))
    anchor_paths = [Path(arguments.anchors)] if arguments.anchors is not None else []
    control_dir = arguments.control_dir or (arguments.state_root / "control")
    progress_file = arguments.progress_file or (
        arguments.state_root / "state" / "internal-model-progress.json"
    )
    internal.write_progress(
        progress_file,
        phase="queued",
        message="Internal-model training is queued",
        maximum_steps=max(1, arguments.max_steps),
        bootstrap_target_steps=max(0, arguments.bootstrap_target_steps),
        foreground_bootstrap=bool(arguments.foreground_bootstrap),
    )
    try:
        with _training_slot(
            control_dir,
            foreground=bool(arguments.foreground_bootstrap),
            progress_file=progress_file,
            timeout_seconds=max(1.0, arguments.slot_timeout_seconds),
        ):
            metrics = internal.train_candidate(
                state_root=arguments.state_root,
                data_paths=data_paths,
                anchor_paths=anchor_paths,
                scope=arguments.scope,
                maximum_steps=max(1, arguments.max_steps),
                learning_rate=arguments.learning_rate,
                batch_size=max(1, arguments.batch_size),
                accumulation_steps=max(1, arguments.accumulation_steps),
                checkpoint_steps=max(1, arguments.checkpoint_steps),
                control_dir=control_dir,
                require_cuda=arguments.require_cuda,
                seed=arguments.seed,
                defer_promotion=arguments.defer_promotion,
                progress_file=progress_file,
                bootstrap_target_steps=max(0, arguments.bootstrap_target_steps),
                honor_inference_priority=not arguments.foreground_bootstrap,
            )
    except Exception as exception:
        checkpoint_root = arguments.state_root / "models" / "internal" / "checkpoints"
        resumable = checkpoint_root.is_dir() and any(checkpoint_root.rglob("latest.pt"))
        internal.write_progress(
            progress_file,
            phase="failed",
            message=f"{type(exception).__name__}: {exception}",
            error_type=type(exception).__name__,
            error=str(exception),
            resumable=resumable,
            foreground_bootstrap=bool(arguments.foreground_bootstrap),
        )
        raise
    if arguments.metrics_output is not None:
        atomic_write_text(arguments.metrics_output, json.dumps(metrics, indent=2) + "\n")
    print(json.dumps(metrics, indent=2), flush=True)
    if arguments.foreground_bootstrap and not (
        bool(metrics.get("promoted_bool", False))
        and bool(metrics.get("bootstrap_complete", False))
    ):
        # A rejected shadow candidate is a recoverable training outcome, but it
        # is not a successful foreground bootstrap and the host must not start.
        return 3
    return int(metrics.get("exit_code", 0))


def internal_status(arguments: argparse.Namespace) -> int:
    print(json.dumps(internal.status_payload(arguments.state_root), indent=2), flush=True)
    return 0


def promote_internal(arguments: argparse.Namespace) -> int:
    payload = internal.promote_generation(
        arguments.state_root,
        arguments.generation,
        arguments.expected_parent,
    )
    print(json.dumps(payload, indent=2), flush=True)
    return 0


def activate_internal(arguments: argparse.Namespace) -> int:
    payload = internal.activate_generation(
        arguments.state_root,
        arguments.generation,
        arguments.expected_current,
    )
    print(json.dumps(payload, indent=2), flush=True)
    return 0


def main() -> int:
    arguments = parse_args()
    if arguments.command == "doctor":
        return doctor()
    if arguments.command == "serve":
        return serve(arguments)
    if arguments.command == "init-internal":
        return init_internal(arguments)
    if arguments.command == "train-internal":
        return train_internal(arguments)
    if arguments.command == "internal-status":
        return internal_status(arguments)
    if arguments.command == "promote-internal":
        return promote_internal(arguments)
    if arguments.command == "activate-internal":
        return activate_internal(arguments)
    if arguments.command == "train-lora":
        return train_lora(arguments)
    if arguments.command == "consolidate-module":
        return consolidate_module(arguments)
    if arguments.command == "repair":
        return repair(arguments)
    if arguments.command == "inspect-module":
        return inspect_module(arguments)
    if arguments.command == "mcp-stdio":
        return mcp_stdio(arguments)
    raise AssertionError(arguments.command)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exception:
        print(f"FATAL {type(exception).__name__}: {exception}", file=sys.stderr, flush=True)
        raise
