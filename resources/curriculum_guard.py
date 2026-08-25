"""Hard curriculum boundary for ThreadSpire internal weight training.

The native UI may request any curriculum label, but the internal model must
train its English expert first.  This module applies that policy at the Python
worker boundary without changing argparse's global behavior or erasing Path
objects returned by argparse.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import sys
from typing import Any

ENGLISH_TARGET = "english-foundation"
ENGLISH_INTERNAL_SCOPE = "english-core"
BOOTSTRAP_POLICY = "full-backbone-english-v4"
_INTERNAL_TRAIN_COMMAND = "train-internal"

_CPP_LABELS = {
    "c",
    "code",
    "coder",
    "coding",
    "cmake",
    "compiler",
    "cpp",
    "cpp23",
    "cpp23-core",
    "cpp23-debugging",
    "cxx",
    "msvc",
}
_CPP_MARKERS = (
    "#include",
    "std::",
    "constexpr",
    "consteval",
    "template<",
    "namespace ",
    "cmakelists",
    "nullptr",
    "unique_ptr",
    "shared_ptr",
    "```cpp",
    "```c++",
    "cpp23",
    "c++23",
    "co_await",
)

_installed = False


def root() -> Path:
    explicit = os.environ.get("THREADSPIRE_DATA_ROOT")
    if explicit:
        return Path(explicit).expanduser().resolve()
    local = os.environ.get("LOCALAPPDATA")
    if local:
        return (Path(local) / "ThreadSpire").resolve()
    return (Path.home() / ".local" / "share" / "ThreadSpire").resolve()


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError, TypeError):
        return None


def walk(value: Any, prefix: str = ""):
    if isinstance(value, dict):
        for key, child in value.items():
            child_key = f"{prefix}.{key}" if prefix else str(key)
            yield child_key.lower().replace("-", "_"), child
            yield from walk(child, child_key)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk(child, f"{prefix}[{index}]")


def established(data_root: Path | None = None) -> bool:
    data_root = data_root or root()
    for name in (
        "curriculum-state.json",
        "curriculum_state.json",
        "english-foundation-state.json",
    ):
        document = read_json(data_root / "state" / name)
        if document is None:
            continue
        foundation = document.get("english_foundation") if isinstance(document, dict) else None
        if not isinstance(foundation, dict) or foundation.get("bootstrap_policy") != BOOTSTRAP_POLICY:
            continue
        complete = False
        evaluated = False
        for key, value in walk(document):
            if "english" not in key and "foundation" not in key:
                continue
            if any(
                marker in key
                for marker in ("established", "foundation_complete", "foundation_ready")
            ) and value is True:
                complete = True
            if any(
                marker in key
                for marker in ("evaluation_passed", "eval_passed", "verified_ready")
            ) and value is True:
                evaluated = True
        if complete and evaluated:
            return True
    return False


def _normalize_label(value: Any) -> str:
    text = str(value).strip().lower()
    normalized = re.sub(r"[^a-z0-9+]+", "-", text).strip("-")
    return normalized


def internal_scope_for(target: Any) -> str:
    normalized = _normalize_label(target)
    if normalized in {"english", "english-foundation", "english-core"} or normalized.startswith(
        "english-foundation-"
    ):
        return ENGLISH_INTERNAL_SCOPE
    if normalized in {"cpp", "c", "cxx", "cpp23", "cpp23-core"}:
        return "cpp23-core"
    if normalized in {"cpp23-debug", "cpp23-debugging"}:
        return "cpp23-debugging"
    return normalized or "general"


def code_shaped(value: Any) -> bool:
    try:
        text = json.dumps(value, ensure_ascii=False).lower()
    except Exception:
        text = str(value).lower()
    return any(marker in text for marker in _CPP_MARKERS) or (
        text.count(";") >= 3 and text.count("{") + text.count("}") >= 2
    )


def _declared_labels(record: dict[str, Any]) -> set[str]:
    labels: set[str] = set()
    for field in ("domain", "skill", "skill_id", "target", "curriculum"):
        value = record.get(field)
        if value is not None and str(value).strip():
            labels.add(_normalize_label(value))
    tags = record.get("tags", [])
    if isinstance(tags, (list, tuple, set)):
        labels.update(
            _normalize_label(value)
            for value in tags
            if str(value).strip()
        )
    return labels


def english_record(record: Any) -> bool:
    if not isinstance(record, dict):
        return False
    labels = _declared_labels(record)
    if any(
        label in _CPP_LABELS
        or label.startswith("cpp")
        or label.startswith("c++")
        or label.startswith("cmake")
        or label.startswith("msvc")
        for label in labels
    ):
        return False
    explicitly_english = any(
        label == "english" or label.startswith("english-")
        for label in labels
    )
    if code_shaped(record):
        return False
    return explicitly_english or not labels.intersection(_CPP_LABELS)


def _same_path_type(original: Any, path: Path) -> Path | str:
    """Return a replacement without discarding argparse's Path type."""

    return path if isinstance(original, Path) else str(path)


def _candidate_english_datasets(data_root: Path) -> tuple[Path, ...]:
    module_directory = Path(__file__).resolve().parent
    return (
        data_root / "data" / "bootstrap.jsonl",
        data_root / "data" / "seed.jsonl",
        module_directory / "bootstrap.jsonl",
        module_directory / "seed.jsonl",
        module_directory.parent / "data" / "bootstrap.jsonl",
        module_directory.parent / "data" / "seed.jsonl",
    )


def _existing_path(value: Any) -> Path | None:
    if not isinstance(value, (str, os.PathLike)):
        return None
    candidate = Path(value).expanduser()
    if not candidate.is_absolute():
        candidate = Path.cwd() / candidate
    try:
        candidate = candidate.resolve()
    except OSError:
        candidate = candidate.absolute()
    return candidate if candidate.is_file() else None


def _filtered_records(path: Path) -> list[str]:
    kept: list[str] = []
    for line in path.read_text(
        encoding="utf-8-sig",
        errors="replace",
    ).splitlines():
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if english_record(record):
            kept.append(
                json.dumps(
                    record,
                    ensure_ascii=False,
                    separators=(",", ":"),
                )
            )
    return kept


def filter_dataset(
    value: Any,
    data_root: Path,
    *,
    purpose: str,
    required: bool,
) -> Any:
    """Build an English-only JSONL file while preserving the input path type."""

    if not isinstance(value, (str, os.PathLike)):
        return value

    source = _existing_path(value)
    candidates: list[Path] = []
    if source is not None and source.suffix.lower() == ".jsonl":
        candidates.append(source)
    if required:
        candidates.extend(
            candidate
            for candidate in _candidate_english_datasets(data_root)
            if candidate.is_file() and candidate not in candidates
        )

    kept: list[str] = []
    selected_source: Path | None = None
    for candidate in candidates:
        kept = _filtered_records(candidate)
        if kept:
            selected_source = candidate
            break

    if selected_source is None and required:
        raise RuntimeError(
            "English Foundation could not find any English-only verified "
            "training records."
        )
    if selected_source is None and source is None:
        return value

    # Optional mixed files, such as accumulated episodes, must become an empty
    # guarded file when they contain no English records. Returning the original
    # file would silently leak C++ evidence through the English bootstrap gate.
    safe_purpose = re.sub(r"[^a-z0-9]+", "-", purpose.lower()).strip("-") or "data"
    output = data_root / "state" / f"guarded-english-{safe_purpose}.jsonl"
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(
        ("\n".join(kept) + "\n") if kept else "",
        encoding="utf-8",
    )
    os.replace(temporary, output)
    return _same_path_type(value, output)


def _serializable_namespace(namespace: argparse.Namespace) -> dict[str, Any]:
    return {
        name: str(value) if isinstance(value, os.PathLike) else value
        for name, value in vars(namespace).items()
    }


def write_status(
    data_root: Path,
    requested: dict[str, Any],
    effective: dict[str, Any],
    ready: bool,
) -> None:
    state = data_root / "state"
    state.mkdir(parents=True, exist_ok=True)
    document = {
        "schema_version": 2,
        "updated_at_utc": datetime.now(timezone.utc).isoformat(),
        "english_foundation_established": ready,
        "requested": requested,
        "effective": effective,
        "effective_internal_scope": internal_scope_for(
            effective.get("scope", ENGLISH_TARGET)
        ),
        "legacy_weights_reused": False,
        "app_data_root": str(data_root),
        "storage_report": str(state / "MODEL_STORAGE.txt"),
        "quarantine_root": str(data_root / "quarantine"),
    }
    path = state / "english-foundation-guard.json"
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)

    report = state / "MODEL_STORAGE.txt"
    if not report.exists():
        report.write_text(
            "ThreadSpire model storage\n"
            "========================\n"
            f"Application data root: {data_root}\n"
            f"Curriculum guard: {path}\n"
            f"Legacy quarantine: {data_root / 'quarantine'}\n"
            "Current policy: English Foundation owns internal weight training "
            "until its evaluation and bootstrap gate both pass.\n",
            encoding="utf-8",
        )


def enforce_namespace(namespace: argparse.Namespace) -> argparse.Namespace:
    """Apply the gate only to the internal full-weight training command."""

    if getattr(namespace, "command", "") != _INTERNAL_TRAIN_COMMAND:
        return namespace

    state_root = getattr(namespace, "state_root", None)
    if isinstance(state_root, (str, os.PathLike)):
        data_root = Path(state_root).expanduser().resolve()
    else:
        data_root = root()
    ready = established(data_root)
    requested = _serializable_namespace(namespace)

    if not ready:
        namespace.scope = ENGLISH_TARGET
        if hasattr(namespace, "seed_dataset"):
            namespace.seed_dataset = filter_dataset(
                namespace.seed_dataset,
                data_root,
                purpose="bootstrap",
                required=True,
            )
        if getattr(namespace, "episodes", None) is not None:
            namespace.episodes = filter_dataset(
                namespace.episodes,
                data_root,
                purpose="episodes",
                required=False,
            )
        if getattr(namespace, "anchors", None) is not None:
            namespace.anchors = filter_dataset(
                namespace.anchors,
                data_root,
                purpose="anchors",
                required=False,
            )
        os.environ["THREADSPIRE_EFFECTIVE_TRAINING_TARGET"] = ENGLISH_TARGET
        os.environ["THREADSPIRE_EFFECTIVE_INTERNAL_SCOPE"] = ENGLISH_INTERNAL_SCOPE

    effective = _serializable_namespace(namespace)
    write_status(data_root, requested, effective, ready)
    print(
        "[curriculum-guard] "
        f"English established: {ready}; "
        f"requested scope: {requested.get('scope', '')}; "
        f"effective target: {effective.get('scope', '')}; "
        f"internal scope: {internal_scope_for(effective.get('scope', ENGLISH_TARGET))}",
        file=sys.stderr,
        flush=True,
    )
    return namespace


def install() -> None:
    """Install environment diagnostics without monkey-patching argparse."""

    global _installed
    if _installed:
        return
    _installed = True
    print(
        f"[curriculum-guard] data root: {root()}",
        file=sys.stderr,
        flush=True,
    )
