#!/usr/bin/env python3
"""CPU smoke test for EngCoder's random-initialized internal model.

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

    training_records = [
        json.loads(line)
        for line in (source_root / "resources" / "bootstrap.jsonl").read_text(
            encoding="utf-8"
        ).splitlines()
        if line.strip()
    ]
    anchor_records = [
        json.loads(line)
        for line in (source_root / "resources" / "bootstrap_anchors.jsonl").read_text(
            encoding="utf-8"
        ).splitlines()
        if line.strip()
    ]
    normalize = lambda value: " ".join(str(value).strip().casefold().split())
    reserved = {
        normalize("Hi. Are you ready to help?"),
        normalize("Convert this to lowercase: THREAD AND CHECKPOINT"),
        normalize("The application don't respond."),
        normalize("Describe throughput without an analogy."),
        normalize("Reply with exactly these two words: Ready now"),
        normalize("What should I inspect first?"),
    }
    training_users = {
        normalize(message["content"])
        for record in training_records
        for message in record.get("messages", [])
        if message.get("role") == "user"
    }
    anchor_users = {
        normalize(message["content"])
        for record in anchor_records
        for message in record.get("messages", [])
        if message.get("role") == "user"
    }
    assert training_users.isdisjoint(reserved)
    assert {
        normalize("Convert this to lowercase: THREAD AND CHECKPOINT"),
        normalize("The application don't respond."),
        normalize("Describe throughput without an analogy."),
    }.issubset(anchor_users)
    instruction_systems = {
        normalize(record["messages"][0]["content"])
        for record in training_records
        if "instruction-following" in record.get("tags", [])
    }
    assert len(instruction_systems) >= 4
    training_identity = [
        record for record in training_records
        if "identity" in record.get("tags", [])
    ]
    held_out_identity = [
        record for record in anchor_records
        if "identity" in record.get("tags", [])
    ]
    assert len(training_identity) >= 8
    assert len(held_out_identity) >= 8
    assert {
        normalize(record["messages"][-1]["content"])
        for record in training_identity + held_out_identity
    }
    assert all(
        "engcoder" in normalize(record["messages"][-1]["content"])
        and "threadspire" not in normalize(record["messages"][-1]["content"])
        for record in training_identity + held_out_identity
    )
    assert sum(
        "exact-copy" in record.get("tags", [])
        for record in training_records
    ) >= 100
    assert sum(
        "multiturn" in record.get("tags", [])
        and len(record.get("messages", [])) >= 5
        for record in training_records
    ) >= 20

    assert model.canonical_training_scope("english-foundation") == "english-core"
    assert model.canonical_training_scope("cpp23-core") == "cpp23-core"
    initial_rate = 3.0e-4
    extended_rate = model.bounded_bootstrap_learning_rate(
        initial_rate, 24_000, 0, 36_000, 24_000
    )
    assert abs(extended_rate - initial_rate * 0.10) < 1.0e-12
    assert model.bounded_bootstrap_learning_rate(
        initial_rate, 0, 0, 24_000, 0
    ) == initial_rate

    temporary = Path(tempfile.mkdtemp(prefix="threadspire-internal-smoke-"))
    try:
        assert model.strip_training_emoji("Yo! What's good? ⚽ 😁") == "Yo! What's good?"
        assert model.strip_training_emoji("hello😀world") == "hello world"
        assert model.strip_training_emoji("1️⃣") == ""
        assert model.english_vocabulary_ratio(
            "Three logs were added; it now contains them."
        ) == 1.0
        assert model.sanitize_training_response(
            "I'm ornith, your local model. ⚽",
            "lmstudio-ornith-1.0-9b",
        ) == "I am EngCoder."
        assert model.sanitize_training_response(
            "I am ThreadSpire's internal model.",
            "threadspire://fast",
        ) == "I am EngCoder's internal model."

        sanitized_episodes = temporary / "sanitized-episodes.jsonl"
        sanitized_episodes.write_text(
            json.dumps({
                "skill": "english-core",
                "eligible_for_training": True,
                "human_accepted": False,
                "deterministic_verified": False,
                "reference_verified": True,
                "model_id": "lmstudio-ornith-1.0-9b",
                "prompt": "What is up? ⚽",
                "response": "I'm ornith, your local model. 😁",
            }) + "\n",
            encoding="utf-8",
        )
        cleaned_records = worker._load_episodes(  # pylint: disable=protected-access
            sanitized_episodes,
            "english-core",
        )
        assert len(cleaned_records) == 1
        assert cleaned_records[0]["prompt"] == "What is up?"
        assert cleaned_records[0]["response"] == "I am EngCoder."
        assert "ornith" not in cleaned_records[0]["response"].lower()
        cleaned_messages = model._record_messages(  # pylint: disable=protected-access
            json.loads(sanitized_episodes.read_text(encoding="utf-8"))
        )
        assert cleaned_messages == [
            {"role": "user", "content": "What is up?"},
            {"role": "assistant", "content": "I am EngCoder."},
        ]

        assert model.assess_generation_quality(
            "A clear, ordinary English answer should pass the structural gate."
        ).accepted
        open_conversation = [
            {"role": "system", "content": "Reply naturally in plain English."},
            {"role": "user", "content": "What do you mean by that?"},
        ]
        assert not model.assess_generation_quality(
            "-1", messages=open_conversation
        ).accepted
        assert not model.assess_generation_quality(
            "Yes. Resource ready.", messages=open_conversation
        ).accepted
        assert not model.assess_generation_quality(
            "In the prerorerevin ghe correcteante.", messages=open_conversation
        ).accepted
        assert not model.assess_generation_quality(
            "The worker has worke compiled whecklaaity active generation.",
            messages=open_conversation,
        ).accepted
        assert model.assess_generation_quality(
            "Tell me which part you want me to clarify.", messages=open_conversation
        ).accepted
        arithmetic_request = [
            {"role": "user", "content": "Calculate 17 + 25."},
        ]
        assert model.assess_generation_quality(
            "42", messages=arithmetic_request
        ).accepted
        assert model.verified_local_response([
            {"role": "system", "content": "Return only the transformed text."},
            {"role": "user", "content": "Convert this to lowercase: SAFE LOCAL HOST"},
        ]) == "safe local host"
        assert model.verified_local_response([
            {"role": "user", "content": "Change RUNNING to lowercase."},
        ]) == "running"
        assert model.verified_local_response([
            {"role": "user", "content": "Convert quiet mode into uppercase."},
        ]) == "QUIET MODE"
        assert model.assess_generation_quality(
            "QUIET MODE",
            messages=[{"role": "user", "content": "Convert quiet mode into uppercase."}],
        ).accepted
        assert model.verified_local_response([
            {"role": "system", "content": "Return only the number."},
            {"role": "user", "content": "Compute 19 + 23."},
        ]) == "42"
        assert model.verified_local_response([
            {"role": "system", "content": "Follow the requested format exactly."},
            {"role": "user", "content": "Reply with exactly these two words: Stay safe"},
        ]) == "Stay safe"
        assert model.verified_local_response([
            {"role": "system", "content": "Correct the English and return only the sentence."},
            {"role": "user", "content": "These service is active."},
        ]) == "These services are active."
        assert model.verified_local_response([
            {"role": "system", "content": "Reply naturally in plain English."},
            {"role": "user", "content": "Hello, could you help with this?"},
        ]) == "Hello! What would you like help with?"
        assert model.verified_local_response([
            {"role": "user", "content": "An unrelated request with no verified local result."},
        ]) is None
        exact_training_record = next(
            record
            for record in training_records
            if record.get("domain") == "english"
            and record.get("messages")
            and record["messages"][-1].get("role") == "assistant"
        )
        assert model.verified_local_response(
            exact_training_record["messages"][:-1],
            source_root / "resources" / "bootstrap.jsonl",
        ) == exact_training_record["messages"][-1]["content"]
        curated_draft = worker._internal_draft(  # pylint: disable=protected-access
            None,
            exact_training_record["messages"][:-1],
            "fast",
            256,
            0.0,
            trusted_dataset=source_root / "resources" / "bootstrap.jsonl",
        )
        assert curated_draft.quality.accepted
        assert curated_draft.content == exact_training_record["messages"][-1]["content"]

        assert model.assess_generation_quality(
            "```cpp\n#include <vector>\nint main() { std::vector<int> values{1, 2, 3}; return values.size(); }\n```"
        ).accepted
        assert not model.assess_generation_quality("wing " * 80).accepted
        assert not model.assess_generation_quality("C" + "+" * 80).accepted
        assert not model.assess_generation_quality(
            "Rrerorere at at an at at t an athe an at perorororrorrorore "
            "an an at rrrorororongerororon a"
        ).accepted
        assert not model.assess_generation_quality(
            "ND aterererere aterererererrerrere at atererorereroresterererrererest"
        ).accepted
        assert not model.assess_generation_quality(
            "30 Cou conthis a core bence A cthect in correcove c"
        ).accepted
        assert model.english_vocabulary_ratio(
            "Throughput is the amount of work completed per unit of time."
        ) >= 0.9
        external_vocabulary = temporary / "external-bootstrap.jsonl"
        external_vocabulary.write_text(
            json.dumps({
                "domain": "english-core",
                "tags": ["english"],
                "messages": [
                    {"role": "user", "content": "Check the lexicon."},
                    {
                        "role": "assistant",
                        "content": "Checkpoint throughput application remains deterministic.",
                    },
                ],
            }) + "\n",
            encoding="utf-8",
        )
        assert model.english_vocabulary_ratio(
            "Checkpoint throughput application remains deterministic.",
            external_vocabulary,
        ) == 1.0
        assert not model.assess_generation_quality(
            "The application dooten respond.0 I coded the response."
        ).accepted
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

        human_episodes = temporary / "human-episodes.jsonl"
        human_records = [
            {
                "skill": "english-core",
                "eligible_for_training": True,
                "human_accepted": True,
                "human_rejected": False,
                "deterministic_verified": False,
                "reference_verified": False,
                "prompt": "What is up?",
                "response": "Not much. What is on your mind?",
            },
        ]
        human_episodes.write_text(
            "\n".join(json.dumps(record) for record in human_records) + "\n",
            encoding="utf-8",
        )
        assert worker._load_episodes(  # pylint: disable=protected-access
            human_episodes,
            "english-core",
        ) == []
        assert model.load_training_sequences(
            [human_episodes],
            128,
            "english-core",
        ) == []

        human_records.extend([
            {
                **human_records[0],
                "prompt": f"Distinct approved conversation {index}",
                "response": f"Distinct verified response {index}.",
            }
            for index in range(2, 5)
        ])
        for index, record in enumerate(human_records):
            record["eligible_for_training"] = index == len(human_records) - 1
        human_episodes.write_text(
            "\n".join(json.dumps(record) for record in human_records) + "\n",
            encoding="utf-8",
        )
        assert len(worker._load_episodes(  # pylint: disable=protected-access
            human_episodes,
            "english-core",
        )) == 4
        assert len(model.load_training_sequences(
            [human_episodes],
            128,
            "english-core",
        )) == 4

        memory_only_episodes = temporary / "memory-only-episodes.jsonl"
        memory_only_episodes.write_text(
            "\n".join(
                json.dumps({**record, "skill": "english-conversation"})
                for record in human_records
            ) + "\n",
            encoding="utf-8",
        )
        assert len(worker._load_episodes(  # pylint: disable=protected-access
            memory_only_episodes,
            "english-conversation",
        )) == 4
        assert len(model.load_training_sequences(
            [memory_only_episodes],
            128,
            "english-conversation",
        )) == 4

        hard_correction = {
            **human_records[0],
            "skill": "english-conversation",
            "eligible_for_training": False,
            "human_accepted": False,
            "human_rejected": True,
            "prompt": "hi",
            "response": "Yes. I can help with that problem.",
            "correction": "Hello!",
            "provenance": "interactive-hard-correction",
        }
        with memory_only_episodes.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(hard_correction) + "\n")
        loaded_corrections = worker._load_episodes(  # pylint: disable=protected-access
            memory_only_episodes,
            "english-conversation",
        )
        assert any(
            record.get("hard_correction") and record["response"] == "Hello!"
            for record in loaded_corrections
        )
        assert len(model.load_training_sequences(
            [memory_only_episodes],
            128,
            "english-conversation",
        )) == 5
        config = model.ModelConfig(
            name="EngCoder Internal Smoke",
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
        assert checkpoint_payload["bootstrap_policy"] == model.BOOTSTRAP_POLICY
        assert len(checkpoint_payload["training_data_fingerprint"]) == 64
        assert len(checkpoint_payload["anchor_data_fingerprint"]) == 64
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
        assert status["ready"] is False
        assert status["external_base_weights"] is False
        assert status["generation"] == "g000001"

        bootstrap_root = temporary / "bootstrap-target"
        model.initialize_model(bootstrap_root, config, force=True)
        progress_path = bootstrap_root / "state" / "internal-model-progress.json"
        original_readiness = model.evaluate_generation_readiness
        model.evaluate_generation_readiness = lambda *_args, **_kwargs: (
            model.GenerationReadiness(
                True,
                1.0,
                ({
                    "name": "smoke",
                    "passed": True,
                    "structural_quality": True,
                    "reason": "deterministic smoke stub",
                    "output": "thread and checkpoint",
                },),
            )
        )
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
        model.evaluate_generation_readiness = original_readiness
        assert bootstrap["status"] == "promoted"
        assert bootstrap["bootstrap_complete"] is True
        assert bootstrap["training_scope"] == "all"
        assert bootstrap["training_data_scope"] == "english-core"
        assert bootstrap["full_backbone_trained"] is True
        assert bootstrap["generation_ready"] is True
        bootstrap_status = model.status_payload(bootstrap_root)
        assert bootstrap_status["ready"] is True
        assert bootstrap_status["bootstrap_target_steps"] == 2
        assert bootstrap_status["bootstrap_complete"] is True
        progress = json.loads(progress_path.read_text(encoding="utf-8"))
        assert progress["phase"] == "complete"
        assert progress["requested_scope"] == "english-foundation"
        assert progress["scope"] == "all"
        assert progress["training_data_scope"] == "english-core"
        assert progress["bootstrap_complete"] is True
        assert progress["full_backbone_trained"] is True
        assert progress["generation_ready"] is True
        assert progress["heartbeat_unix_ms"] > 0
        curriculum = json.loads(
            (bootstrap_root / "state" / "curriculum-state.json").read_text(encoding="utf-8")
        )
        assert curriculum["english_foundation"]["evaluation_passed"] is True
        assert curriculum["english_foundation"]["established"] is True
        assert curriculum["english_foundation"]["full_backbone_trained"] is True
        assert curriculum["english_foundation"]["generation_ready"] is True
        preflight_only = model.train_candidate(
            bootstrap_root,
            [str(source_root / "resources" / "bootstrap.jsonl")],
            [str(source_root / "resources" / "bootstrap_anchors.jsonl")],
            "english-foundation",
            50,
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
        assert preflight_only["status"] == "promoted", preflight_only
        assert preflight_only["steps"] == 0
        assert preflight_only["total_steps"] == 2
        assert preflight_only["bootstrap_complete"] is True
        preflight_progress = json.loads(progress_path.read_text(encoding="utf-8"))
        assert preflight_progress["phase"] == "complete"
        assert preflight_progress["message"] == "Saved local host passed every readiness check and is now active"
        assert preflight_progress["step"] == 0
        assert preflight_progress["total_steps"] == 2

        repair_root = temporary / "bootstrap-repair"
        model.initialize_model(repair_root, config, force=True)
        repair_progress = repair_root / "state" / "internal-model-progress.json"
        repair_original_readiness = model.evaluate_generation_readiness
        repair_original_loss = model.evaluate_loss
        try:
            model.evaluate_generation_readiness = lambda *_args, **_kwargs: (
                model.GenerationReadiness(
                    False,
                    0.0,
                    ({
                        "name": "repair-smoke",
                        "passed": False,
                        "structural_quality": True,
                        "reason": "forced repair continuation",
                        "output": "partial but structurally valid English",
                    },),
                )
            )
            losses = iter((5.0, 4.0, 4.0, 3.0))
            model.evaluate_loss = lambda *_args, **_kwargs: next(losses)
            first_repair = model.train_candidate(
                repair_root,
                [source_root / "resources" / "bootstrap.jsonl"],
                [source_root / "resources" / "bootstrap_anchors.jsonl"],
                "english-foundation",
                1,
                3.0e-4,
                1,
                1,
                1,
                repair_root / "control",
                False,
                progress_file=repair_progress,
                bootstrap_target_steps=1,
                honor_inference_priority=False,
            )
            assert first_repair["status"] == "rejected"
            assert first_repair["generation"] == "g000001"
            assert model.resolve_bootstrap_repair_generation(
                repair_root, "g000000"
            ) == "g000001"
            assert json.loads(repair_progress.read_text(encoding="utf-8"))["phase"] == "repair-required"

            continued_repair = model.train_candidate(
                repair_root,
                [source_root / "resources" / "bootstrap.jsonl"],
                [source_root / "resources" / "bootstrap_anchors.jsonl"],
                "english-foundation",
                1,
                3.0e-4,
                1,
                1,
                1,
                repair_root / "control",
                False,
                progress_file=repair_progress,
                bootstrap_target_steps=1,
                honor_inference_priority=False,
            )
            assert continued_repair["parent_generation"] == "g000001"
            assert continued_repair["generation"] == "g000002"
            assert continued_repair["active_generation_at_start"] == "g000000"
            assert continued_repair["resumed_from_repair_generation"] is True
            assert continued_repair["training_scope"] == "all"
            assert continued_repair["total_steps"] == 2
            assert model.status_payload(repair_root)["generation"] == "g000000"
        finally:
            model.evaluate_generation_readiness = repair_original_readiness
            model.evaluate_loss = repair_original_loss


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
        print("EngCoder internal-model smoke test passed")
        return 0
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
