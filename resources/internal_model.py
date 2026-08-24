#!/usr/bin/env python3
"""ThreadSpire's locally initialized language model.

This module contains no downloaded model weights.  It defines a compact
byte-token decoder transformer, its modular skill experts, checkpoint format,
streaming generation, and resumable local training loop.
"""

from __future__ import annotations
try:
    from . import curriculum_guard as _threadspire_curriculum_guard
except ImportError:
    import curriculum_guard as _threadspire_curriculum_guard
_threadspire_curriculum_guard.install()


from collections import Counter
from dataclasses import asdict, dataclass, field
import codecs
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import time
from typing import Any, Iterable, Iterator, Sequence, TypeAlias

import torch
from torch import Tensor, nn
import torch.nn.functional as F
from safetensors.torch import load_file, save_file


PAD_ID = 0
BOS_ID = 1
EOS_ID = 2
SYSTEM_ID = 3
USER_ID = 4
ASSISTANT_ID = 5
SEPARATOR_ID = 6
BYTE_OFFSET = 16
VOCAB_SIZE = BYTE_OFFSET + 256
FORMAT_VERSION = "threadspire.internal.v1"
BOOTSTRAP_POLICY = "full-backbone-english-v1"


SkillRoute: TypeAlias = tuple[str, ...]
SkillRoutes: TypeAlias = tuple[SkillRoute, ...]
SkillInput: TypeAlias = Sequence[str] | Sequence[Sequence[str]]
TrainingSequence: TypeAlias = tuple[list[int], list[int], SkillRoute]


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, path)


def update_english_foundation_state(
    state_root: Path,
    *,
    generation: str,
    evaluation_passed: bool,
    promoted: bool,
    bootstrap_target_steps: int,
    bootstrap_complete: bool,
    full_backbone_trained: bool,
    generation_ready: bool,
    total_steps: int,
    validation_loss: float,
) -> bool:
    """Persist the honest usability gate from the active English generation."""

    path = state_root / "state" / "curriculum-state.json"
    existing: dict[str, Any] = {}
    try:
        loaded = json.loads(path.read_text(encoding="utf-8-sig"))
        if isinstance(loaded, dict):
            existing = loaded
    except (OSError, ValueError, TypeError):
        pass

    prior = existing.get("english_foundation", {})
    if not isinstance(prior, dict):
        prior = {}
    prior_evaluation = bool(prior.get("evaluation_passed", False))
    prior_established = bool(prior.get("established", False))

    accepted_active_english = bool(promoted and evaluation_passed)
    evaluation_ready = prior_evaluation or accepted_active_english
    established_now = bool(
        accepted_active_english
        and bootstrap_target_steps > 0
        and bootstrap_complete
        and full_backbone_trained
        and generation_ready
    )
    foundation_established = prior_established or established_now

    document = {
        "schema_version": 2,
        "updated_unix_ms": int(time.time() * 1000),
        "english_foundation": {
            "evaluation_passed": evaluation_ready,
            "established": foundation_established,
            "foundation_complete": foundation_established,
            "active_generation": generation if promoted else str(
                prior.get("active_generation", "")
            ),
            "last_candidate_generation": generation,
            "last_candidate_promoted": promoted,
            "last_candidate_evaluation_passed": evaluation_passed,
            "bootstrap_target_steps": max(0, int(bootstrap_target_steps)),
            "bootstrap_complete": bool(bootstrap_complete),
            "full_backbone_trained": bool(full_backbone_trained),
            "generation_ready": bool(generation_ready),
            "bootstrap_policy": BOOTSTRAP_POLICY,
            "optimizer_scope": "all" if full_backbone_trained else "english-core",
            "total_steps": max(0, int(total_steps)),
            "validation_loss": (
                float(validation_loss) if math.isfinite(validation_loss) else None
            ),
            "internal_scope": "english-core",
            "curriculum_target": "english-foundation",
        },
    }
    atomic_write_text(path, json.dumps(document, ensure_ascii=False, indent=2) + "\n")
    return foundation_established


def write_progress(
    path: Path | None,
    *,
    phase: str,
    message: str,
    **fields: Any,
) -> None:
    """Publish a crash-safe lifecycle heartbeat for the native UI."""

    if path is None:
        return
    payload: dict[str, Any] = {
        "format": "threadspire.internal-progress.v1",
        "phase": phase,
        "message": message,
        "heartbeat_unix_ms": int(time.time() * 1000),
        "pid": os.getpid(),
    }
    payload.update(fields)
    atomic_write_text(path, json.dumps(payload, ensure_ascii=False, indent=2) + "\n")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class ByteTokenizer:
    """Deterministic UTF-8 byte tokenizer owned by the application.

    It has no external vocabulary file and can represent every Unicode string
    and every source file.  English and C++ become more compact later through
    optional learned token-merging; the base format remains permanently
    decodable because bytes are the ABI.
    """

    vocab_size = VOCAB_SIZE
    pad_token_id = PAD_ID
    bos_token_id = BOS_ID
    eos_token_id = EOS_ID

    @staticmethod
    def encode_text(text: str) -> list[int]:
        return [BYTE_OFFSET + value for value in text.encode("utf-8")]

    @staticmethod
    def decode(tokens: Sequence[int]) -> str:
        data = bytes(token - BYTE_OFFSET for token in tokens if BYTE_OFFSET <= token < VOCAB_SIZE)
        return data.decode("utf-8", errors="replace")

    @classmethod
    def prompt_tokens(cls, messages: Sequence[dict[str, str]]) -> list[int]:
        result = [BOS_ID]
        role_token = {
            "system": SYSTEM_ID,
            "user": USER_ID,
            "assistant": ASSISTANT_ID,
        }
        for message in messages:
            role = str(message.get("role", "user")).lower()
            result.append(role_token.get(role, USER_ID))
            result.extend(cls.encode_text(str(message.get("content", ""))))
            result.append(SEPARATOR_ID)
        if not messages or str(messages[-1].get("role", "")).lower() != "assistant":
            result.append(ASSISTANT_ID)
        return result

    @classmethod
    def supervised_tokens(
        cls,
        messages: Sequence[dict[str, str]],
        maximum_tokens: int,
    ) -> tuple[list[int], list[int]]:
        tokens = [BOS_ID]
        labels = [-100]
        role_token = {
            "system": SYSTEM_ID,
            "user": USER_ID,
            "assistant": ASSISTANT_ID,
        }
        for message in messages:
            role = str(message.get("role", "user")).lower()
            content = str(message.get("content", ""))
            role_id = role_token.get(role, USER_ID)
            content_tokens = cls.encode_text(content)
            train_role = role == "assistant"
            tokens.append(role_id)
            labels.append(role_id if train_role else -100)
            tokens.extend(content_tokens)
            labels.extend(content_tokens if train_role else [-100] * len(content_tokens))
            tokens.append(SEPARATOR_ID)
            labels.append(SEPARATOR_ID if train_role else -100)
        tokens.append(EOS_ID)
        labels.append(EOS_ID)
        if len(tokens) > maximum_tokens:
            tokens = tokens[-maximum_tokens:]
            labels = labels[-maximum_tokens:]
            tokens[0] = BOS_ID
            labels[0] = -100
        return tokens, labels


@dataclass(slots=True)
class ModelConfig:
    name: str = "ThreadSpire Core"
    vocab_size: int = VOCAB_SIZE
    context_length: int = 1024
    d_model: int = 384
    n_layers: int = 8
    n_heads: int = 6
    ffn_hidden: int = 1152
    expert_hidden: int = 192
    skills: list[str] = field(default_factory=lambda: [
        "english-core",
        "cpp23-core",
        "cpp23-debugging",
        "planning",
    ])
    dropout: float = 0.0
    rope_base: float = 10000.0

    def validate(self) -> None:
        if self.vocab_size != VOCAB_SIZE:
            raise ValueError(f"vocab_size must be {VOCAB_SIZE} for the byte-token ABI")
        if self.d_model <= 0 or self.n_layers <= 0 or self.n_heads <= 0:
            raise ValueError("model dimensions must be positive")
        if self.d_model % self.n_heads != 0:
            raise ValueError("d_model must be divisible by n_heads")
        if self.context_length < 128:
            raise ValueError("context_length must be at least 128")
        if self.ffn_hidden < self.d_model:
            raise ValueError("ffn_hidden must be at least d_model")
        if self.expert_hidden <= 0:
            raise ValueError("expert_hidden must be positive")
        normalized = []
        for skill in self.skills:
            skill = normalize_skill(skill)
            if skill and skill not in normalized:
                normalized.append(skill)
        self.skills = normalized

    @classmethod
    def from_path(cls, path: Path) -> "ModelConfig":
        payload = json.loads(path.read_text(encoding="utf-8"))
        config = cls(**payload)
        config.validate()
        return config


def normalize_skill(value: str) -> str:
    result: list[str] = []
    hyphen = False
    for character in value.lower():
        if character.isalnum():
            result.append(character)
            hyphen = False
        elif result and not hyphen:
            result.append("-")
            hyphen = True
    while result and result[-1] == "-":
        result.pop()
    return "".join(result) or "general"


def canonical_training_scope(value: str) -> str:
    """Map UI curriculum labels to concrete internal expert identifiers."""

    normalized = normalize_skill(value)
    if normalized in {"english", "english-foundation", "english-core"} or normalized.startswith(
        "english-foundation-"
    ):
        return "english-core"
    if normalized in {"cpp", "c", "cxx", "cpp23", "cpp23-core"}:
        return "cpp23-core"
    if normalized in {"cpp23-debug", "cpp23-debugging"}:
        return "cpp23-debugging"
    return normalized


def normalize_skill_routes(
    skills: SkillInput,
    batch_size: int,
    available_skills: Sequence[str],
) -> SkillRoutes:
    """Normalize global or per-sample expert routes.

    Inference passes one route shared by the batch. Training passes one route
    per example so an English-only sample cannot accidentally update a C++
    expert merely because another example shares the same padded batch.
    """

    available = set(available_skills)
    if batch_size <= 0:
        return ()
    if not skills:
        return tuple(() for _ in range(batch_size))

    first = skills[0]
    if isinstance(first, str):
        route = tuple(dict.fromkeys(
            normalized
            for normalized in (normalize_skill(str(value)) for value in skills)
            if normalized in available
        ))
        return tuple(route for _ in range(batch_size))

    routes: list[SkillRoute] = []
    for index in range(batch_size):
        source = skills[index] if index < len(skills) else ()
        route = tuple(dict.fromkeys(
            normalized
            for normalized in (normalize_skill(str(value)) for value in source)
            if normalized in available
        ))
        routes.append(route)
    return tuple(routes)


class RMSNorm(nn.Module):
    def __init__(self, dimension: int, epsilon: float = 1.0e-6) -> None:
        super().__init__()
        self.weight = nn.Parameter(torch.ones(dimension))
        self.epsilon = epsilon

    def forward(self, value: Tensor) -> Tensor:
        scale = value.float().pow(2).mean(dim=-1, keepdim=True)
        normalized = value * torch.rsqrt(scale + self.epsilon).to(value.dtype)
        return normalized * self.weight


def _rope_frequencies(head_dimension: int, maximum: int, base: float, device: torch.device) -> Tensor:
    indices = torch.arange(0, head_dimension, 2, dtype=torch.float32, device=device)
    inverse = 1.0 / (base ** (indices / head_dimension))
    positions = torch.arange(maximum, dtype=torch.float32, device=device)
    return torch.outer(positions, inverse)


def _rotate_half(value: Tensor) -> Tensor:
    first = value[..., ::2]
    second = value[..., 1::2]
    return torch.stack((-second, first), dim=-1).flatten(-2)


def _apply_rope(value: Tensor, frequencies: Tensor, offset: int) -> Tensor:
    length = value.shape[-2]
    selected = frequencies[offset : offset + length]
    cosine = torch.repeat_interleave(selected.cos(), 2, dim=-1)[None, None, :, :]
    sine = torch.repeat_interleave(selected.sin(), 2, dim=-1)[None, None, :, :]
    return (value * cosine.to(value.dtype)) + (_rotate_half(value) * sine.to(value.dtype))


class CausalSelfAttention(nn.Module):
    def __init__(self, config: ModelConfig) -> None:
        super().__init__()
        self.n_heads = config.n_heads
        self.head_dimension = config.d_model // config.n_heads
        self.qkv = nn.Linear(config.d_model, config.d_model * 3, bias=False)
        self.output = nn.Linear(config.d_model, config.d_model, bias=False)
        self.dropout = config.dropout
        self.rope_base = config.rope_base
        self._frequency_cache: dict[tuple[str, int], Tensor] = {}

    def _frequencies(self, device: torch.device, maximum: int) -> Tensor:
        key = (str(device), maximum)
        cached = self._frequency_cache.get(key)
        if cached is None or cached.shape[0] < maximum:
            cached = _rope_frequencies(self.head_dimension, maximum, self.rope_base, device)
            self._frequency_cache[key] = cached
        return cached

    def forward(
        self,
        value: Tensor,
        past: tuple[Tensor, Tensor] | None = None,
        use_cache: bool = False,
    ) -> tuple[Tensor, tuple[Tensor, Tensor] | None]:
        batch, length, width = value.shape
        query, key, content = self.qkv(value).chunk(3, dim=-1)
        query = query.view(batch, length, self.n_heads, self.head_dimension).transpose(1, 2)
        key = key.view(batch, length, self.n_heads, self.head_dimension).transpose(1, 2)
        content = content.view(batch, length, self.n_heads, self.head_dimension).transpose(1, 2)
        offset = 0 if past is None else int(past[0].shape[-2])
        frequencies = self._frequencies(value.device, offset + length)
        query = _apply_rope(query, frequencies, offset)
        key = _apply_rope(key, frequencies, offset)
        if past is not None:
            key = torch.cat((past[0], key), dim=-2)
            content = torch.cat((past[1], content), dim=-2)
        causal = past is None and length > 1
        attended = F.scaled_dot_product_attention(
            query,
            key,
            content,
            dropout_p=self.dropout if self.training else 0.0,
            is_causal=causal,
        )
        attended = attended.transpose(1, 2).contiguous().view(batch, length, width)
        return self.output(attended), ((key, content) if use_cache else None)


class SwiGLU(nn.Module):
    def __init__(self, input_width: int, hidden_width: int) -> None:
        super().__init__()
        self.gate = nn.Linear(input_width, hidden_width, bias=False)
        self.up = nn.Linear(input_width, hidden_width, bias=False)
        self.down = nn.Linear(hidden_width, input_width, bias=False)

    def forward(self, value: Tensor) -> Tensor:
        return self.down(F.silu(self.gate(value)) * self.up(value))


class TransformerBlock(nn.Module):
    def __init__(self, config: ModelConfig) -> None:
        super().__init__()
        self.attention_norm = RMSNorm(config.d_model)
        self.attention = CausalSelfAttention(config)
        self.ffn_norm = RMSNorm(config.d_model)
        self.ffn = SwiGLU(config.d_model, config.ffn_hidden)
        self.expert_norm = RMSNorm(config.d_model)
        self.experts = nn.ModuleDict({
            normalize_skill(skill): SwiGLU(config.d_model, config.expert_hidden)
            for skill in config.skills
        })

    def forward(
        self,
        value: Tensor,
        skill_routes: SkillRoutes,
        past: tuple[Tensor, Tensor] | None = None,
        use_cache: bool = False,
    ) -> tuple[Tensor, tuple[Tensor, Tensor] | None]:
        attended, present = self.attention(self.attention_norm(value), past, use_cache)
        value = value + attended
        value = value + self.ffn(self.ffn_norm(value))
        if any(skill_routes):
            normalized = self.expert_norm(value)
            expert_sum = torch.zeros_like(value)
            route_counts = torch.tensor(
                [max(1, len(route)) for route in skill_routes],
                dtype=value.dtype,
                device=value.device,
            ).view(-1, 1, 1)
            for skill, expert in self.experts.items():
                mask_values = [1.0 if skill in route else 0.0 for route in skill_routes]
                if not any(mask_values):
                    continue
                mask = torch.tensor(
                    mask_values,
                    dtype=value.dtype,
                    device=value.device,
                ).view(-1, 1, 1)
                expert_sum = expert_sum + (expert(normalized) * mask)
            value = value + (expert_sum / torch.sqrt(route_counts))
        return value, present


class ThreadSpireModel(nn.Module):
    def __init__(self, config: ModelConfig) -> None:
        super().__init__()
        config.validate()
        self.config = config
        self.embedding = nn.Embedding(config.vocab_size, config.d_model)
        self.blocks = nn.ModuleList([TransformerBlock(config) for _ in range(config.n_layers)])
        self.final_norm = RMSNorm(config.d_model)
        self.reset_parameters()

    def reset_parameters(self) -> None:
        standard_deviation = 0.02
        for module in self.modules():
            if isinstance(module, nn.Linear):
                nn.init.normal_(module.weight, mean=0.0, std=standard_deviation)
            elif isinstance(module, nn.Embedding):
                nn.init.normal_(module.weight, mean=0.0, std=standard_deviation)
        for block in self.blocks:
            nn.init.normal_(
                block.attention.output.weight,
                mean=0.0,
                std=standard_deviation / math.sqrt(2.0 * self.config.n_layers),
            )
            nn.init.normal_(
                block.ffn.down.weight,
                mean=0.0,
                std=standard_deviation / math.sqrt(2.0 * self.config.n_layers),
            )

    def parameter_count(self) -> int:
        return sum(parameter.numel() for parameter in self.parameters())

    def forward(
        self,
        input_ids: Tensor,
        skills: SkillInput = (),
        labels: Tensor | None = None,
        past_key_values: Sequence[tuple[Tensor, Tensor]] | None = None,
        use_cache: bool = False,
    ) -> dict[str, Any]:
        if input_ids.shape[-1] > self.config.context_length:
            input_ids = input_ids[:, -self.config.context_length :]
            if labels is not None:
                labels = labels[:, -self.config.context_length :]
        skill_routes = normalize_skill_routes(
            skills,
            int(input_ids.shape[0]),
            self.config.skills,
        )
        value = self.embedding(input_ids)
        presents: list[tuple[Tensor, Tensor]] = []
        for index, block in enumerate(self.blocks):
            past = None if past_key_values is None else past_key_values[index]
            value, present = block(value, skill_routes, past, use_cache)
            if present is not None:
                presents.append(present)
        logits = F.linear(self.final_norm(value), self.embedding.weight)
        result: dict[str, Any] = {"logits": logits}
        if labels is not None:
            shifted_logits = logits[:, :-1, :].contiguous()
            shifted_labels = labels[:, 1:].contiguous()
            result["loss"] = F.cross_entropy(
                shifted_logits.view(-1, shifted_logits.shape[-1]),
                shifted_labels.view(-1),
                ignore_index=-100,
            )
        if use_cache:
            result["past_key_values"] = presents
        return result


@dataclass(slots=True)
class LoadedInternalModel:
    model: ThreadSpireModel
    config: ModelConfig
    generation: str
    manifest: dict[str, Any]
    device: torch.device


def internal_root(state_root: Path) -> Path:
    return state_root / "models" / "internal"


def active_manifest_path(state_root: Path) -> Path:
    return internal_root(state_root) / "active.json"


def resolve_active_generation(state_root: Path) -> Path:
    pointer_path = active_manifest_path(state_root)
    if not pointer_path.is_file():
        raise FileNotFoundError("internal model has not been initialized")
    pointer = json.loads(pointer_path.read_text(encoding="utf-8"))
    generation = str(pointer.get("generation", "")).strip()
    if not generation or Path(generation).name != generation:
        raise ValueError("invalid internal model active pointer")
    path = internal_root(state_root) / "generations" / generation
    if not path.is_dir():
        raise FileNotFoundError(f"active internal generation is missing: {path}")
    return path


def generation_manifest(path: Path) -> dict[str, Any]:
    manifest_path = path / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"internal model manifest not found: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != FORMAT_VERSION:
        raise ValueError("unsupported internal model format")
    weights = path / str(manifest.get("weights_file", "weights.safetensors"))
    if not weights.is_file():
        raise FileNotFoundError(f"internal model weights not found: {weights}")
    if sha256_file(weights) != str(manifest.get("weights_sha256", "")):
        raise ValueError("internal model weights checksum mismatch")
    return manifest


def load_generation_model(
    state_root: Path,
    generation: str,
    device: torch.device | None = None,
    dtype: torch.dtype | None = None,
) -> LoadedInternalModel:
    if not generation or Path(generation).name != generation:
        raise ValueError("invalid internal model generation")
    path = internal_root(state_root) / "generations" / generation
    manifest = generation_manifest(path)
    if str(manifest.get("generation", "")) != generation:
        raise ValueError("internal model generation manifest does not match its directory")
    config = ModelConfig.from_path(path / str(manifest.get("config_file", "config.json")))
    device = device or torch.device("cuda" if torch.cuda.is_available() else "cpu")
    if dtype is None:
        dtype = torch.bfloat16 if device.type == "cuda" and torch.cuda.is_bf16_supported() else (
            torch.float16 if device.type == "cuda" else torch.float32
        )
    model = ThreadSpireModel(config)
    model.load_state_dict(load_file(str(path / str(manifest["weights_file"])), device="cpu"), strict=True)
    model.to(device=device, dtype=dtype)
    model.eval()
    return LoadedInternalModel(
        model=model,
        config=config,
        generation=generation,
        manifest=manifest,
        device=device,
    )


def resolve_bootstrap_repair_generation(
    state_root: Path,
    active_generation: str,
) -> str | None:
    """Find the strongest valid rejected descendant to continue bootstrap from."""

    generations_root = internal_root(state_root) / "generations"
    if not generations_root.is_dir():
        return None
    manifests: dict[str, dict[str, Any]] = {}
    paths: dict[str, Path] = {}
    for path in generations_root.iterdir():
        if not path.is_dir() or path.name.endswith(".tmp"):
            continue
        try:
            manifest = generation_manifest(path)
        except (FileNotFoundError, ValueError, json.JSONDecodeError):
            continue
        generation = str(manifest.get("generation", ""))
        if generation != path.name:
            continue
        manifests[generation] = manifest
        paths[generation] = path

    def descends_from_active(generation: str) -> bool:
        visited: set[str] = set()
        current = generation
        while current and current not in visited:
            visited.add(current)
            parent = str(manifests.get(current, {}).get("parent") or "")
            if parent == active_generation:
                return True
            current = parent
        return False

    candidates: list[tuple[float, float, int, int, str]] = []
    for generation, manifest in manifests.items():
        if generation == active_generation or not descends_from_active(generation):
            continue
        if manifest.get("status") != "candidate-rejected":
            continue
        if manifest.get("bootstrap_policy") != BOOTSTRAP_POLICY:
            continue
        if canonical_training_scope(str(manifest.get("training_data_scope", ""))) != "english-core":
            continue
        if bool(manifest.get("bootstrap_complete", False)) or bool(manifest.get("generation_ready", False)):
            continue
        try:
            metrics = json.loads((paths[generation] / "metrics.json").read_text(encoding="utf-8"))
        except (FileNotFoundError, ValueError, json.JSONDecodeError):
            continue
        if not bool(metrics.get("loss_gate_passed", False)):
            continue
        readiness = float(metrics.get("generation_readiness_score", 0.0))
        validation_loss = float(manifest.get("validation_loss", float("inf")))
        if not math.isfinite(validation_loss):
            continue
        candidates.append((
            readiness,
            -validation_loss,
            int(manifest.get("steps", 0)),
            int(manifest.get("created_unix_ms", 0)),
            generation,
        ))
    return max(candidates)[-1] if candidates else None


def save_generation(
    state_root: Path,
    model: ThreadSpireModel,
    config: ModelConfig,
    generation: str,
    *,
    status: str,
    parent: str | None,
    steps: int,
    tokens_seen: int,
    validation_loss: float | None,
    training_scope: str,
    promote: bool,
    bootstrap_target_steps: int = 0,
    bootstrap_complete: bool = False,
    training_data_scope: str = "all",
    full_backbone_trained: bool = False,
    generation_ready: bool = False,
    bootstrap_policy: str = BOOTSTRAP_POLICY,
) -> Path:
    root = internal_root(state_root)
    destination = root / "generations" / generation
    temporary = root / "generations" / (generation + ".tmp")
    shutil.rmtree(temporary, ignore_errors=True)
    temporary.mkdir(parents=True, exist_ok=True)
    config_path = temporary / "config.json"
    atomic_write_text(config_path, json.dumps(asdict(config), indent=2, ensure_ascii=False) + "\n")
    weights_path = temporary / "weights.safetensors"
    tensors = {
        name: tensor.detach().to(device="cpu").contiguous()
        for name, tensor in model.state_dict().items()
    }
    save_file(tensors, str(weights_path))
    manifest = {
        "format": FORMAT_VERSION,
        "name": config.name,
        "origin": "random-initialized-local-model",
        "generation": generation,
        "parent": parent,
        "status": status,
        "created_unix_ms": int(time.time() * 1000),
        "parameter_count": model.parameter_count(),
        "weights_file": weights_path.name,
        "weights_sha256": sha256_file(weights_path),
        "config_file": config_path.name,
        "steps": steps,
        "tokens_seen": tokens_seen,
        "validation_loss": validation_loss,
        "training_scope": training_scope,
        "training_data_scope": training_data_scope,
        "bootstrap_target_steps": max(0, int(bootstrap_target_steps)),
        "bootstrap_complete": bool(bootstrap_complete),
        "full_backbone_trained": bool(full_backbone_trained),
        "generation_ready": bool(generation_ready),
        "bootstrap_policy": bootstrap_policy,
        "skills": config.skills,
        "external_base_weights": False,
    }
    atomic_write_text(temporary / "manifest.json", json.dumps(manifest, indent=2) + "\n")
    if destination.exists():
        shutil.rmtree(destination)
    os.replace(temporary, destination)
    if promote:
        pointer = {
            "format": FORMAT_VERSION,
            "generation": generation,
            "promoted_unix_ms": int(time.time() * 1000),
            "origin": "random-initialized-local-model",
        }
        atomic_write_text(active_manifest_path(state_root), json.dumps(pointer, indent=2) + "\n")
    return destination


def initialize_model(
    state_root: Path,
    config: ModelConfig,
    seed: int = 1337,
    force: bool = False,
) -> dict[str, Any]:
    root = internal_root(state_root)
    active = active_manifest_path(state_root)
    if active.is_file() and not force:
        path = resolve_active_generation(state_root)
        return generation_manifest(path)
    if force:
        shutil.rmtree(root, ignore_errors=True)
    torch.manual_seed(seed)
    random.seed(seed)
    model = ThreadSpireModel(config)
    generation = "g000000"
    path = save_generation(
        state_root,
        model,
        config,
        generation,
        status="initialized-untrained",
        parent=None,
        steps=0,
        tokens_seen=0,
        validation_loss=None,
        training_scope="all",
        promote=True,
    )
    return generation_manifest(path)


def load_active_model(
    state_root: Path,
    device: torch.device | None = None,
    dtype: torch.dtype | None = None,
) -> LoadedInternalModel:
    path = resolve_active_generation(state_root)
    return load_generation_model(state_root, path.name, device=device, dtype=dtype)


def lane_skills(lane: str) -> tuple[str, ...]:
    lane = normalize_skill(lane)
    if lane in {"coder", "coding", "cpp23"}:
        return ("english-core", "cpp23-core", "cpp23-debugging")
    if lane in {"thinker", "planning"}:
        return ("english-core", "planning", "cpp23-core")
    if lane in {"reviewer", "verify", "verifier"}:
        return ("english-core", "cpp23-debugging")
    return ("english-core",)


@dataclass(frozen=True, slots=True)
class GenerationQuality:
    accepted: bool
    reason: str


def blocked_no_repeat_tokens(
    generated_tokens: Sequence[int],
    ngram_size: int = 8,
    recent_window: int = 512,
) -> set[int]:
    """Return byte-token continuations that would repeat a recent n-gram.

    This is deliberately byte-token aware. A generic repetition penalty would
    punish ordinary letters because every character is a token in the base ABI.
    Blocking only the exact continuation of an already emitted n-gram prevents
    loops without distorting normal English or C++ character frequencies.
    """

    ngram_size = max(2, int(ngram_size))
    recent_window = max(ngram_size, int(recent_window))
    if len(generated_tokens) < ngram_size - 1:
        return set()

    prefix = tuple(int(token) for token in generated_tokens[-(ngram_size - 1) :])
    first = max(0, len(generated_tokens) - recent_window)
    blocked: set[int] = set()
    for index in range(first, len(generated_tokens) - ngram_size + 1):
        if tuple(int(token) for token in generated_tokens[index : index + ngram_size - 1]) != prefix:
            continue
        candidate = int(generated_tokens[index + ngram_size - 1])
        if BYTE_OFFSET <= candidate < VOCAB_SIZE:
            blocked.add(candidate)
    return blocked


def _looks_like_code(text: str) -> bool:
    markers = (
        "```", "#include", "std::", "constexpr", "consteval", "template<",
        "namespace ", "class ", "struct ", "->", "::", "cmake_minimum_required",
    )
    marker_count = sum(marker in text for marker in markers)
    brace_count = text.count("{") + text.count("}") + text.count(";")
    return marker_count >= 2 or (marker_count >= 1 and brace_count >= 3)


def assess_generation_quality(text: str) -> GenerationQuality:
    """Reject obvious corruption and repetition before a draft reaches the UI.

    This is a conservative structural gate, not a factual verifier. It catches
    the failure modes a tiny under-trained model can identify locally without
    pretending that fluent text is necessarily correct.
    """

    stripped = text.strip()
    if not stripped:
        return GenerationQuality(False, "empty internal-model draft")
    if "\ufffd" in stripped:
        return GenerationQuality(False, "invalid UTF-8 replacement characters")
    if any(ord(character) < 32 and character not in "\r\n\t" for character in stripped):
        return GenerationQuality(False, "control characters in generated text")
    if re.search(r"([A-Za-z0-9])\1{7,}", stripped):
        return GenerationQuality(False, "repeated alphanumeric character run")
    if re.search(r"([^\w\s])\1{15,}", stripped):
        return GenerationQuality(False, "repeated punctuation run")
    if re.search(r"([A-Za-z]{2,5})(?:\1){2,}", stripped, re.IGNORECASE):
        return GenerationQuality(False, "repeated character-fragment loop")

    code_like = _looks_like_code(stripped)
    words = re.findall(r"[A-Za-z][A-Za-z0-9_'-]*", stripped.lower())
    if words:
        run = 1
        maximum_run = 1
        for previous, current in zip(words, words[1:]):
            run = run + 1 if current == previous else 1
            maximum_run = max(maximum_run, run)
        if maximum_run >= (8 if code_like else 4):
            return GenerationQuality(False, "consecutive repeated-word loop")

    if len(words) >= 20:
        counts = Counter(words)
        dominant_ratio = max(counts.values()) / len(words)
        unique_ratio = len(counts) / len(words)
        if dominant_ratio >= (0.40 if code_like else 0.24):
            return GenerationQuality(False, "one word dominates the draft")
        if unique_ratio < (0.16 if code_like else 0.30):
            return GenerationQuality(False, "very low lexical diversity")

    if len(words) >= 24:
        trigrams = Counter(zip(words, words[1:], words[2:]))
        repeated_trigrams = sum(max(0, count - 1) for count in trigrams.values())
        repeated_ratio = repeated_trigrams / max(1, len(words) - 2)
        maximum_trigram = max(trigrams.values(), default=1)
        if maximum_trigram >= (8 if code_like else 4) or repeated_ratio >= (
            0.55 if code_like else 0.28
        ):
            return GenerationQuality(False, "repeated phrase loop")

    visible = [character for character in stripped if not character.isspace()]
    if len(visible) >= 24 and not code_like:
        alphanumeric_ratio = sum(character.isalnum() for character in visible) / len(visible)
        letters = "".join(
            character.lower() for character in visible if character.isalpha()
        )
        if len(letters) >= 24:
            bigrams = Counter(zip(letters, letters[1:]))
            dominant_ratio = max(bigrams.values(), default=0) / max(1, len(letters) - 1)
            if dominant_ratio >= 0.20:
                return GenerationQuality(False, "one character pair dominates the draft")
        if alphanumeric_ratio < 0.18:
            return GenerationQuality(False, "generated text is mostly punctuation")

    return GenerationQuality(True, "structural quality gate passed")


def sample_token(logits: Tensor, temperature: float, top_p: float, top_k: int = 64) -> Tensor:
    if temperature <= 0.0:
        return torch.argmax(logits, dim=-1, keepdim=True)
    logits = logits / max(temperature, 1.0e-5)
    if top_k > 0 and top_k < logits.shape[-1]:
        threshold = torch.topk(logits, top_k, dim=-1).values[..., -1, None]
        logits = logits.masked_fill(logits < threshold, float("-inf"))
    probabilities = torch.softmax(logits, dim=-1)
    sorted_probabilities, sorted_indices = torch.sort(probabilities, descending=True, dim=-1)
    cumulative = torch.cumsum(sorted_probabilities, dim=-1)
    mask = cumulative - sorted_probabilities > top_p
    sorted_probabilities = sorted_probabilities.masked_fill(mask, 0.0)
    sorted_probabilities = sorted_probabilities / sorted_probabilities.sum(dim=-1, keepdim=True)
    selected = torch.multinomial(sorted_probabilities, 1)
    return sorted_indices.gather(-1, selected)


def generate_tokens(
    loaded: LoadedInternalModel,
    messages: Sequence[dict[str, str]],
    lane: str,
    maximum_new_tokens: int,
    temperature: float,
    top_p: float = 0.92,
) -> Iterator[tuple[str, int]]:
    tokenizer = ByteTokenizer()
    maximum_new_tokens = max(
        1,
        min(maximum_new_tokens, max(1, loaded.config.context_length - 1)),
    )
    prompt = tokenizer.prompt_tokens(messages)
    maximum_prompt = max(1, loaded.config.context_length - maximum_new_tokens)
    prompt = prompt[-maximum_prompt:]
    input_ids = torch.tensor([prompt], dtype=torch.long, device=loaded.device)
    skills = lane_skills(lane)
    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    generated_tokens: list[int] = []
    with torch.inference_mode():
        result = loaded.model(input_ids, skills=skills, use_cache=True)
        past = result["past_key_values"]
        logits = result["logits"][:, -1, :]
        for _ in range(maximum_new_tokens):
            # During free-form generation only UTF-8 byte tokens and EOS are
            # legal.  Structural chat tokens are training delimiters, not text.
            filtered_logits = logits.clone()
            filtered_logits[:, :BYTE_OFFSET] = float("-inf")
            filtered_logits[:, EOS_ID] = logits[:, EOS_ID]
            for blocked_token in blocked_no_repeat_tokens(generated_tokens):
                filtered_logits[:, blocked_token] = float("-inf")
            if len(generated_tokens) >= 6 and len(set(generated_tokens[-6:])) == 1:
                filtered_logits[:, generated_tokens[-1]] = float("-inf")
            next_token = sample_token(filtered_logits, temperature, top_p)
            token_id = int(next_token.item())
            if token_id == EOS_ID:
                break
            if BYTE_OFFSET <= token_id < VOCAB_SIZE:
                generated_tokens.append(token_id)
                text = decoder.decode(bytes([token_id - BYTE_OFFSET]), final=False)
                if text:
                    yield text, token_id
            result = loaded.model(next_token, skills=skills, past_key_values=past, use_cache=True)
            past = result["past_key_values"]
            logits = result["logits"][:, -1, :]
    tail = decoder.decode(b"", final=True)
    if tail:
        yield tail, -1


@dataclass(frozen=True, slots=True)
class GenerationReadiness:
    passed: bool
    score: float
    samples: tuple[dict[str, Any], ...]


def evaluate_generation_readiness(
    model: ThreadSpireModel,
    config: ModelConfig,
    generation: str,
    manifest: dict[str, Any],
    device: torch.device,
) -> GenerationReadiness:
    """Run deterministic held-out prompts before declaring a model usable."""

    loaded = LoadedInternalModel(
        model=model,
        config=config,
        generation=generation,
        manifest=manifest,
        device=device,
    )
    canaries = (
        (
            "lowercase",
            [
                {
                    "role": "system",
                    "content": "Return only the requested transformed text.",
                },
                {
                    "role": "user",
                    "content": "Convert this to lowercase: THREAD AND CHECKPOINT",
                },
            ],
            ("thread", "checkpoint"),
        ),
        (
            "grammar",
            [
                {
                    "role": "system",
                    "content": "Correct the English and return only the sentence.",
                },
                {"role": "user", "content": "The application don't respond."},
            ],
            ("application", "respond"),
        ),
        (
            "definition",
            [
                {"role": "system", "content": "Answer in precise, plain English."},
                {
                    "role": "user",
                    "content": "Describe throughput without an analogy.",
                },
            ],
            ("throughput", "time"),
        ),
    )
    samples: list[dict[str, Any]] = []
    passed_count = 0
    for name, messages, required_terms in canaries:
        output = "".join(
            piece
            for piece, _ in generate_tokens(
                loaded, messages, "fast", 64, 0.0, top_p=1.0
            )
        ).strip()
        quality = assess_generation_quality(output)
        semantic_match = all(term in output.lower() for term in required_terms)
        sample_passed = quality.accepted and semantic_match
        passed_count += int(sample_passed)
        samples.append({
            "name": name,
            "passed": sample_passed,
            "structural_quality": quality.accepted,
            "reason": quality.reason,
            "output": output,
        })
    structurally_sound = all(bool(sample["structural_quality"]) for sample in samples)
    return GenerationReadiness(
        passed=structurally_sound and passed_count >= 2,
        score=passed_count / len(canaries),
        samples=tuple(samples),
    )


def _record_messages(record: dict[str, Any]) -> list[dict[str, str]] | None:
    messages = record.get("messages")
    if isinstance(messages, list):
        clean = [
            {"role": str(item.get("role", "user")), "content": str(item.get("content", ""))}
            for item in messages
            if isinstance(item, dict) and str(item.get("content", "")).strip()
        ]
        if clean and any(message["role"].lower() == "assistant" for message in clean):
            return clean
    prompt = str(record.get("prompt", "")).strip()
    response = str(record.get("response", "")).strip()
    if prompt and response:
        return [
            {"role": "user", "content": prompt},
            {"role": "assistant", "content": response},
        ]
    return None


def _record_skills(record_capabilities: set[str]) -> tuple[str, ...]:
    """Map one training record onto the experts that should receive gradients.

    English remains active for every sample because it is the shared interface.
    C++23/debugging/planning experts are only activated when the record actually
    contains evidence for those capabilities.  This avoids teaching every
    specialist from every unrelated English sentence during bootstrap.
    """

    selected = ["english-core"]
    if any(
        capability == "cpp23" or capability.startswith("cpp23-")
        for capability in record_capabilities
    ) or record_capabilities.intersection({
        "cmake", "msvc", "build-systems", "code", "coding",
    }):
        selected.append("cpp23-core")
    if any("debug" in capability or "repair" in capability for capability in record_capabilities) \
            or record_capabilities.intersection({
                "compiler-diagnostic", "compiler-error", "undefined-behavior",
                "sanitizer", "concurrency", "tests",
            }):
        if "cpp23-core" not in selected:
            selected.append("cpp23-core")
        selected.append("cpp23-debugging")
    if record_capabilities.intersection({
        "planning", "architecture", "design", "trajectory", "long-horizon",
        "repository", "tool-use",
    }):
        selected.append("planning")
    return tuple(dict.fromkeys(selected))


def _capability_matches_scope(capabilities: set[str], normalized_scope: str) -> bool:
    if normalized_scope in capabilities or "general" in capabilities:
        return True
    if normalized_scope.startswith("cpp23") and any(
        capability == "cpp23" or capability.startswith("cpp23-")
        for capability in capabilities
    ):
        return True
    if normalized_scope.startswith("english") and any(
        capability == "english" or capability.startswith("english-")
        for capability in capabilities
    ):
        return True
    if normalized_scope == "cpp23-debugging" and (
        any("debug" in capability or "repair" in capability for capability in capabilities)
        or bool(capabilities.intersection({
            "compiler-diagnostic", "compiler-error", "undefined-behavior",
            "sanitizer", "concurrency", "tests",
        }))
    ):
        return True
    if normalized_scope == "cpp23-core" and capabilities.intersection({
        "cmake", "msvc", "build-systems", "code", "coding",
    }):
        return True
    if normalized_scope == "planning" and capabilities.intersection({
        "planning", "architecture", "design", "trajectory", "long-horizon",
        "repository", "tool-use",
    }):
        return True
    return False


def load_training_sequences(
    paths: Iterable[Path | str],
    maximum_tokens: int,
    skill: str | None = None,
) -> list[TrainingSequence]:
    tokenizer = ByteTokenizer()
    sequences: list[TrainingSequence] = []
    seen: set[str] = set()
    normalized_skill = normalize_skill(skill) if skill else None
    for path_value in paths:
        path = Path(path_value)
        if not path.is_file():
            continue
        with path.open("r", encoding="utf-8") as stream:
            for line in stream:
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                # Defense in depth: even if a caller bypasses the worker's
                # filtered JSONL paths, an English-only optimizer scope must
                # never consume explicit or code-shaped C++ evidence.
                if normalized_skill and normalized_skill.startswith("english"):
                    if not _threadspire_curriculum_guard.english_record(record):
                        continue
                record_skill = normalize_skill(str(record.get("skill", record.get("domain", "general"))))
                record_tags = {
                    normalize_skill(str(value))
                    for value in record.get("tags", [])
                    if str(value).strip()
                }
                record_capabilities = {record_skill, *record_tags}
                if normalized_skill:
                    if not _capability_matches_scope(record_capabilities, normalized_skill):
                        continue
                if "eligible_for_training" in record:
                    if not bool(record.get("eligible_for_training", False)):
                        continue
                    if not (
                        bool(record.get("human_accepted", False))
                        or bool(record.get("deterministic_verified", False))
                        or bool(record.get("reference_verified", False))
                    ):
                        continue
                elif "accepted" in record and not bool(record.get("accepted", False)):
                    continue
                messages = _record_messages(record)
                if not messages:
                    continue
                fingerprint = hashlib.sha256(
                    json.dumps(messages, ensure_ascii=False, sort_keys=True).encode("utf-8")
                ).hexdigest()
                if fingerprint in seen:
                    continue
                seen.add(fingerprint)
                tokens, labels = tokenizer.supervised_tokens(messages, maximum_tokens)
                route = _record_skills(record_capabilities)
                if normalized_skill and normalized_skill in {
                    "english-core", "cpp23-core", "cpp23-debugging", "planning"
                }:
                    route = tuple(dict.fromkeys(("english-core", normalized_skill)))
                sequences.append((tokens, labels, route))
    return sequences


def collate_batch(
    batch: Sequence[TrainingSequence],
    device: torch.device,
) -> tuple[Tensor, Tensor, SkillRoutes]:
    maximum = max(len(tokens) for tokens, _, _ in batch)
    input_ids = torch.full((len(batch), maximum), PAD_ID, dtype=torch.long)
    labels = torch.full((len(batch), maximum), -100, dtype=torch.long)
    routes: list[SkillRoute] = []
    for index, (tokens, targets, skills) in enumerate(batch):
        input_ids[index, : len(tokens)] = torch.tensor(tokens, dtype=torch.long)
        labels[index, : len(targets)] = torch.tensor(targets, dtype=torch.long)
        routes.append(skills)
    return input_ids.to(device), labels.to(device), tuple(routes)


def evaluate_loss(
    model: ThreadSpireModel,
    sequences: Sequence[TrainingSequence],
    device: torch.device,
    limit: int = 32,
) -> float:
    if not sequences:
        return float("inf")
    model.eval()
    losses: list[float] = []
    with torch.inference_mode():
        for item in sequences[:limit]:
            input_ids, labels, skills = collate_batch([item], device)
            result = model(input_ids, skills=skills, labels=labels)
            loss = float(result["loss"].detach().float().cpu())
            if math.isfinite(loss):
                losses.append(loss)
    return sum(losses) / len(losses) if losses else float("inf")


def configure_training_scope(model: ThreadSpireModel, scope: str) -> list[nn.Parameter]:
    scope = canonical_training_scope(scope)
    for parameter in model.parameters():
        parameter.requires_grad = False
    if scope in {"all", "core", "bootstrap", "general"}:
        for parameter in model.parameters():
            parameter.requires_grad = True
    else:
        matched = False
        for block in model.blocks:
            if scope in block.experts:
                for parameter in block.experts[scope].parameters():
                    parameter.requires_grad = True
                matched = True
        if not matched:
            raise ValueError(f"unknown internal skill module: {scope}")
        for parameter in model.final_norm.parameters():
            parameter.requires_grad = True
    return [parameter for parameter in model.parameters() if parameter.requires_grad]


def _next_generation(state_root: Path, parent: str) -> str:
    try:
        number = int(parent.removeprefix("g")) + 1
    except ValueError:
        number = int(time.time())
    generations_root = internal_root(state_root) / "generations"
    if generations_root.is_dir():
        for path in generations_root.iterdir():
            name = path.name.removesuffix(".tmp")
            if not name.startswith("g"):
                continue
            try:
                number = max(number, int(name.removeprefix("g")) + 1)
            except ValueError:
                continue
    return f"g{number:06d}"


def save_training_checkpoint(
    path: Path,
    model: ThreadSpireModel,
    optimizer: torch.optim.Optimizer,
    *,
    parent: str,
    scope: str,
    steps: int,
    tokens_seen: int,
    order: list[int],
    cursor: int,
    accumulation_steps: int,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    gradients = {
        name: parameter.grad.detach().to(device="cpu").contiguous()
        for name, parameter in model.named_parameters()
        if parameter.grad is not None
    }
    torch.save(
        {
            "format": FORMAT_VERSION,
            "parent": parent,
            "scope": scope,
            "model": model.state_dict(),
            "optimizer": optimizer.state_dict(),
            "steps": steps,
            "tokens_seen": tokens_seen,
            "order": order,
            "cursor": cursor,
            "accumulation_steps": accumulation_steps,
            "gradients": gradients,
            "python_rng": random.getstate(),
            "torch_rng": torch.get_rng_state(),
            "cuda_rng": torch.cuda.get_rng_state_all() if torch.cuda.is_available() else None,
        },
        temporary,
    )
    os.replace(temporary, path)


def train_candidate(
    state_root: Path,
    data_paths: Sequence[Path],
    anchor_paths: Sequence[Path],
    scope: str,
    maximum_steps: int,
    learning_rate: float,
    batch_size: int,
    accumulation_steps: int,
    checkpoint_steps: int,
    control_dir: Path | None,
    require_cuda: bool,
    seed: int = 1337,
    defer_promotion: bool = False,
    progress_file: Path | None = None,
    bootstrap_target_steps: int = 0,
    honor_inference_priority: bool = True,
) -> dict[str, Any]:
    write_progress(
        progress_file,
        phase="starting",
        message="Preparing the internal model training run",
        maximum_steps=max(1, int(maximum_steps)),
        bootstrap_target_steps=max(0, int(bootstrap_target_steps)),
    )
    if require_cuda and not torch.cuda.is_available():
        raise RuntimeError("CUDA is required for internal model training")
    random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    requested_scope = normalize_skill(scope)
    training_data_scope = canonical_training_scope(scope)
    write_progress(
        progress_file,
        phase="loading-model",
        message=f"Loading the strongest active or repair generation on {device.type.upper()}",
        device=device.type,
        maximum_steps=max(1, int(maximum_steps)),
        bootstrap_target_steps=max(0, int(bootstrap_target_steps)),
    )
    loaded = load_active_model(state_root, device=device, dtype=torch.float32)
    active_generation = loaded.generation
    if (
        training_data_scope == "english-core"
        and max(0, int(bootstrap_target_steps)) > 0
        and not bool(loaded.manifest.get("bootstrap_complete", False))
    ):
        repair_generation = resolve_bootstrap_repair_generation(
            state_root,
            active_generation,
        )
        if repair_generation is not None:
            loaded = load_generation_model(
                state_root,
                repair_generation,
                device=device,
                dtype=torch.float32,
            )
            print(json.dumps({
                "event": "bootstrap-repair-resume",
                "active_generation": active_generation,
                "repair_generation": repair_generation,
                "prior_total_steps": int(loaded.manifest.get("steps", 0)),
            }), flush=True)
    parent = loaded.generation
    model = loaded.model
    config = loaded.config
    prior_total_steps = max(0, int(loaded.manifest.get("steps", 0)))
    prior_bootstrap_target = max(0, int(loaded.manifest.get("bootstrap_target_steps", 0)))
    effective_bootstrap_target = max(prior_bootstrap_target, max(0, int(bootstrap_target_steps)))
    prior_full_backbone = bool(loaded.manifest.get("full_backbone_trained", False))
    prior_generation_ready = bool(loaded.manifest.get("generation_ready", False))
    prior_bootstrap_complete = bool(
        loaded.manifest.get("bootstrap_complete", False)
        and prior_full_backbone
        and prior_generation_ready
    )
    bootstrap_backbone_required = bool(
        training_data_scope == "english-core"
        and effective_bootstrap_target > 0
        and not prior_bootstrap_complete
    )
    optimizer_scope = "all" if bootstrap_backbone_required else training_data_scope
    scope_filter = (
        None
        if training_data_scope in {"all", "core", "bootstrap", "general"}
        else training_data_scope
    )
    sequences = load_training_sequences(
        data_paths,
        config.context_length,
        scope_filter,
    )
    anchors = load_training_sequences(
        anchor_paths,
        config.context_length,
        scope_filter,
    )
    if not sequences:
        raise RuntimeError("no verified internal-model training examples were available")
    parameters = configure_training_scope(model, optimizer_scope)
    optimizer = torch.optim.AdamW(parameters, lr=learning_rate, betas=(0.9, 0.95), weight_decay=0.1)
    use_amp = device.type == "cuda"
    amp_dtype = torch.bfloat16 if use_amp and torch.cuda.is_bf16_supported() else torch.float16
    write_progress(
        progress_file,
        phase="evaluating-baseline",
        message="Measuring the active generation before training",
        generation=parent,
        device=device.type,
        maximum_steps=max(1, int(maximum_steps)),
        prior_total_steps=prior_total_steps,
        bootstrap_target_steps=effective_bootstrap_target,
        training_examples=len(sequences),
        anchor_examples=len(anchors),
        training_data_scope=training_data_scope,
        optimizer_scope=optimizer_scope,
    )
    previous_loss = evaluate_loss(model, anchors or sequences[:32], device)
    model.train()
    run_started = time.monotonic()
    last_progress_write = 0.0
    steps = 0
    tokens_seen = int(loaded.manifest.get("tokens_seen", 0))
    order = list(range(len(sequences)))
    random.shuffle(order)
    cursor = 0
    checkpoint_path = internal_root(state_root) / "checkpoints" / f"{parent}-{optimizer_scope}" / "latest.pt"
    restored_gradients = False
    if checkpoint_path.is_file():
        checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
        if (
            checkpoint.get("format") == FORMAT_VERSION
            and checkpoint.get("parent") == parent
            and checkpoint.get("scope") == optimizer_scope
            and int(checkpoint.get("accumulation_steps", accumulation_steps)) == accumulation_steps
        ):
            model.load_state_dict(checkpoint["model"], strict=True)
            optimizer.load_state_dict(checkpoint["optimizer"])
            steps = int(checkpoint.get("steps", 0))
            tokens_seen = int(checkpoint.get("tokens_seen", tokens_seen))
            saved_order = checkpoint.get("order")
            if isinstance(saved_order, list) and len(saved_order) == len(sequences):
                order = [int(value) for value in saved_order]
            cursor = int(checkpoint.get("cursor", 0)) % max(1, len(order))
            python_rng = checkpoint.get("python_rng")
            if python_rng is not None:
                random.setstate(python_rng)
            torch_rng = checkpoint.get("torch_rng")
            if torch_rng is not None:
                torch.set_rng_state(torch_rng)
            cuda_rng = checkpoint.get("cuda_rng")
            if cuda_rng is not None and torch.cuda.is_available():
                torch.cuda.set_rng_state_all(cuda_rng)
            saved_gradients = checkpoint.get("gradients")
            if isinstance(saved_gradients, dict):
                parameters_by_name = dict(model.named_parameters())
                for name, gradient in saved_gradients.items():
                    parameter = parameters_by_name.get(str(name))
                    if parameter is None or not isinstance(gradient, Tensor):
                        continue
                    parameter.grad = gradient.to(
                        device=parameter.device,
                        dtype=parameter.dtype,
                    ).clone()
                    restored_gradients = True
            print(json.dumps({"event": "resumed", "steps": steps, "tokens_seen": tokens_seen, "scope": optimizer_scope}), flush=True)
    if not restored_gradients:
        optimizer.zero_grad(set_to_none=True)
    resumed_step = steps
    write_progress(
        progress_file,
        phase="training",
        message=(
            f"Resuming bootstrap training at step {steps:,} of {maximum_steps:,}"
            if steps > 0
            else f"Bootstrap training step 0 of {maximum_steps:,}"
        ),
        generation=parent,
        requested_scope=requested_scope,
        scope=optimizer_scope,
        training_data_scope=training_data_scope,
        step=steps,
        maximum_steps=maximum_steps,
        prior_total_steps=prior_total_steps,
        projected_total_steps=prior_total_steps + maximum_steps,
        bootstrap_target_steps=effective_bootstrap_target,
        tokens_seen=tokens_seen,
        percent=(100.0 * steps / max(1, maximum_steps)),
        elapsed_seconds=0.0,
        eta_seconds=None,
        resumed=steps > 0,
        device=device.type,
    )
    active_marker = None if control_dir is None else control_dir / "training.active"
    if active_marker is not None:
        atomic_write_text(active_marker, f"pid={os.getpid()}\n")
    try:
        while steps < maximum_steps:
            if control_dir is not None and honor_inference_priority and (
                (control_dir / "inference.priority").exists()
                or any(control_dir.glob("inference.*"))
            ):
                save_training_checkpoint(
                    checkpoint_path,
                    model,
                    optimizer,
                    parent=parent,
                    scope=optimizer_scope,
                    steps=steps,
                    tokens_seen=tokens_seen,
                    order=order,
                    cursor=cursor,
                    accumulation_steps=accumulation_steps,
                )
                write_progress(
                    progress_file,
                    phase="preempted",
                    message="Training checkpointed and yielded to foreground inference",
                    generation=parent,
                    requested_scope=requested_scope,
                    scope=optimizer_scope,
                    training_data_scope=training_data_scope,
                    step=steps,
                    maximum_steps=maximum_steps,
                    prior_total_steps=prior_total_steps,
                    bootstrap_target_steps=effective_bootstrap_target,
                    tokens_seen=tokens_seen,
                    percent=(100.0 * steps / max(1, maximum_steps)),
                    resumable=True,
                )
                return {
                    "status": "preempted",
                    "exit_code": 75,
                    "parent_generation": parent,
                    "steps": steps,
                    "tokens_seen": tokens_seen,
                }
            batch_items: list[tuple[list[int], list[int]]] = []
            for _ in range(max(1, batch_size)):
                if cursor >= len(order):
                    random.shuffle(order)
                    cursor = 0
                batch_items.append(sequences[order[cursor]])
                cursor += 1
            input_ids, labels, active_skills = collate_batch(batch_items, device)
            with torch.autocast(device_type=device.type, dtype=amp_dtype, enabled=use_amp):
                result = model(input_ids, skills=active_skills, labels=labels)
                loss = result["loss"] / max(1, accumulation_steps)
            loss.backward()
            tokens_seen += int((labels != -100).sum().item())
            if (steps + 1) % max(1, accumulation_steps) == 0:
                torch.nn.utils.clip_grad_norm_(parameters, 1.0)
                optimizer.step()
                optimizer.zero_grad(set_to_none=True)
            steps += 1
            loss_value = float(result["loss"].detach().float().cpu())
            now = time.monotonic()
            if steps == 1 or steps % 10 == 0:
                print(json.dumps({
                    "event": "training-progress",
                    "scope": optimizer_scope,
                    "training_data_scope": training_data_scope,
                    "step": steps,
                    "maximum_steps": maximum_steps,
                    "loss": loss_value,
                    "tokens_seen": tokens_seen,
                }), flush=True)
            if steps == maximum_steps or steps == 1 or now - last_progress_write >= 1.0:
                elapsed = max(0.0, now - run_started)
                completed_this_process = max(0, steps - resumed_step)
                seconds_per_step = elapsed / completed_this_process if completed_this_process > 0 else 0.0
                eta = seconds_per_step * max(0, maximum_steps - steps) if seconds_per_step > 0.0 else None
                write_progress(
                    progress_file,
                    phase="training",
                    message=f"Bootstrap training step {steps:,} of {maximum_steps:,}",
                    generation=parent,
                    requested_scope=requested_scope,
                    scope=optimizer_scope,
                    training_data_scope=training_data_scope,
                    step=steps,
                    maximum_steps=maximum_steps,
                    prior_total_steps=prior_total_steps,
                    projected_total_steps=prior_total_steps + maximum_steps,
                    bootstrap_target_steps=effective_bootstrap_target,
                    tokens_seen=tokens_seen,
                    loss=loss_value,
                    percent=(100.0 * steps / max(1, maximum_steps)),
                    elapsed_seconds=elapsed,
                    eta_seconds=eta,
                    resumed=resumed_step > 0,
                    device=device.type,
                )
                last_progress_write = now
            if checkpoint_steps > 0 and steps % checkpoint_steps == 0:
                save_training_checkpoint(
                    checkpoint_path,
                    model,
                    optimizer,
                    parent=parent,
                    scope=optimizer_scope,
                    steps=steps,
                    tokens_seen=tokens_seen,
                    order=order,
                    cursor=cursor,
                    accumulation_steps=accumulation_steps,
                )
    finally:
        if active_marker is not None:
            active_marker.unlink(missing_ok=True)
    remainder = steps % max(1, accumulation_steps)
    if remainder != 0:
        correction = float(max(1, accumulation_steps)) / float(remainder)
        for parameter in parameters:
            if parameter.grad is not None:
                parameter.grad.mul_(correction)
        torch.nn.utils.clip_grad_norm_(parameters, 1.0)
        optimizer.step()
        optimizer.zero_grad(set_to_none=True)
    write_progress(
        progress_file,
        phase="evaluating-candidate",
        message="Training finished; evaluating the shadow generation",
        generation=parent,
        requested_scope=requested_scope,
        scope=optimizer_scope,
        training_data_scope=training_data_scope,
        step=steps,
        maximum_steps=maximum_steps,
        prior_total_steps=prior_total_steps,
        projected_total_steps=prior_total_steps + steps,
        bootstrap_target_steps=effective_bootstrap_target,
        tokens_seen=tokens_seen,
        percent=100.0,
        elapsed_seconds=max(0.0, time.monotonic() - run_started),
        eta_seconds=None,
        device=device.type,
    )
    validation_loss = evaluate_loss(model, anchors or sequences[:32], device)
    requires_generation_readiness = (
        effective_bootstrap_target > 0 or prior_generation_ready
    )
    candidate_readiness = (
        evaluate_generation_readiness(
            model, config, parent, loaded.manifest, device
        )
        if requires_generation_readiness else
        GenerationReadiness(False, 0.0, ())
    )
    generation_ready = bool(candidate_readiness.passed)
    improvement = previous_loss - validation_loss if math.isfinite(previous_loss) else 1.0
    loss_passed = math.isfinite(validation_loss) and (
        not math.isfinite(previous_loss)
        or validation_loss <= previous_loss + 1.0e-6
    )
    passed_evaluation = loss_passed and (
        generation_ready if requires_generation_readiness else True
    )
    generation = _next_generation(state_root, parent)
    total_steps = prior_total_steps + steps
    full_backbone_trained = prior_full_backbone or bool(
        optimizer_scope == "all"
        and training_data_scope == "english-core"
        and effective_bootstrap_target > 0
        and total_steps >= effective_bootstrap_target
    )
    bootstrap_complete = prior_bootstrap_complete or bool(
        effective_bootstrap_target > 0
        and total_steps >= effective_bootstrap_target
        and full_backbone_trained
        and generation_ready
        and passed_evaluation
    )
    write_progress(
        progress_file,
        phase="saving-generation",
        message="Evaluation passed; publishing the new internal-model generation"
        if passed_evaluation else
        (
            "Generation canaries did not pass; saving the repair candidate for continuation"
            if loss_passed else
            "Held-out loss regressed; saving the candidate without promotion"
        ),
        generation=generation,
        parent_generation=parent,
        requested_scope=requested_scope,
        scope=optimizer_scope,
        training_data_scope=training_data_scope,
        step=steps,
        maximum_steps=maximum_steps,
        total_steps=total_steps,
        bootstrap_target_steps=effective_bootstrap_target,
        bootstrap_complete=bootstrap_complete,
        full_backbone_trained=full_backbone_trained,
        generation_ready=generation_ready,
        generation_readiness_score=candidate_readiness.score,
        tokens_seen=tokens_seen,
        previous_loss=previous_loss,
        validation_loss=validation_loss,
        elapsed_seconds=max(0.0, time.monotonic() - run_started),
        percent=100.0,
        eta_seconds=None,
    )
    model.to(device="cpu", dtype=torch.float32)
    promoted = passed_evaluation and not defer_promotion
    candidate_status = (
        "trained" if promoted else
        ("trained-candidate" if passed_evaluation else "candidate-rejected")
    )
    path = save_generation(
        state_root,
        model,
        config,
        generation,
        status=candidate_status,
        parent=parent,
        steps=total_steps,
        tokens_seen=tokens_seen,
        validation_loss=validation_loss,
        training_scope=optimizer_scope,
        training_data_scope=training_data_scope,
        promote=promoted,
        bootstrap_target_steps=effective_bootstrap_target,
        bootstrap_complete=bootstrap_complete,
        full_backbone_trained=full_backbone_trained,
        generation_ready=generation_ready,
        bootstrap_policy=BOOTSTRAP_POLICY,
    )
    checkpoint_path.unlink(missing_ok=True)
    metrics = {
        "status": (
            "promoted" if promoted else
            ("candidate-ready" if passed_evaluation else "rejected")
        ),
        "generation": generation,
        "parent_generation": parent,
        "active_generation_at_start": active_generation,
        "resumed_from_repair_generation": parent != active_generation,
        "candidate_path": str(path),
        "requested_training_target": requested_scope,
        "training_scope": optimizer_scope,
        "training_data_scope": training_data_scope,
        "steps": steps,
        "tokens_seen": tokens_seen,
        "previous_loss": previous_loss,
        "validation_loss": validation_loss,
        "loss_improvement": improvement,
        "validation_score": 1.0 / (1.0 + max(validation_loss, 0.0)) if math.isfinite(validation_loss) else 0.0,
        "regression_score": max(0.0, validation_loss - previous_loss) if math.isfinite(previous_loss) else 0.0,
        "loss_gate_passed": loss_passed,
        "passed_evaluation": 1.0 if passed_evaluation else 0.0,
        "passed_evaluation_bool": passed_evaluation,
        "promoted": 1.0 if promoted else 0.0,
        "promoted_bool": promoted,
        "promotion_deferred": defer_promotion,
        "total_steps": total_steps,
        "bootstrap_target_steps": effective_bootstrap_target,
        "bootstrap_complete": bootstrap_complete,
        "full_backbone_trained": full_backbone_trained,
        "generation_ready": generation_ready,
        "generation_readiness_score": candidate_readiness.score,
        "generation_readiness_samples": list(candidate_readiness.samples),
        "bootstrap_policy": BOOTSTRAP_POLICY,
    }
    english_established = False
    if training_data_scope == "english-core":
        english_established = update_english_foundation_state(
            state_root,
            generation=generation,
            evaluation_passed=passed_evaluation,
            promoted=promoted,
            bootstrap_target_steps=effective_bootstrap_target,
            bootstrap_complete=bootstrap_complete,
            full_backbone_trained=full_backbone_trained,
            generation_ready=generation_ready,
            total_steps=total_steps,
            validation_loss=validation_loss,
        )
    metrics["english_foundation_established"] = english_established
    atomic_write_text(path / "metrics.json", json.dumps(metrics, indent=2) + "\n")
    needs_bootstrap_repair = bool(effective_bootstrap_target > 0 and not promoted)
    write_progress(
        progress_file,
        phase=(
            "complete" if promoted else
            ("candidate-ready" if passed_evaluation else
             ("repair-required" if needs_bootstrap_repair else "rejected"))
        ),
        message=(
            "Bootstrap training completed and the new generation is active"
            if promoted and bootstrap_complete else
            ("Training completed and the new generation is active" if promoted else
             ("Training completed; promotion is deferred" if passed_evaluation else
              ("Bootstrap canaries did not pass; the saved candidate will be continued by repair"
               if loss_passed and needs_bootstrap_repair else
               "Training completed, but the candidate failed held-out loss checks")))
        ),
        generation=generation,
        parent_generation=parent,
        requested_scope=requested_scope,
        scope=optimizer_scope,
        training_data_scope=training_data_scope,
        step=steps,
        maximum_steps=maximum_steps,
        total_steps=total_steps,
        bootstrap_target_steps=effective_bootstrap_target,
        bootstrap_complete=bootstrap_complete,
        full_backbone_trained=full_backbone_trained,
        generation_ready=generation_ready,
        generation_readiness_score=candidate_readiness.score,
        tokens_seen=tokens_seen,
        previous_loss=previous_loss,
        validation_loss=validation_loss,
        elapsed_seconds=max(0.0, time.monotonic() - run_started),
        percent=100.0,
        eta_seconds=0.0,
        promoted=promoted,
        passed_evaluation=passed_evaluation,
        resumable=needs_bootstrap_repair and loss_passed,
    )
    return metrics


def promote_generation(
    state_root: Path,
    generation: str,
    expected_parent: str,
) -> dict[str, Any]:
    """Atomically promote an evaluated candidate with parent compare-and-swap.

    The weights are already immutable in their generation directory.  Promotion
    changes only the candidate manifest status and the small active pointer.
    A candidate trained from a stale parent can therefore never overwrite a
    newer active generation.
    """

    if Path(generation).name != generation or Path(expected_parent).name != expected_parent:
        raise ValueError("invalid internal generation identifier")
    root = internal_root(state_root)
    candidate_path = root / "generations" / generation
    manifest = generation_manifest(candidate_path)
    if str(manifest.get("parent", "")) != expected_parent:
        raise RuntimeError("candidate parent does not match the requested expected parent")
    if str(manifest.get("status", "")) not in {"trained-candidate", "trained"}:
        raise RuntimeError("internal candidate has not passed evaluation")

    current_path = resolve_active_generation(state_root)
    current_manifest = generation_manifest(current_path)
    current_generation = str(current_manifest.get("generation", ""))
    if current_generation != expected_parent:
        raise RuntimeError(
            f"stale internal candidate: active={current_generation}, expected={expected_parent}"
        )

    manifest["status"] = "trained"
    manifest["promoted_unix_ms"] = int(time.time() * 1000)
    atomic_write_text(
        candidate_path / "manifest.json",
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
    )
    pointer = {
        "format": FORMAT_VERSION,
        "generation": generation,
        "parent": expected_parent,
        "promoted_unix_ms": int(time.time() * 1000),
        "origin": "random-initialized-local-model",
    }
    atomic_write_text(active_manifest_path(state_root), json.dumps(pointer, indent=2) + "\n")
    curriculum_scope = canonical_training_scope(str(
        manifest.get("training_data_scope", manifest.get("training_scope", ""))
    ))
    if curriculum_scope == "english-core":
        try:
            promoted_validation_loss = float(manifest.get("validation_loss", float("inf")))
        except (TypeError, ValueError):
            promoted_validation_loss = float("inf")
        update_english_foundation_state(
            state_root,
            generation=generation,
            evaluation_passed=True,
            promoted=True,
            bootstrap_target_steps=int(manifest.get("bootstrap_target_steps", 0)),
            bootstrap_complete=bool(manifest.get("bootstrap_complete", False)),
            full_backbone_trained=bool(manifest.get("full_backbone_trained", False)),
            generation_ready=bool(manifest.get("generation_ready", False)),
            total_steps=int(manifest.get("steps", 0)),
            validation_loss=promoted_validation_loss,
        )
    return {
        "status": "promoted",
        "generation": generation,
        "parent_generation": expected_parent,
        "external_base_weights": False,
    }


def activate_generation(
    state_root: Path,
    generation: str,
    expected_current: str,
) -> dict[str, Any]:
    """Atomically move the active pointer to a previously trained generation."""

    if Path(generation).name != generation or Path(expected_current).name != expected_current:
        raise ValueError("invalid internal generation identifier")
    target_path = internal_root(state_root) / "generations" / generation
    target = generation_manifest(target_path)
    if str(target.get("status", "")) != "trained":
        raise RuntimeError("rollback target is not a trained internal generation")
    current_path = resolve_active_generation(state_root)
    current = generation_manifest(current_path)
    current_generation = str(current.get("generation", ""))
    if current_generation != expected_current:
        raise RuntimeError(
            f"stale internal rollback: active={current_generation}, expected={expected_current}"
        )
    pointer = {
        "format": FORMAT_VERSION,
        "generation": generation,
        "rollback_from": expected_current,
        "promoted_unix_ms": int(time.time() * 1000),
        "origin": "random-initialized-local-model",
    }
    atomic_write_text(active_manifest_path(state_root), json.dumps(pointer, indent=2) + "\n")
    return {
        "status": "activated",
        "generation": generation,
        "previous_generation": expected_current,
        "external_base_weights": False,
    }


def status_payload(state_root: Path) -> dict[str, Any]:
    try:
        path = resolve_active_generation(state_root)
        manifest = generation_manifest(path)
        ready = bool(
            manifest.get("status") == "trained"
            and manifest.get("bootstrap_complete", False)
            and manifest.get("full_backbone_trained", False)
            and manifest.get("generation_ready", False)
        )
        return {
            "initialized": True,
            "ready": ready,
            "path": str(path),
            **manifest,
        }
    except Exception as exception:
        return {
            "initialized": False,
            "ready": False,
            "error": str(exception),
            "origin": "random-initialized-local-model",
            "external_base_weights": False,
        }
