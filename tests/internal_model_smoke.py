#!/usr/bin/env python3
"""CPU smoke test for ThreadSpire's random-initialized internal model.

This intentionally uses a tiny configuration so it can validate the model ABI,
checkpoint/preemption path, exact resume state, candidate evaluation, and
promotion without a GPU or downloaded weights.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile

import torch
from safetensors.torch import load_file


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    arguments = parser.parse_args()
    source_root = arguments.source_root.resolve()
    sys.path.insert(0, str(source_root / "resources"))

    import internal_model as model  # pylint: disable=import-outside-toplevel
    import model_worker as worker  # pylint: disable=import-outside-toplevel

    assert model.canonical_training_scope("english-foundation") == "english-core"
    assert model.canonical_training_scope("cpp23-core") == "cpp23-core"

    temporary = Path(tempfile.mkdtemp(prefix="threadspire-internal-smoke-"))
    try:
        assert model.assess_generation_quality(
            "A clear, ordinary English answer should pass the structural gate."
        ).accepted
        assert model.assess_generation_quality(
            "```cpp\n#include <vector>\nint main() { std::vector<int> values{1, 2, 3}; return values.size(); }\n```"
        ).accepted
        assert not model.assess_generation_quality("wing " * 80).accepted
        assert not model.assess_generation_quality("C" + "+" * 80).accepted
        repeated = model.ByteTokenizer.encode_text("abcdefghabcdefgh")
        blocked = model.blocked_no_repeat_tokens(repeated, 8, 512)
        assert model.ByteTokenizer.encode_text("a")[0] in blocked

        reference_episodes = temporary / "reference-episodes.jsonl"
        reference_episodes.write_text(
            json.dumps({
                "skill": "english-core",
                "eligible_for_training": True,
                "human_accepted": False,
                "deterministic_verified": False,
                "reference_verified": True,
                "prompt": "What is deterministic training evidence?",
                "response": "Evidence accepted by an independent or trusted reference gate.",
            }) + "\n",
            encoding="utf-8",
        )
        loaded_reference = worker._load_episodes(  # pylint: disable=protected-access
            reference_episodes,
            "english-core",
        )
        assert len(loaded_reference) == 1

        config = model.ModelConfig(
            name="ThreadSpire Internal Smoke",
            context_length=128,
            d_model=64,
            n_layers=2,
            n_heads=4,
            ffn_hidden=128,
            expert_hidden=32,
        )
        initialized = model.initialize_model(temporary, config, force=True)
        assert initialized["origin"] == "random-initialized-local-model"
        assert initialized["external_base_weights"] is False
        assert initialized["status"] == "initialized-untrained"

        # Foreground bootstrap owns the slot. Stale cooperative markers and a
        # dead lock must be removed rather than leaving the app "running" forever.
        stale_control = temporary / "stale-control"
        stale_control.mkdir(parents=True, exist_ok=True)
        (stale_control / "training.paused").write_text("stale\n", encoding="utf-8")
        (stale_control / "inference.99999999").write_text("stale\n", encoding="utf-8")
        (stale_control / "training.lock").write_text("pid=99999999\n", encoding="utf-8")
        with worker._training_slot(  # pylint: disable=protected-access
            stale_control,
            foreground=True,
            timeout_seconds=2.0,
        ):
            assert not (stale_control / "training.paused").exists()
            assert not (stale_control / "inference.99999999").exists()
            assert (stale_control / "training.active").exists()
        assert not (stale_control / "training.lock").exists()
        assert not (stale_control / "training.active").exists()

        sequences = model.load_training_sequences(
            [source_root / "resources" / "bootstrap.jsonl"],
            config.context_length,
        )
        english_only = model.load_training_sequences(
            [source_root / "resources" / "bootstrap.jsonl"],
            config.context_length,
            "english-core",
        )
        assert english_only
        assert all(route == ("english-core",) for _, _, route in english_only)
        english = next(item for item in sequences if item[2] == ("english-core",))
        specialized = next(item for item in sequences if "cpp23-core" in item[2])
        _, _, routes = model.collate_batch([english, specialized], torch.device("cpu"))
        assert routes[0] == ("english-core",)
        assert "cpp23-core" in routes[1]

        # Prove that a preemption during gradient accumulation is exactly
        # resumable, not merely restartable from the last full optimizer step.
        reference_root = temporary / "reference"
        resumed_root = temporary / "resumed"
        model.initialize_model(reference_root, config, force=True)
        model.initialize_model(resumed_root, config, force=True)
        reference = model.train_candidate(
            reference_root,
            [source_root / "resources" / "bootstrap.jsonl"],
            [source_root / "resources" / "bootstrap_anchors.jsonl"],
            "all",
            2,
            3.0e-4,
            1,
            2,
            1,
            reference_root / "control",
            False,
            defer_promotion=True,
        )
        assert reference["status"] == "candidate-ready"

        control = resumed_root / "control"
        control.mkdir(parents=True, exist_ok=True)
        marker = control / "inference.priority"
        original_checkpoint = model.save_training_checkpoint

        def checkpoint_then_preempt(*args: object, **kwargs: object) -> None:
            original_checkpoint(*args, **kwargs)
            if int(kwargs.get("steps", 0)) == 1:
                marker.write_text("resume-smoke\n", encoding="utf-8")

        model.save_training_checkpoint = checkpoint_then_preempt
        try:
            preempted_mid_accumulation = model.train_candidate(
                resumed_root,
                [source_root / "resources" / "bootstrap.jsonl"],
                [source_root / "resources" / "bootstrap_anchors.jsonl"],
                "all",
                2,
                3.0e-4,
                1,
                2,
                1,
                control,
                False,
                defer_promotion=True,
            )
        finally:
            model.save_training_checkpoint = original_checkpoint
        assert preempted_mid_accumulation["status"] == "preempted"
        assert preempted_mid_accumulation["steps"] == 1
        accumulation_checkpoint = (
            resumed_root / "models" / "internal" / "checkpoints" /
            "g000000-all" / "latest.pt"
        )
        checkpoint_payload = torch.load(
            accumulation_checkpoint,
            map_location="cpu",
            weights_only=False,
        )
        assert checkpoint_payload["gradients"]
        assert checkpoint_payload["accumulation_steps"] == 2
        marker.unlink()
        resumed = model.train_candidate(
            resumed_root,
            [source_root / "resources" / "bootstrap.jsonl"],
            [source_root / "resources" / "bootstrap_anchors.jsonl"],
            "all",
            2,
            3.0e-4,
            1,
            2,
            1,
            control,
            False,
            defer_promotion=True,
        )
        assert resumed["status"] == "candidate-ready"
        reference_weights = load_file(
            str(Path(reference["candidate_path"]) / "weights.safetensors"),
            device="cpu",
        )
        resumed_weights = load_file(
            str(Path(resumed["candidate_path"]) / "weights.safetensors"),
            device="cpu",
        )
        assert reference_weights.keys() == resumed_weights.keys()
        for name in reference_weights:
            assert torch.equal(reference_weights[name], resumed_weights[name]), name

        control = temporary / "control"
        control.mkdir(parents=True, exist_ok=True)
        (control / "inference.priority").write_text("smoke\n", encoding="utf-8")
        preempted = model.train_candidate(
            temporary,
            [source_root / "resources" / "bootstrap.jsonl"],
            [source_root / "resources" / "bootstrap_anchors.jsonl"],
            "all",
            2,
            3.0e-4,
            1,
            1,
            1,
            control,
            False,
        )
        assert preempted["status"] == "preempted"
        checkpoint = temporary / "models" / "internal" / "checkpoints" / "g000000-all" / "latest.pt"
        assert checkpoint.is_file()

        (control / "inference.priority").unlink()
        completed = model.train_candidate(
            temporary,
            [source_root / "resources" / "bootstrap.jsonl"],
            [source_root / "resources" / "bootstrap_anchors.jsonl"],
            "all",
            2,
            3.0e-4,
            1,
            1,
            1,
            control,
            False,
            defer_promotion=True,
        )
        assert completed["status"] == "candidate-ready"
        assert completed["passed_evaluation_bool"] is True
        assert completed["promoted_bool"] is False
        assert not checkpoint.exists()
        before_promotion = model.status_payload(temporary)
        assert before_promotion["ready"] is False
        assert before_promotion["generation"] == "g000000"

        promoted = model.promote_generation(
            temporary,
            completed["generation"],
            completed["parent_generation"],
        )
        assert promoted["status"] == "promoted"
        status = model.status_payload(temporary)
        assert status["ready"] is True
        assert status["external_base_weights"] is False
        assert status["generation"] == "g000001"

        bootstrap_root = temporary / "bootstrap-target"
        model.initialize_model(bootstrap_root, config, force=True)
        progress_path = bootstrap_root / "state" / "internal-model-progress.json"
        bootstrap = model.train_candidate(
            bootstrap_root,
            [str(source_root / "resources" / "bootstrap.jsonl")],
            [str(source_root / "resources" / "bootstrap_anchors.jsonl")],
            "english-foundation",
            2,
            3.0e-4,
            1,
            1,
            1,
            bootstrap_root / "control",
            False,
            progress_file=progress_path,
            bootstrap_target_steps=2,
            honor_inference_priority=False,
        )
        assert bootstrap["status"] == "promoted"
        assert bootstrap["bootstrap_complete"] is True
        bootstrap_status = model.status_payload(bootstrap_root)
        assert bootstrap_status["ready"] is True
        assert bootstrap_status["bootstrap_target_steps"] == 2
        assert bootstrap_status["bootstrap_complete"] is True
        progress = json.loads(progress_path.read_text(encoding="utf-8"))
        assert progress["phase"] == "complete"
        assert progress["requested_scope"] == "english-foundation"
        assert progress["scope"] == "english-core"
        assert progress["bootstrap_complete"] is True
        assert progress["heartbeat_unix_ms"] > 0
        curriculum = json.loads(
            (bootstrap_root / "state" / "curriculum-state.json").read_text(encoding="utf-8")
        )
        assert curriculum["english_foundation"]["evaluation_passed"] is True
        assert curriculum["english_foundation"]["established"] is True

        try:
            model.promote_generation(
                temporary,
                completed["generation"],
                completed["parent_generation"],
            )
        except RuntimeError as error:
            assert "stale internal candidate" in str(error)
        else:
            raise AssertionError("stale candidate promotion unexpectedly succeeded")

        active = model.load_active_model(
            temporary,
            device=torch.device("cpu"),
            dtype=torch.float32,
        )
        model.save_generation(
            temporary,
            active.model,
            active.config,
            "g000002",
            status="trained",
            parent="g000001",
            steps=int(active.manifest.get("steps", 0)),
            tokens_seen=int(active.manifest.get("tokens_seen", 0)),
            validation_loss=active.manifest.get("validation_loss"),
            training_scope="rollback-smoke",
            promote=True,
        )
        rolled_back = model.activate_generation(temporary, "g000001", "g000002")
        assert rolled_back["status"] == "activated"
        assert model.status_payload(temporary)["generation"] == "g000001"
        try:
            model.activate_generation(temporary, "g000001", "g000002")
        except RuntimeError as error:
            assert "stale internal rollback" in str(error)
        else:
            raise AssertionError("stale internal rollback unexpectedly succeeded")

        planning = model.load_training_sequences(
            [source_root / "resources" / "bootstrap.jsonl"],
            config.context_length,
            "planning",
        )
        assert planning
        assert all("planning" in route for _, _, route in planning[:16])
        print("ThreadSpire internal-model smoke test passed")
        return 0
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
