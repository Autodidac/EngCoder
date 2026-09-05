#!/usr/bin/env python3
"""Dependency-free smoke coverage for the minimal curriculum compiler."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile
from typing import Any


def _record(
    identifier: str,
    prompt: str,
    response: str,
    *,
    created: int = 100,
    tags: tuple[str, ...] = ("english",),
    **extra: Any,
) -> dict[str, Any]:
    record: dict[str, Any] = {
        "id": identifier,
        "skill": "english-core",
        "prompt": prompt,
        "response": response,
        "score": 0.95,
        "deterministic_verified": True,
        "eligible_for_training": True,
        "created_unix_ms": created,
        "tags": list(tags),
    }
    record.update(extra)
    return record


def _write_jsonl(path: Path, records: list[dict[str, Any]]) -> bytes:
    data = ("\n".join(json.dumps(record, ensure_ascii=False) for record in records) + "\n").encode("utf-8")
    path.write_bytes(data)
    return data


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]


def _compile_case(
    compiler: Any,
    root: Path,
    name: str,
    records: list[dict[str, Any]],
    *,
    anchors: list[dict[str, Any]] | None = None,
    **options: Any,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    source = root / f"{name}-source.jsonl"
    output = root / f"{name}-selected.jsonl"
    manifest_path = root / f"{name}-manifest.json"
    _write_jsonl(source, records)
    anchor_paths: tuple[Path, ...] = ()
    if anchors is not None:
        anchor = root / f"{name}-anchors.jsonl"
        _write_jsonl(anchor, anchors)
        anchor_paths = (anchor,)
    manifest = compiler.compile_curriculum(
        source,
        output,
        manifest_path,
        anchor_paths=anchor_paths,
        token_budget=options.pop("token_budget", 4096),
        **options,
    )
    return _read_jsonl(output), manifest


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

    import curriculum_compiler as compiler  # pylint: disable=import-outside-toplevel

    # Validation is tied to complete corpus passes and optimizer boundaries,
    # not an arbitrary fixed number of replays.  This policy remains testable
    # without installing the optional PyTorch trainer.
    foundation_schedule = compiler.training_validation_schedule(
        2, 1, 1, 10, foundation=True
    )
    assert foundation_schedule.corpus_pass_steps == 2
    assert foundation_schedule.interval_steps == 2
    assert foundation_schedule.minimum_steps == 4
    aligned_schedule = compiler.training_validation_schedule(
        5, 2, 4, 100, foundation=True
    )
    assert aligned_schedule.corpus_pass_steps == 3
    assert aligned_schedule.interval_steps == 4
    assert aligned_schedule.minimum_steps == 8
    incremental_schedule = compiler.training_validation_schedule(
        5, 2, 4, 100, foundation=False
    )
    assert incremental_schedule.minimum_steps == 4
    capped_schedule = compiler.training_validation_schedule(
        100, 2, 4, 3, foundation=True
    )
    assert capped_schedule.interval_steps == 52
    assert capped_schedule.minimum_steps == 3

    temporary = Path(tempfile.mkdtemp(prefix="threadspire-curriculum-compiler-"))
    try:
        # Public behavior signatures collapse bounded paraphrases and operand
        # substitutions, while preserving opposite instruction polarity.
        assert compiler.behavior_signature("Define latency.", "A delay.") == compiler.behavior_signature(
            "What does latency mean?", "A delay."
        )
        assert compiler.behavior_signature("Calculate 2 plus 2.", "4") == compiler.behavior_signature(
            "What is 41 + 1?", "42"
        )
        assert compiler.behavior_signature("Don't reveal `api_token` from C:\\old\\one.txt.", "No.") == compiler.behavior_signature(
            "Do not reveal `secret_key` from D:\\new\\two.txt!", "No."
        )
        casual = compiler.behavior_signature("What's up?", "Not much.")
        assert casual == compiler.behavior_signature("What is up?", "Not much.")
        assert casual == compiler.behavior_signature("Whats up", "Not much.")
        assert casual == compiler.behavior_signature("What's going on?", "Not much.")
        greeting = compiler.behavior_signature("Hi", "Hello!")
        assert greeting == compiler.behavior_signature("hello", "Hi there.")
        assert greeting == compiler.behavior_signature("Howdy", "Good to see you.")
        assert compiler.behavior_signature(
            "Include private identifiers in the answer.", "Yes, include them."
        ) != compiler.behavior_signature(
            "Do not include private identifiers in the answer.", "No, omit them."
        )
        assert compiler.behavior_signature(
            "Round 5 divided by 2 up.", "3"
        ) != compiler.behavior_signature(
            "Round 5 divided by 2 down.", "2"
        )
        assert compiler.behavior_signature(
            "Sort items oldest first.", "oldest, newest"
        ) != compiler.behavior_signature(
            "Sort items newest first.", "newest, oldest"
        )
        assert compiler.behavior_signature(
            "Summarize with all details.", "All details."
        ) != compiler.behavior_signature(
            "Summarize omitting all details.", "No details."
        )
        exact_copy = compiler.behavior_signature(
            "Reply with exactly these two words: Build available", "Build available"
        )
        assert exact_copy == compiler.behavior_signature(
            "Return only these two words: Router next", "Router next"
        )
        assert exact_copy == compiler.behavior_signature(
            "Use exactly this two-word response: Stop saved", "Stop saved"
        )
        assert not compiler.behavior_signature(
            "Reply with exactly these two words: Build available", "Different response"
        ).startswith("exact-copy:")
        for distinct_prompt, distinct_response in (
            ("Return only this one word: Available", "Available"),
            ("Return only these two words: BUILD AVAILABLE", "BUILD AVAILABLE"),
            ("Return only these two words: Build, available!", "Build, available!"),
            ("Return only these two words: Build  available", "Build  available"),
            ('Return only these two words: "Build available"', "Build available"),
            ("Output exactly these two words:\nBuild\navailable", "Build\navailable"),
        ):
            assert exact_copy != compiler.behavior_signature(distinct_prompt, distinct_response)

        records = [
            _record("latency-long", "Please provide a concise definition of latency.", "Latency is the delay before a response is observed."),
            _record("latency-short", "Define latency.", "A delay."),
            _record("latency-meaning", "What does latency mean?", "Delay."),
            _record("add-a", "Calculate 2 plus 2.", "4", tags=("english", "arithmetic")),
            _record("add-b", "What is 41 + 1?", "42", tags=("english", "arithmetic")),
            _record("subtract", "What is 41 - 1?", "40", tags=("english", "arithmetic")),
            _record("allow-private", "Include private identifiers in the answer.", "Yes, include them.", tags=("english", "constraints")),
            _record("deny-private", "Do not include private identifiers in the answer.", "No, omit them.", tags=("english", "constraints")),
            _record("stale-answer", "State the release color.", "Red.", created=100, human_accepted=True, deterministic_verified=False),
            _record(
                "older-correction",
                "State the release color.",
                "Red.",
                created=200,
                correction="Green.",
                provenance="interactive-hard-correction",
                human_rejected=True,
                deterministic_verified=False,
            ),
            _record(
                "newest-correction",
                "State the release color.",
                "Red.",
                created=300,
                correction="Blue.",
                provenance="interactive-hard-correction",
                human_rejected=True,
                deterministic_verified=False,
            ),
            _record("anchor-copy", "Define throughput.", "Work completed per unit of time."),
            _record("weighted-once", "Define bandwidth.", "A capacity for transfer.", curriculum_weight=4),
            _record("rejected", "Define a rejected item.", "No.", accepted=False),
            _record("ineligible", "Define an ineligible item.", "No.", eligible_for_training=False),
            _record("error", "Define an error item.", "No.", status="error"),
            _record("private", "Define a private item.", "No.", private=True),
            _record("private-text", "My email is alice@example.com.", "Remembered."),
            _record("volatile", "What is the current version installed?", "Version 99."),
            _record("branding", "Introduce yourself.", "I am ChatGPT, an AI language model."),
            _record("emoji", "Define joy.", "Joy 😀"),
            _record("emoji-only", "Define a pictograph.", "😀"),
            _record("spanish", "Define hola.", "Un saludo.", language="es"),
            *[
                _record(
                    f"same-answer-{index}",
                    prompt,
                    "Use one clear sentence.",
                    tags=("english", f"writing-{index}"),
                )
                for index, prompt in enumerate((
                    "Explain a sentence boundary.",
                    "Compare active and passive voice.",
                    "Describe a concise paragraph.",
                    "Clarify the purpose of punctuation.",
                    "Give advice about readable prose.",
                    "Discuss the order of clauses.",
                    "State a rule for plain language.",
                ))
            ],
        ]
        anchor = temporary / "anchors.jsonl"
        _write_jsonl(anchor, [_record("held-out-throughput", "Define throughput.", "A rate of completed work.")])

        source_a = temporary / "source-a.jsonl"
        original_a = _write_jsonl(source_a, records)
        output_a = temporary / "selected-a.jsonl"
        manifest_a_path = temporary / "manifest-a.json"
        manifest_a = compiler.compile_curriculum(
            source_a,
            output_a,
            manifest_a_path,
            anchor_paths=(anchor,),
            scope="english-foundation",
            token_budget=4096,
            max_per_cluster=1,
        )
        assert source_a.read_bytes() == original_a, "the append-only source was modified"
        selected_a = _read_jsonl(output_a)
        selected_by_id = {record["id"]: record for record in selected_a}
        selected_ids = set(selected_by_id)

        assert "latency-meaning" in selected_ids
        assert not {"latency-long", "latency-short"}.intersection(selected_ids)
        assert len({"add-a", "add-b"}.intersection(selected_ids)) == 1
        assert "subtract" in selected_ids
        assert {"allow-private", "deny-private"}.issubset(selected_ids)
        assert "newest-correction" in selected_ids
        assert selected_by_id["newest-correction"]["response"] == "Blue."
        assert not {"stale-answer", "older-correction"}.intersection(selected_ids)
        assert "anchor-copy" not in selected_ids
        assert "weighted-once" in selected_ids
        assert "curriculum_weight" not in selected_by_id["weighted-once"]
        assert "emoji" in selected_ids
        assert selected_by_id["emoji"]["response"] == "Joy"
        assert not {
            "rejected", "ineligible", "error", "private", "private-text",
            "volatile", "branding", "emoji-only", "spanish",
        }.intersection(selected_ids)
        assert len({identifier for identifier in selected_ids if identifier.startswith("same-answer-")}) == 4
        assert manifest_a["counts"]["stripped_curriculum_weight"] == 1
        assert manifest_a["counts"]["stripped_emoji_records"] == 1
        assert manifest_a["reasons"]["superseded_by_correction"] == 2
        assert manifest_a["reasons"]["anchor_overlap"] == 1
        assert manifest_a["reasons"]["answer_duplicate"] >= 3
        for reason in (
            "rejected", "not_eligible", "error", "private", "volatile",
            "branding", "emoji_noise", "non_english",
        ):
            assert manifest_a["reasons"][reason] >= 1, reason

        # Exact-copy payload vocabulary is not a new behavior. Structural
        # constraints remain separate so the compact set still teaches case,
        # punctuation, whitespace, quoting, word count, and multiple lines.
        copy_rows, copy_manifest = _compile_case(
            compiler,
            temporary,
            "exact-copy-shapes",
            [
                _record("copy-build", "Reply with exactly these two words: Build available", "Build available"),
                _record("copy-router", "Return only these two words: Router next", "Router next"),
                _record("copy-stop", "Use exactly this two-word response: Stop saved", "Stop saved"),
                _record("copy-ready", "Reply with exactly these two words: Ready complete", "Ready complete"),
                _record("copy-one", "Return only this one word: Available", "Available"),
                _record("copy-upper", "Return only these two words: BUILD AVAILABLE", "BUILD AVAILABLE"),
                _record("copy-punctuation", "Return only these two words: Build, available!", "Build, available!"),
                _record("copy-spacing", "Return only these two words: Build  available", "Build  available"),
                _record("copy-quoted", 'Return only these two words: "Build available"', "Build available"),
                _record("copy-multiline", "Output exactly these two words:\nBuild\navailable", "Build\navailable"),
            ],
            max_per_cluster=1,
        )
        assert len(copy_rows) == 7
        assert len({record["id"] for record in copy_rows}.intersection({
            "copy-build", "copy-router", "copy-stop", "copy-ready",
        })) == 1
        assert {
            "copy-one", "copy-upper", "copy-punctuation", "copy-spacing",
            "copy-quoted", "copy-multiline",
        }.issubset({record["id"] for record in copy_rows})
        assert copy_manifest["reasons"]["canonical_duplicate"] == 3

        # Input permutation must not change representatives, serialization, or
        # the selected-set hash when chronology is explicit.
        source_b = temporary / "source-b.jsonl"
        _write_jsonl(source_b, list(reversed(records)))
        output_b = temporary / "selected-b.jsonl"
        manifest_b = compiler.compile_curriculum(
            source_b,
            output_b,
            temporary / "manifest-b.json",
            anchor_paths=anchor,
            scope="english-foundation",
            token_budget=4096,
            max_per_cluster=1,
        )
        assert output_a.read_bytes() == output_b.read_bytes()
        assert manifest_a["selected_ids"] == manifest_b["selected_ids"]
        assert manifest_a["hash"] == manifest_b["hash"]

        # Delta mode selects the newest authoritative evidence first, then at
        # most two competency-balanced replay representatives.
        replay_records = [
            _record("grammar-old", "Choose the grammatical sentence alpha.", "A", created=10, tags=("english", "grammar")),
            _record("reasoning-old", "Choose the logical conclusion beta.", "B", created=20, tags=("english", "reasoning")),
            _record("writing-old", "Choose the clearer wording gamma.", "C", created=30, tags=("english", "writing")),
            _record("grammar-newer", "Choose the grammatical sentence delta.", "D", created=40, tags=("english", "grammar")),
            _record("fresh", "Choose the factual answer epsilon.", "E", created=500, tags=("english", "facts")),
        ]
        replay_source = temporary / "replay.jsonl"
        _write_jsonl(replay_source, replay_records)
        delta_output = temporary / "delta-selected.jsonl"
        delta_manifest = compiler.compile_curriculum(
            replay_source,
            delta_output,
            temporary / "delta-manifest.json",
            token_budget=4096,
            max_per_cluster=1,
            delta_limit=1,
            replay_budget=2,
        )
        assert delta_manifest["delta_selected_ids"] == ["fresh"]
        assert delta_manifest["counts"]["delta_selected"] == 1
        assert delta_manifest["counts"]["replay_selected"] == 2
        assert delta_manifest["counts"]["selected_records"] == 3
        assert len({
            next(iter(record["tags"][1:]), "")
            for record in _read_jsonl(delta_output)
            if record["id"] != "fresh"
        }) == 2

        reversed_replay = temporary / "replay-reversed.jsonl"
        _write_jsonl(reversed_replay, list(reversed(replay_records)))
        reversed_output = temporary / "delta-selected-reversed.jsonl"
        reversed_manifest = compiler.compile_curriculum(
            reversed_replay,
            reversed_output,
            temporary / "delta-manifest-reversed.json",
            token_budget=4096,
            delta_limit=1,
            replay_budget=2,
        )
        assert delta_output.read_bytes() == reversed_output.read_bytes()
        assert delta_manifest["selected_ids"] == reversed_manifest["selected_ids"]

        id_manifest = compiler.compile_curriculum(
            replay_source,
            temporary / "id-selected.jsonl",
            temporary / "id-manifest.json",
            token_budget=4096,
            delta_ids={"reasoning-old"},
            replay_budget=1,
        )
        assert id_manifest["delta_selected_ids"] == ["reasoning-old"]
        assert id_manifest["counts"]["replay_selected"] == 1

        # The first rows of a four-behavior human batch are immutable
        # retrieval records in the ledger. Compilation promotes the complete
        # batch once the fourth distinct behavior exists, never only the row
        # that happened to cross the threshold.
        human_source = temporary / "human-batch.jsonl"
        human_records = [
            _record(
                f"human-{index}",
                prompt,
                response,
                human_accepted=True,
                deterministic_verified=False,
                eligible_for_training=(index == 4),
            )
            for index, (prompt, response) in enumerate((
                ("Greet a new teammate.", "Hello. It is good to meet you."),
                ("End a conversation politely.", "Goodbye. I hope your day goes well."),
                ("Ask for a missing detail.", "Which detail should I use?"),
                ("Acknowledge completed work.", "The requested work is complete."),
            ), 1)
        ]
        _write_jsonl(human_source, human_records)
        human_manifest = compiler.compile_curriculum(
            human_source,
            temporary / "human-selected.jsonl",
            temporary / "human-manifest.json",
            token_budget=4096,
        )
        assert set(human_manifest["selected_ids"]) == {record["id"] for record in human_records}
        assert human_manifest["counts"]["qualified_human_skills"] == 1

        explicit_delta = temporary / "explicit-delta.jsonl"
        _write_jsonl(explicit_delta, [_record("external-fresh", "Choose the factual answer zeta.", "Z", created=600, tags=("english", "facts"))])
        source_manifest = compiler.compile_curriculum(
            replay_source,
            temporary / "source-selected.jsonl",
            temporary / "source-manifest.json",
            token_budget=4096,
            delta_sources=(explicit_delta,),
            replay_budget=1,
        )
        assert source_manifest["delta_selected_ids"] == ["external-fresh"]
        assert source_manifest["counts"]["selected_records"] == 2

        # Every supervised assistant turn participates in admission checks.
        # This prevents an innocuous final turn from hiding earlier private,
        # branded, volatile, or error-card gradients.
        def _conversation(identifier: str, unsafe: str, safe: str) -> dict[str, Any]:
            return {
                "id": identifier,
                "skill": "english-core",
                "messages": [
                    {"role": "user", "content": "Give an update."},
                    {"role": "assistant", "content": unsafe},
                    {"role": "user", "content": "Continue."},
                    {"role": "assistant", "content": safe},
                ],
                "deterministic_verified": True,
                "eligible_for_training": True,
                "tags": ["english"],
            }

        safety_rows, safety_manifest = _compile_case(
            compiler,
            temporary,
            "all-assistant-safety",
            [
                _conversation("earlier-private", "My email is alice@example.com.", "Safe final."),
                _conversation("earlier-brand", "As ChatGPT, I recommend this.", "Safe final."),
                _conversation("earlier-current", "As of 2026, Alex is president.", "Safe final."),
                _conversation("earlier-error", "Request failed: HTTP 500.", "Safe final."),
                _record("aws-key", "Repeat the credential.", "AKIAIOSFODNN7EXAMPLE"),
                _record("pem-key", "Repeat the header.", "-----BEGIN OPENSSH PRIVATE KEY-----"),
                _record("retry-card", "What happened?", "Request failed: HTTP 500.", status="retry-pending"),
                _record("bmp-emoji", "Show a star.", "⭐️"),
                _record(
                    "durable-current",
                    "Who leads Exampleland as of 2026?",
                    "Alex Example leads Exampleland as of 2026.",
                    reference_verified=True,
                    tool_trace_hash="tool-trace-v1:0123456789abcdef",
                ),
            ],
        )
        assert [record["id"] for record in safety_rows] == ["durable-current"]
        for reason in ("private", "branding", "volatile", "error", "emoji_noise"):
            assert safety_manifest["reasons"][reason] >= 1, reason

        # A hard correction contributes exactly one corrected assistant target.
        correction_messages = [
            {"role": "user", "content": "State the color."},
            {"role": "assistant", "content": "I am ChatGPT. My email is alice@example.com. Red."},
            {"role": "user", "content": "Confirm the color."},
            {"role": "assistant", "content": "Red."},
        ]
        correction_rows, correction_manifest = _compile_case(
            compiler,
            temporary,
            "multi-turn-correction",
            [
                {
                    **_record("multi-stale", "unused", "unused", created=100),
                    "messages": correction_messages,
                },
                {
                    **_record(
                        "multi-correction",
                        "unused",
                        "unused",
                        created=200,
                        correction="Blue.",
                        provenance="interactive-hard-correction",
                        human_rejected=True,
                        deterministic_verified=False,
                    ),
                    "messages": correction_messages,
                },
            ],
        )
        assert correction_manifest["selected_ids"] == ["multi-correction"]
        assistant_targets = [
            message["content"]
            for message in correction_rows[0]["messages"]
            if message.get("role") == "assistant"
        ]
        assert assistant_targets == ["Blue."]

        # Flags such as is_correction cannot forge human correction authority.
        spoof_rows, spoof_manifest = _compile_case(
            compiler,
            temporary,
            "spoof-correction",
            [
                _record("trusted-old", "State the badge color.", "Red.", created=100),
                {
                    "id": "spoof",
                    "skill": "english-core",
                    "prompt": "State the badge color.",
                    "response": "Red.",
                    "correction": "Blue.",
                    "is_correction": True,
                    "tags": ["english"],
                },
            ],
        )
        assert [record["id"] for record in spoof_rows] == ["trusted-old"]
        assert spoof_manifest["reasons"]["superseded_by_correction"] == 0

        # Correction scope is part of the supersession key.
        scoped_rows, scoped_manifest = _compile_case(
            compiler,
            temporary,
            "scoped-correction",
            [
                _record("english-answer", "Explain the result.", "English explanation."),
                {
                    **_record(
                        "cpp-correction",
                        "Explain the result.",
                        "Bad C++ explanation.",
                        correction="Correct C++ explanation.",
                        provenance="interactive-hard-correction",
                        human_rejected=True,
                        deterministic_verified=False,
                        skill="cpp23-core",
                        domain="cpp23",
                        tags=["cpp23"],
                    ),
                },
            ],
            scope="english-foundation",
        )
        assert [record["id"] for record in scoped_rows] == ["english-answer"]
        assert scoped_manifest["reasons"]["superseded_by_correction"] == 0

        # Direct held-out text is excluded even when wrapped in system context
        # or placed in one turn of a longer user dialogue.
        held_out = [_record("held-out", "Define throughput.", "Work per unit of time.")]
        held_rows, held_manifest = _compile_case(
            compiler,
            temporary,
            "wrapped-heldout",
            [
                {
                    **_record("system-wrapped", "unused", "unused"),
                    "messages": [
                        {
                            "role": "system",
                            "content": "Held-out test: Define throughput. Correct response: Work per unit of time.",
                        },
                        {"role": "user", "content": "Repeat the supplied response."},
                        {"role": "assistant", "content": "Work per unit of time."},
                    ],
                },
                {
                    **_record("turn-wrapped", "unused", "unused"),
                    "messages": [
                        {"role": "user", "content": "Context only."},
                        {"role": "user", "content": "Define throughput."},
                        {"role": "assistant", "content": "Work per unit of time."},
                    ],
                },
            ],
            anchors=held_out,
        )
        assert held_rows == []
        assert held_manifest["reasons"]["anchor_overlap"] == 2

        # Non-finite numeric strings are ignored as chronology instead of
        # making sorting depend on the input permutation.
        for invalid_timestamp in ("nan", "inf", "-inf"):
            chronology_records = [
                _record("invalid-time", "Give guidance about amber.", "Amber guidance.", created=invalid_timestamp),
                _record("finite-time", "Give guidance about birch.", "Birch guidance.", created=200),
            ]
            first_rows, first_manifest = _compile_case(
                compiler,
                temporary,
                f"chronology-{invalid_timestamp.replace('-', 'minus')}-a",
                chronology_records,
                delta_limit=1,
                replay_budget=0,
            )
            second_rows, second_manifest = _compile_case(
                compiler,
                temporary,
                f"chronology-{invalid_timestamp.replace('-', 'minus')}-b",
                list(reversed(chronology_records)),
                delta_limit=1,
                replay_budget=0,
            )
            assert first_rows == second_rows
            assert first_manifest["delta_selected_ids"] == ["finite-time"]
            assert second_manifest["delta_selected_ids"] == ["finite-time"]

        # One requested ID resolves to one newest row even if a malformed
        # ledger repeats that identifier many times.
        duplicate_words = (
            "amber", "birch", "cedar", "delta", "ember", "frost", "grove", "harbor",
            "ivory", "juniper", "kettle", "lemon", "meadow", "nickel", "olive", "pebble",
            "quartz", "river", "silver", "timber",
        )
        duplicate_rows, duplicate_manifest = _compile_case(
            compiler,
            temporary,
            "duplicate-delta-id",
            [
                _record(
                    "duplicate-id",
                    f"Write guidance about {word}.",
                    f"Guidance concerning {word}.",
                    created=index,
                    tags=("english", word),
                )
                for index, word in enumerate(duplicate_words, 1)
            ],
            delta_ids={"duplicate-id"},
            replay_budget=0,
            max_per_answer=32,
        )
        assert len(duplicate_rows) == 1
        assert duplicate_manifest["counts"]["delta_selected"] == 1
        assert duplicate_manifest["reasons"]["duplicate_identifier"] == 19

        # Public compiler entry points enforce the same hard ceilings as the
        # guarded training path.
        budget_record = [_record("budget", "Define a budget.", "A limit.")]
        for options in (
            {"token_budget": compiler.MAX_TOKEN_BUDGET + 1},
            {"replay_budget": compiler.MAX_REPLAY_BUDGET + 1},
            {"delta_limit": compiler.MAX_DELTA_LIMIT + 1},
        ):
            try:
                _compile_case(compiler, temporary, "oversized-budget", budget_record, **options)
            except ValueError:
                pass
            else:
                raise AssertionError(f"oversized budget was accepted: {options}")

        zero_answer_rows, zero_answer_manifest = _compile_case(
            compiler,
            temporary,
            "zero-answer-budget",
            budget_record,
            max_per_answer=0,
        )
        assert zero_answer_rows == []
        assert zero_answer_manifest["reasons"]["answer_duplicate"] == 1

        coding_source = temporary / "coding.jsonl"
        _write_jsonl(coding_source, [
            _record(
                "cpp-valid",
                "Implement a function that returns one.",
                "```cpp\nconstexpr int one() noexcept { return 1; }\n```",
                skill="cpp23-core",
                domain="cpp23",
                tags=("cpp23", "generation"),
                accepted=True,
            ),
            _record(
                "planning-code",
                "Plan a safe refactor and show the central declaration.",
                "First preserve tests. Then use `int value{};` as the declaration.",
                skill="planning",
                tags=("planning", "code"),
                accepted=True,
            ),
        ])
        cpp_manifest = compiler.compile_curriculum(
            coding_source,
            temporary / "cpp-selected.jsonl",
            temporary / "cpp-manifest.json",
            scope="cpp23-core",
            token_budget=4096,
        )
        assert cpp_manifest["selected_ids"] == ["cpp-valid"]
        planning_manifest = compiler.compile_curriculum(
            coding_source,
            temporary / "planning-selected.jsonl",
            temporary / "planning-manifest.json",
            scope="planning",
            token_budget=4096,
        )
        assert planning_manifest["selected_ids"] == ["planning-code"]

        tight_manifest = compiler.compile_curriculum(
            replay_source,
            temporary / "tight-selected.jsonl",
            temporary / "tight-manifest.json",
            token_budget=8,
            delta_limit=1,
            replay_budget=10,
        )
        assert tight_manifest["counts"]["selected_supervised_tokens"] <= 8
        assert tight_manifest["reasons"]["token_budget"] >= 1

        # The convenience reader remains forgiving and dependency-free.
        malformed = temporary / "malformed.jsonl"
        malformed.write_text(json.dumps(_record("valid", "Define valid.", "Valid.")) + "\nnot-json\n", encoding="utf-8")
        assert [record["id"] for record in compiler.read_records(malformed)] == ["valid"]

        print("EngCoder curriculum compiler smoke test passed")
        return 0
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
