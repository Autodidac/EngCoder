#!/usr/bin/env python3
"""Regression test for the English-first Python training boundary."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile


def _record(identifier: str, domain: str, response: str) -> dict[str, object]:
    return {
        "id": identifier,
        "domain": domain,
        "accepted": True,
        "tags": [domain],
        "messages": [
            {"role": "user", "content": "Provide the requested answer."},
            {"role": "assistant", "content": response},
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    arguments = parser.parse_args()
    source_root = arguments.source_root.resolve()
    sys.path.insert(0, str(source_root / "resources"))

    temporary = Path(tempfile.mkdtemp(prefix="threadspire-curriculum-guard-"))
    previous_root = os.environ.get("THREADSPIRE_DATA_ROOT")
    try:
        os.environ["THREADSPIRE_DATA_ROOT"] = str(temporary)
        data = temporary / "data"
        data.mkdir(parents=True)
        mixed = data / "mixed.jsonl"
        mixed.write_text(
            "\n".join(
                json.dumps(record)
                for record in (
                    _record("english", "english", "This is an English answer."),
                    _record("cpp", "cpp23", "```cpp\nint value{};\n```"),
                )
            )
            + "\n",
            encoding="utf-8",
        )

        import curriculum_guard as guard  # pylint: disable=import-outside-toplevel

        cpp_only_episodes = data / "cpp-only-episodes.jsonl"
        cpp_only_episodes.write_text(
            json.dumps(_record("cpp-episode", "general", "```cpp\nint value{};\n```")) + "\n",
            encoding="utf-8",
        )

        parsed = guard.enforce_namespace(argparse.Namespace(
            command="train-internal",
            state_root=temporary,
            seed_dataset=mixed,
            episodes=cpp_only_episodes,
            anchors=mixed,
            scope="cpp23-core",
        ))
        assert parsed.scope == "english-foundation"
        assert isinstance(parsed.seed_dataset, Path)
        assert isinstance(parsed.episodes, Path)
        assert isinstance(parsed.anchors, Path)
        assert parsed.seed_dataset.is_file()
        assert parsed.episodes.is_file()
        assert parsed.anchors.is_file()
        assert "cpp23" not in parsed.seed_dataset.read_text(encoding="utf-8")
        assert parsed.episodes.read_text(encoding="utf-8") == ""
        assert "```cpp" not in parsed.anchors.read_text(encoding="utf-8")

        unrelated = argparse.Namespace(
            command="inspect-module",
            module=Path("candidate.module"),
        )
        guarded_unrelated = guard.enforce_namespace(unrelated)
        assert isinstance(guarded_unrelated.module, Path)
        assert guarded_unrelated.module == Path("candidate.module")

        curriculum_state = temporary / "state" / "curriculum-state.json"
        curriculum_state.write_text(
            json.dumps({
                "english_foundation": {
                    "evaluation_passed": True,
                    "established": True,
                    "bootstrap_policy": "full-backbone-english-v4",
                }
            }),
            encoding="utf-8",
        )
        ready = argparse.Namespace(
            command="train-internal",
            state_root=temporary,
            seed_dataset=mixed,
            anchors=mixed,
            scope="cpp23-core",
        )
        ready = guard.enforce_namespace(ready)
        assert ready.scope == "cpp23-core"
        assert ready.seed_dataset == mixed
        assert isinstance(ready.seed_dataset, Path)

        print("curriculum guard smoke test passed")
        return 0
    finally:
        if previous_root is None:
            os.environ.pop("THREADSPIRE_DATA_ROOT", None)
        else:
            os.environ["THREADSPIRE_DATA_ROOT"] = previous_root
        shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
