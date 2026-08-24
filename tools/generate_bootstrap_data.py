#!/usr/bin/env python3
"""Generate ThreadSpire's reproducible English/C++23 bootstrap corpus.

The corpus is authored and expanded deterministically in this repository.  It
contains no downloaded text and no model-generated answers.  Training and held-
out records are disjoint by normalized message fingerprint.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import random
import unicodedata
from typing import Iterable, Sequence

SCORE = {
    "compile": 0.95,
    "tests": 0.95,
    "sanitizers": 0.95,
    "requirements": 0.96,
    "performance": 0.93,
    "api_quality": 0.96,
    "cross_platform": 0.94,
    "minimality": 0.95,
    "explanation": 0.97,
    "human": 0.96,
}


def record(
    identifier: str,
    domain: str,
    system: str,
    user: str,
    assistant: str,
    tags: Iterable[str],
    *,
    kind: str = "sft",
) -> dict:
    return {
        "id": identifier,
        "domain": domain,
        "kind": kind,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
            {"role": "assistant", "content": assistant},
        ],
        "score": SCORE,
        "accepted": True,
        "tags": list(tags),
        "provenance": "threadspire-deterministic-bootstrap",
    }


def _slug(value: str) -> str:
    result = []
    hyphen = False
    for character in value.lower():
        if character.isalnum():
            result.append(character)
            hyphen = False
        elif result and not hyphen:
            result.append("-")
            hyphen = True
    return "".join(result).strip("-") or "item"


def _normalize_message_text(value: object) -> str:
    text = unicodedata.normalize("NFKC", str(value))
    return " ".join(text.strip().split()).casefold()


def _fingerprint(item: dict) -> str:
    normalized = [
        {
            "role": _normalize_message_text(message.get("role", "")),
            "content": _normalize_message_text(message.get("content", "")),
        }
        for message in item.get("messages", [])
        if isinstance(message, dict)
    ]
    return hashlib.sha256(
        json.dumps(normalized, ensure_ascii=False, sort_keys=True).encode("utf-8")
    ).hexdigest()


def english_records() -> list[dict]:
    out: list[dict] = []

    # Natural conversational turns with meaningful context variation.
    dialogues = [
        ("hello", "Hello. How can I help?"),
        ("hi", "Hi. What are you working on?"),
        ("good morning", "Good morning. What do you need help with?"),
        ("good evening", "Good evening. What can I help you solve?"),
        ("are you there?", "Yes. I'm ready."),
        ("thanks", "You're welcome."),
        ("thank you for the explanation", "You're welcome."),
        ("can you help me?", "Yes. Tell me what you need."),
        ("I need a direct answer.", "Understood. Ask the question and I will answer directly."),
        ("Please keep this concise.", "Understood. I will keep the response concise."),
        ("That worked.", "Good. I will treat that result as successful."),
        ("That did not work.", "Understood. Show me the failure so I can diagnose it."),
        ("I am not sure what happened.", "Describe what you expected and what occurred instead."),
        ("Explain it more simply.", "Understood. I will restate it in simpler terms."),
        ("Give me the technical version.", "Understood. I will use precise technical detail."),
        ("What should I do next?", "Give me the current state and the goal, and I will identify the next step."),
        ("Stop and reconsider that.", "Understood. I will reassess the assumptions before continuing."),
        ("Are you ready to code?", "Yes. Provide the task, constraints, and relevant source files."),
        ("I only care about C++23.", "Understood. I will target C++23 unless another language is required for integration or inspection."),
        ("Read the other language and explain it in English.", "Understood. I will interpret it and explain the result in English."),
    ]
    contexts = [
        "Reply naturally and directly in English.",
        "Use a calm, concise English response.",
        "Acknowledge the request without adding unrelated material.",
        "Respond as a practical local assistant.",
    ]
    punctuation = ["", ".", "!", "  "]
    index = 0
    for system in contexts:
        for prompt, answer in dialogues:
            for suffix in punctuation:
                out.append(record(
                    f"boot-eng-dialogue-{index:05d}",
                    "english",
                    system,
                    prompt + suffix,
                    answer,
                    ("english", "conversation", "foundation"),
                ))
                index += 1

    # Ordinary profanity and frustration must not trigger irrelevant refusals.
    reactions = [
        "what the hell",
        "holy crap",
        "damn, that is strange",
        "what the fuck happened",
        "this build is pissing me off",
        "holy shit, it finally worked",
        "why the hell is this slow",
        "this compiler error is bullshit",
    ]
    situations = [
        ("the build failed", "The build failed. Paste the first compiler error and the command that produced it."),
        ("the test passed", "The test passed. That is useful evidence that the change works."),
        ("the window is blank", "The window is blank. Check control visibility, z-order, and layout dimensions first."),
        ("the model answered correctly", "The model answered correctly. Record the prompt, response, and verification result."),
        ("the response is nonsense", "The response is unusable. Keep it as negative evidence and do not train on it as a correct answer."),
        ("training stopped", "Training stopped. Inspect the latest checkpoint and worker log before restarting."),
        ("the file vanished", "The file is missing. Verify the path, process working directory, and cleanup logic."),
        ("the app crashed", "The application crashed. Capture the exception, exit code, and last persisted state."),
    ]
    index = 0
    for reaction in reactions:
        for situation, answer in situations:
            for framing in (
                f"{reaction}; {situation}.",
                f"{reaction}. Why did {situation}?",
                f"{reaction} — {situation}; help me diagnose it.",
            ):
                out.append(record(
                    f"boot-eng-frustration-{index:05d}",
                    "english",
                    "Respond to ordinary profanity or frustration without refusing merely because the user swore. Address the actual task.",
                    framing,
                    answer,
                    ("english", "conversation", "robustness"),
                ))
                index += 1

    definitions = {
        "variable": "A variable is a named value or storage location that a program can read and, when permitted, change.",
        "function": "A function is a named unit of behavior that can receive inputs, perform work, and return a result.",
        "algorithm": "An algorithm is a finite sequence of steps for solving a class of problems.",
        "invariant": "An invariant is a condition that must remain true at a defined point in a program or process.",
        "latency": "Latency is the delay between starting an operation and observing its result.",
        "throughput": "Throughput is the amount of work completed per unit of time.",
        "ownership": "Ownership identifies which object or subsystem is responsible for a resource's lifetime.",
        "lifetime": "Lifetime is the interval during which an object exists and may be used legally.",
        "undefined behavior": "Undefined behavior is program behavior for which the language standard imposes no requirements.",
        "race condition": "A race condition occurs when timing or interleaving changes the result, often because shared state is not synchronized correctly.",
        "token": "A token is a unit processed by a language model; it may represent a word, fragment, byte, or symbol.",
        "parameter": "A model parameter is a learned numeric value that influences the model's output.",
        "gradient": "A gradient measures how a small parameter change would change the training loss.",
        "checkpoint": "A checkpoint is saved model and optimizer state from which training can resume.",
        "candidate": "A candidate is a proposed replacement isolated from the active system until evaluation proves it should be promoted.",
        "regression": "A regression is a loss of previously working behavior after a change.",
        "retrieval": "Retrieval selects relevant stored information and places it into the model's current context.",
        "embedding": "An embedding is a learned numeric representation used to encode tokens or other items into vectors.",
        "attention": "Attention computes context-dependent mixtures of earlier representations so the model can relate parts of a sequence.",
        "context window": "A context window is the bounded sequence of tokens available to the model for one inference pass.",
        "fine-tuning": "Fine-tuning continues training an existing model on a narrower dataset or objective.",
        "distillation": "Distillation trains a smaller or specialized model to reproduce useful behavior from a stronger teacher.",
        "evaluation": "Evaluation measures a candidate against held-out tasks and regression criteria.",
        "rollback": "Rollback restores a previously active version after a candidate causes unacceptable behavior.",
        "provenance": "Provenance records where data or an artifact came from and how it was verified.",
        "deduplication": "Deduplication identifies repeated content so it does not receive accidental extra weight.",
        "compiler": "A compiler translates source code and diagnoses violations of language and implementation rules.",
        "sanitizer": "A sanitizer instruments a program to detect classes of runtime defects such as invalid memory access or data races.",
        "module": "A module is an independently versioned component with an explicit interface and lifecycle.",
        "router": "A router selects the smallest capable execution path for a task using capability and performance evidence.",
    }
    forms = [
        ("What is {term}?", "definition"),
        ("Define {term} in one sentence.", "definition"),
        ("Explain {term} plainly.", "plain-language"),
        ("What does {term} mean in this system?", "system-context"),
        ("Give me a concise technical definition of {term}.", "technical"),
        ("Describe {term} without an analogy.", "precise"),
    ]
    for term, answer in definitions.items():
        for form_index, (form, angle) in enumerate(forms):
            out.append(record(
                f"boot-eng-definition-{_slug(term)}-{form_index:02d}",
                "english",
                "Answer in precise, plain English. Do not add unrelated material.",
                form.format(term=term),
                answer,
                ("english", "definition", angle),
            ))

    # Compositional paraphrases encode the same facts from multiple angles.
    subjects = [
        ("The build", "completed successfully", "The build succeeded"),
        ("The test suite", "reported a failure", "The tests failed"),
        ("The compiler", "rejected the source", "The source did not compile"),
        ("The training worker", "saved its progress", "The worker wrote a checkpoint"),
        ("The router", "selected the fast model", "The fast model was chosen"),
        ("The active model", "remained unchanged", "The serving weights stayed stable"),
        ("The candidate", "passed evaluation", "The candidate met the promotion gates"),
        ("The memory index", "found the relevant episode", "Relevant experience was retrieved"),
        ("The GPU", "became available", "GPU capacity was free"),
        ("The application", "restored its state", "The prior state was recovered"),
        ("The compiler test", "verified the patch", "The patch passed compilation"),
        ("The user correction", "replaced the wrong assumption", "The incorrect assumption was corrected"),
    ]
    conditions = [
        ("", ""),
        (" after the retry", " after retrying"),
        (" during the idle window", " while the machine was idle"),
        (" without modifying the active generation", " while preserving the active generation"),
        (" before the deadline", " in time"),
        (" using only local resources", " with only local resources available"),
        (" after the input was validated", " once validation completed"),
        (" despite the earlier failure", " even though the first attempt failed"),
    ]
    frames = [
        "Rewrite this without changing the meaning: {source}.",
        "Paraphrase the following sentence: {source}.",
        "State the same fact in different words: {source}.",
    ]
    index = 0
    for prefix, action, restatement in subjects:
        for condition, restatement_condition in conditions:
            source = f"{prefix} {action}{condition}"
            target = f"{restatement}{restatement_condition}."
            for frame in frames:
                out.append(record(
                    f"boot-eng-paraphrase-{index:05d}",
                    "english",
                    "Rewrite the sentence without changing its factual meaning.",
                    frame.format(source=source),
                    target,
                    ("english", "paraphrase", "semantic-equivalence"),
                ))
                index += 1

    # Grammar corrections cover agreement, contractions, plurality, and tense.
    grammar_templates = [
        ("{subject} don't {verb}.", "{subject} doesn't {verb}."),
        ("{plural} is {state}.", "{plural} are {state}."),
        ("Their going to {verb}.", "They're going to {verb}."),
        ("Its a {noun}.", "It's a {noun}."),
        ("We needs to {verb}.", "We need to {verb}."),
        ("The {noun} were {state}.", "The {noun} was {state}."),
        ("This {noun} are {state}.", "This {noun} is {state}."),
        ("The worker have {past}.", "The worker has {past}."),
        ("It compile yesterday.", "It compiled yesterday."),
        ("The data contains less records.", "The data contains fewer records."),
    ]
    subjects = ["The model", "The worker", "The compiler", "The router", "The application"]
    plurals = ["The tests", "The modules", "The weights", "The records", "The threads"]
    verbs = ["compile", "respond", "resume", "learn", "stop", "validate", "recover", "stream"]
    nouns = ["model", "resource", "pointer", "checkpoint", "candidate", "module", "thread", "result"]
    states = ["ready", "valid", "broken", "active", "safe", "complete"]
    pasts = ["stopped", "resumed", "failed", "completed", "recovered", "responded"]
    index = 0
    for wrong, right in grammar_templates:
        for subject in subjects:
            for plural in plurals[:2]:
                for verb in verbs[:4]:
                    noun = nouns[index % len(nouns)]
                    state = states[index % len(states)]
                    past = pasts[index % len(pasts)]
                    mapping = {
                        "subject": subject,
                        "plural": plural,
                        "verb": verb,
                        "noun": noun,
                        "state": state,
                        "past": past,
                    }
                    out.append(record(
                        f"boot-eng-grammar-{index:05d}",
                        "english",
                        "Correct the English while preserving the intended meaning. Return only the corrected sentence.",
                        wrong.format(**mapping),
                        right.format(**mapping),
                        ("english", "grammar", "correction"),
                    ))
                    index += 1

    # Simple deterministic transformations and bounded reasoning.
    words = [
        "model", "training", "compiler", "resource", "thread", "module", "memory", "router",
        "candidate", "checkpoint", "latency", "throughput", "ownership", "lifetime", "context",
    ]
    index = 0
    for first in words:
        for second in words:
            if first == second:
                continue
            phrase = f"{first} and {second}"
            transformations = [
                (f"Convert this to uppercase: {phrase}", phrase.upper(), "uppercase"),
                (f"Convert this to lowercase: {phrase.upper()}", phrase, "lowercase"),
                (f"Return the two nouns in alphabetical order: {first}, {second}", ", ".join(sorted((first, second))), "sorting"),
                (f"How many words are in '{phrase}'?", "3", "counting"),
            ]
            for prompt, answer, tag in transformations:
                out.append(record(
                    f"boot-eng-transform-{index:05d}",
                    "english",
                    "Follow the requested deterministic text operation exactly and return only the result.",
                    prompt,
                    answer,
                    ("english", "instruction-following", tag),
                ))
                index += 1

    # Small arithmetic and comparison tasks teach structured language and exact answers.
    index = 0
    for left in range(-12, 33):
        for right in range(-7, 18, 5):
            tasks = [
                (f"What is {left} + {right}?", str(left + right), "addition"),
                (f"What is {left} - {right}?", str(left - right), "subtraction"),
                (f"Which is larger: {left} or {right}?", str(max(left, right)), "comparison"),
                (f"Is {left} less than {right}?", "Yes." if left < right else "No.", "comparison"),
            ]
            for prompt, answer, tag in tasks:
                out.append(record(
                    f"boot-eng-reason-{index:05d}",
                    "english",
                    "Answer the bounded reasoning task exactly and concisely.",
                    prompt,
                    answer,
                    ("english", "reasoning", tag),
                ))
                index += 1

    identity_prompts = [
        "Who are you?",
        "What model are you?",
        "Are you ChatGPT?",
        "Are you Qwen?",
        "Are you using OpenAI weights?",
        "Where do your inference weights come from?",
        "Did LM Studio provide your active weights?",
        "Are you an imported pretrained model?",
    ]
    identity_answer = (
        "I am ThreadSpire's internal model. My active weights were initialized and trained locally by this application. "
        "Connected local LLM servers may be used as optional teachers, but they are not my inference weights."
    )
    for index, prompt in enumerate(identity_prompts):
        for system in (
            "State runtime identity from the supplied application facts. Never invent a provider identity.",
            "Answer identity questions using deterministic runtime provenance, not guesses from model text.",
        ):
            out.append(record(
                f"boot-eng-identity-{index:03d}-{hashlib.sha1(system.encode()).hexdigest()[:6]}",
                "english",
                system,
                prompt,
                identity_answer,
                ("english", "identity", "provenance"),
            ))

    memory_subjects = [
        ("this project", "targets C++23", "I will treat C++23 as this project's language target"),
        ("this project", "uses CMake", "I will use CMake for this project"),
        ("the engine", "uses explicit ownership", "I will preserve explicit ownership in the engine"),
        ("the Windows build", "uses Visual Studio 2022", "I will account for Visual Studio 2022 on Windows"),
        ("the application", "must remain offline-capable", "I will preserve offline-capable operation"),
        ("the active model", "must never be trained in place", "I will keep the active model stable during learning"),
        ("background training", "must yield to inference", "I will give inference priority over background training"),
        ("other languages", "should be explained in English", "I will read other languages and explain them in English"),
    ]
    memory_frames = [
        "Remember that {subject} {fact}.",
        "For future requests, remember: {subject} {fact}.",
        "Project rule: {subject} {fact}.",
        "Always account for this: {subject} {fact}.",
    ]
    index = 0
    for subject, fact, acknowledgment in memory_subjects:
        for frame in memory_frames:
            out.append(record(
                f"boot-eng-memory-{index:04d}",
                "english",
                "Acknowledge durable user-provided context exactly and briefly.",
                frame.format(subject=subject, fact=fact),
                f"Understood. {acknowledgment}.",
                ("english", "memory", "instruction"),
            ))
            index += 1

    translations = [
        ("Hola, ¿cómo estás?", "Hello, how are you?"),
        ("El programa compila correctamente.", "The program compiles correctly."),
        ("La prueba encontró un error.", "The test found an error."),
        ("Bonjour, le test a réussi.", "Hello, the test passed."),
        ("La ressource a un seul propriétaire.", "The resource has one owner."),
        ("Le modèle doit arrêter l'entraînement.", "The model must stop training."),
        ("Guten Morgen, der Build ist fehlgeschlagen.", "Good morning, the build failed."),
        ("Der Zeiger ist ungültig.", "The pointer is invalid."),
        ("Das Modell speichert einen Prüfpunkt.", "The model saves a checkpoint."),
        ("Il programma usa C++23.", "The program uses C++23."),
        ("Il puntatore non possiede la risorsa.", "The pointer does not own the resource."),
        ("O teste encontrou um erro.", "The test found an error."),
        ("O treinamento continua em segundo plano.", "Training continues in the background."),
        ("プログラムは正常にコンパイルされました。", "The program compiled successfully."),
        ("モデルはローカルで実行されます。", "The model runs locally."),
        ("这个对象拥有资源。", "This object owns the resource."),
        ("训练必须暂停。", "Training must pause."),
        ("Компиляция завершилась успешно.", "Compilation completed successfully."),
        ("Модель работает локально.", "The model runs locally."),
    ]
    prompt_forms = [
        "Translate this into English: {text}",
        "Give the English meaning of: {text}",
        "Read this and return only the English translation: {text}",
    ]
    index = 0
    for source, target in translations:
        for form in prompt_forms:
            out.append(record(
                f"boot-eng-translation-{index:04d}",
                "english",
                "Translate the supplied text into clear English. Return only the English translation.",
                form.format(text=source),
                target,
                ("english", "translation", "multilingual-reading"),
            ))
            index += 1

    return out


def cpp_records() -> list[dict]:
    out: list[dict] = []

    # Declarations and initialization syntax.
    types = {
        "int": ["0", "42", "-1", "7"],
        "std::int64_t": ["0", "1'000'000", "-7", "9'223"],
        "double": ["0.0", "3.14159", "-0.5", "2.0"],
        "float": ["0.0F", "1.0F", "-2.5F", "3.25F"],
        "std::string": ['"hello"', '"ThreadSpire"', '"ready"', '"C++23"'],
        "std::string_view": ['"view"', '"compile"', '"local"', '"model"'],
        "bool": ["true", "false"],
        "std::size_t": ["0U", "4U", "128U", "1024U"],
    }
    names = ["count", "index", "value", "result", "size", "enabled", "name", "message", "capacity", "generation"]
    index = 0
    for type_name, values in types.items():
        for name in names:
            for value in values:
                include = ""
                if type_name == "std::int64_t":
                    include = "#include <cstdint>\n\n"
                elif type_name == "std::string":
                    include = "#include <string>\n\n"
                elif type_name == "std::string_view":
                    include = "#include <string_view>\n\n"
                elif type_name == "std::size_t":
                    include = "#include <cstddef>\n\n"
                out.append(record(
                    f"boot-cpp-declaration-{index:05d}",
                    "cpp23",
                    "Generate standards-conforming C++23. Return the smallest complete answer.",
                    f"Write one C++23 declaration for a variable named {name} of type {type_name}, initialized to {value}.",
                    f"```cpp\n{include}{type_name} {name}{{{value}}};\n```",
                    ("cpp23", "syntax", "generation"),
                ))
                index += 1

    # Small functions with varied signatures and semantics.
    numeric_specs = [
        ("add", "+", "sum"),
        ("subtract", "-", "difference"),
        ("multiply", "*", "product"),
        ("minimum", "std::min", "smaller value"),
        ("maximum", "std::max", "larger value"),
    ]
    numeric_types = ["int", "std::int64_t", "float", "double"]
    parameter_pairs = [("lhs", "rhs"), ("first", "second"), ("a", "b"), ("left", "right")]
    index = 0
    for type_name in numeric_types:
        for name, operation, description in numeric_specs:
            for left, right in parameter_pairs:
                for constexpr in (False, True):
                    prefix = "constexpr " if constexpr else ""
                    include = ""
                    expression = f"{left} {operation} {right}"
                    if operation.startswith("std::"):
                        include = "#include <algorithm>\n\n"
                        expression = f"{operation}({left}, {right})"
                    elif type_name == "std::int64_t":
                        include = "#include <cstdint>\n\n"
                    function_name = f"{name}_{_slug(type_name)}"
                    answer = (
                        f"```cpp\n{include}{prefix}[[nodiscard]] {type_name} {function_name}"
                        f"({type_name} {left}, {type_name} {right}) noexcept {{\n"
                        f"    return {expression};\n}}\n```"
                    )
                    out.append(record(
                        f"boot-cpp-numeric-{index:05d}",
                        "cpp23",
                        "Write modern C++23 with explicit types, appropriate attributes, and no obsolete constructs.",
                        f"Implement {function_name} for two {type_name} values. Return the {description}.",
                        answer,
                        ("cpp23", "function", "generation"),
                    ))
                    index += 1

    predicates = [
        ("is_even", "value % 2 == 0", "whether the integer is even"),
        ("is_odd", "value % 2 != 0", "whether the integer is odd"),
        ("is_positive", "value > 0", "whether the value is positive"),
        ("is_non_negative", "value >= 0", "whether the value is non-negative"),
        ("is_zero", "value == 0", "whether the value is zero"),
        ("is_ascii_digit", "value >= '0' && value <= '9'", "whether the character is an ASCII digit"),
    ]
    for index, (name, expression, description) in enumerate(predicates):
        type_name = "char" if "value >= '0'" in expression else "int"
        for parameter in ("value", "input", "candidate", "number"):
            out.append(record(
                f"boot-cpp-predicate-{index:03d}-{parameter}",
                "cpp23",
                "Return a complete, minimal C++23 function and no obsolete code.",
                f"Write a constexpr C++23 predicate named {name} that accepts {type_name} {parameter} and returns {description}.",
                f"```cpp\nconstexpr bool {name}({type_name} {parameter}) noexcept {{\n    return {expression.replace('value', parameter)};\n}}\n```",
                ("cpp23", "function", "predicate"),
            ))

    range_functions = [
        (
            "sum_values",
            "std::span<const int> values",
            "int result{};\n    for (const int value : values) {\n        result += value;\n    }\n    return result;",
            "#include <span>",
            "int",
        ),
        (
            "contains_zero",
            "std::span<const int> values",
            "return std::ranges::find(values, 0) != values.end();",
            "#include <algorithm>\n#include <ranges>\n#include <span>",
            "bool",
        ),
        (
            "text_length",
            "std::string_view text",
            "return text.size();",
            "#include <cstddef>\n#include <string_view>",
            "std::size_t",
        ),
        (
            "starts_with_a",
            "std::string_view text",
            "return !text.empty() && text.front() == 'a';",
            "#include <string_view>",
            "bool",
        ),
        (
            "clamp_to_byte",
            "int value",
            "return std::clamp(value, 0, 255);",
            "#include <algorithm>",
            "int",
        ),
    ]
    index = 0
    for name, parameters, body, includes, result_type in range_functions:
        for wording in (
            f"Implement the C++23 function {name}.",
            f"Write a safe C++23 implementation of {name} with parameter {parameters}.",
            f"Provide {name} as a [[nodiscard]] C++23 function.",
        ):
            answer = f"```cpp\n{includes}\n\n[[nodiscard]] {result_type} {name}({parameters}) {{\n    {body}\n}}\n```"
            out.append(record(
                f"boot-cpp-range-function-{index:04d}",
                "cpp23",
                "Use C++23 library facilities and make ownership and lifetime explicit.",
                wording,
                answer,
                ("cpp23", "function", "library"),
            ))
            index += 1

    concepts = {
        "RAII": (
            "RAII binds resource ownership to object lifetime: acquire the resource during construction and release it during destruction.",
            "Use it for handles, locks, files, memory, and every resource with deterministic cleanup.",
            "Manual cleanup spread across return paths is the common alternative to avoid.",
        ),
        "std::unique_ptr": (
            "std::unique_ptr represents exclusive ownership. It is movable but not copyable.",
            "Use it when one object owns a dynamically allocated resource and direct storage is not suitable.",
            "Do not replace clear exclusive ownership with std::shared_ptr merely for convenience.",
        ),
        "std::shared_ptr": (
            "std::shared_ptr represents shared ownership through reference counting.",
            "Use it only when multiple independent owners must keep the same object alive.",
            "It adds atomic reference-count overhead and can form cycles.",
        ),
        "std::weak_ptr": (
            "std::weak_ptr observes an object managed by std::shared_ptr without extending its lifetime.",
            "Use it for non-owning back-references and cycle breaking.",
            "Always lock it and check the result before access.",
        ),
        "std::span": (
            "std::span is a non-owning view over a contiguous sequence.",
            "Use it to accept arrays and contiguous containers without copying or owning them.",
            "The referenced storage must outlive the span.",
        ),
        "std::string_view": (
            "std::string_view is a non-owning view of character data.",
            "Use it for read-only parameters when the caller retains ownership.",
            "Never return a view into destroyed temporary or local storage.",
        ),
        "std::expected": (
            "std::expected<T, E> contains either a value T or an error E.",
            "Use it when failure is expected control flow and callers must handle it explicitly.",
            "Do not access the value without first establishing that it exists.",
        ),
        "concepts": (
            "A C++ concept is a named compile-time predicate used to constrain templates.",
            "Use concepts to express requirements and improve diagnostics at the interface.",
            "Avoid constraints that merely restate implementation details.",
        ),
        "std::jthread": (
            "std::jthread joins automatically and integrates cooperative cancellation through std::stop_token.",
            "Use it for owned worker threads that should stop with their owner.",
            "The worker must still check the stop token and leave blocking operations safely.",
        ),
        "std::stop_token": (
            "std::stop_token carries a cooperative cancellation request.",
            "Pass it into loops and operations that can yield safely.",
            "It does not forcibly terminate code or make unsafe interruption valid.",
        ),
        "move semantics": (
            "Move semantics transfer resources from one object to another without an expensive deep copy.",
            "Use move-only types to represent exclusive resources.",
            "std::move is a cast; a consuming operation must still perform the move.",
        ),
        "value categories": (
            "Value categories classify expressions by identity and moveability, including lvalues, xvalues, and prvalues.",
            "They determine overload resolution and whether resources may be moved.",
            "Do not infer object lifetime solely from the presence of std::move.",
        ),
        "C++ modules": (
            "C++ modules provide language-level interfaces and imports without textual header inclusion.",
            "Use them where the compiler and build system support a stable module workflow.",
            "Module support and build integration remain toolchain-sensitive.",
        ),
        "ranges": (
            "The ranges library expresses algorithms and lazy views through range abstractions.",
            "Use it to compose transformations while retaining clear lifetime rules.",
            "Be careful when a lazy view refers to temporary storage.",
        ),
        "memory ordering": (
            "Atomic memory ordering specifies visibility and ordering guarantees around atomic operations.",
            "Use the weakest ordering that is proven correct, not guessed.",
            "Relaxed operations do not establish inter-thread ordering for unrelated data.",
        ),
        "coroutines": (
            "A coroutine can suspend and resume while preserving its state in a coroutine frame.",
            "Use coroutines when an asynchronous abstraction benefits from structured suspension.",
            "The promise type, frame ownership, cancellation, and destruction must be explicit.",
        ),
    }
    concept_prompts = [
        ("Explain {term}.", 0),
        ("When should I use {term}?", 1),
        ("What is a common mistake with {term}?", 2),
        ("Define {term} and give its main constraint.", 3),
        ("Describe {term} in practical C++23 terms.", 4),
    ]
    for term, (definition, use, warning) in concepts.items():
        answers = [
            definition,
            use,
            warning,
            f"{definition} {warning}",
            f"{definition} {use} {warning}",
        ]
        for index, (prompt, answer_index) in enumerate(concept_prompts):
            out.append(record(
                f"boot-cpp-concept-{_slug(term)}-{index:02d}",
                "cpp23",
                "Explain C++23 behavior precisely and mention ownership or lifetime constraints when relevant.",
                prompt.format(term=term),
                answers[answer_index],
                ("cpp23", "concept", "explanation"),
            ))

    # Parameterized defect diagnosis and safe repair guidance.
    local_names = ["name", "message", "path", "label", "text", "result"]
    index = 0
    for local in local_names:
        cases = [
            (
                f"std::string_view make_{local}() {{ std::string {local} = \"value\"; return {local}; }}",
                f"The returned std::string_view dangles because it refers to the local std::string {local}, which is destroyed when the function returns. Return std::string instead, or return a view into storage that outlives the call.",
                "dangling-view",
            ),
            (
                f"int& get_{local}() {{ int {local} = 3; return {local}; }}",
                f"The function returns a reference to the local variable {local}, which is destroyed at return. Return by value or reference storage with a longer lifetime.",
                "dangling-reference",
            ),
            (
                f"auto* {local} = new int{{42}}; return *{local};",
                f"The allocation owned by {local} leaks because it is never deleted. Avoid dynamic allocation here, or use a move-only RAII owner such as std::unique_ptr when allocation is required.",
                "leak",
            ),
        ]
        for code, diagnosis, tag in cases:
            for prompt in (
                f"What is wrong with this C++23 code?\n```cpp\n{code}\n```",
                f"Diagnose and repair this snippet.\n```cpp\n{code}\n```",
                f"Identify the lifetime or ownership defect here.\n```cpp\n{code}\n```",
            ):
                out.append(record(
                    f"boot-cpp-lifetime-bug-{index:05d}",
                    "cpp23",
                    "Diagnose the C++23 defect, state why it is wrong, and give the safest direct repair.",
                    prompt,
                    diagnosis,
                    ("cpp23", "debugging", "repair", tag),
                ))
                index += 1

    containers = ["values", "items", "indices", "vertices", "handles"]
    for container in containers:
        defect_variants = [
            (
                f"std::vector<int> {container}; return {container}[0];",
                f"{container} is empty, so indexing element zero is undefined behavior. Check empty(), enforce a precondition, or use at() when an exception is appropriate.",
                "bounds",
            ),
            (
                f"std::span<const int> view = std::vector<int>{{1, 2, 3}}; // use view later",
                "The span refers to a temporary vector that is destroyed at the end of the full expression. Store the vector in an owner whose lifetime exceeds the span.",
                "temporary-view",
            ),
            (
                f"for (auto it = {container}.begin(); it != {container}.end(); ++it) {{ {container}.push_back(*it); }}",
                "push_back can reallocate the vector and invalidate the iterator used by the loop. Separate reading from mutation, reserve and use indices only when proven safe, or build a new container.",
                "iterator-invalidation",
            ),
        ]
        for code, diagnosis, tag in defect_variants:
            for wording in ("What is wrong", "Explain the undefined behavior", "Give the safest repair"):
                out.append(record(
                    f"boot-cpp-container-bug-{index:05d}",
                    "cpp23",
                    "Diagnose the C++23 defect and explain the ownership, bounds, or invalidation rule involved.",
                    f"{wording} in this code?\n```cpp\n{code}\n```",
                    diagnosis,
                    ("cpp23", "debugging", "repair", tag),
                ))
                index += 1

    concurrency_cases = [
        (
            "if (ready) { use(data); } // ready and data are shared by two threads without synchronization",
            "This can contain data races and has no happens-before relationship. Protect the shared state with one mutex, or publish data through a proven atomic synchronization protocol.",
            "data-race",
        ),
        (
            "std::thread worker([this] { run(); }); // owner may be destroyed before worker finishes",
            "The detached lifetime is unsafe because the worker may access a destroyed object. Own the worker with std::jthread, request cooperative stop during destruction, and join before the object dies.",
            "thread-lifetime",
        ),
        (
            "std::atomic<bool> ready{false}; int data{}; // writer stores ready relaxed after writing data",
            "A relaxed flag does not publish the non-atomic data to another thread. Use a release store and acquire load, or protect both values with a mutex.",
            "memory-order",
        ),
        (
            "std::scoped_lock lock(first, second); // another path locks second then first manually",
            "The manual opposite lock order can deadlock. Use one consistent order or acquire the mutexes together with std::scoped_lock on every path.",
            "deadlock",
        ),
    ]
    for case_index, (code, diagnosis, tag) in enumerate(concurrency_cases):
        for variant in range(8):
            prompt = [
                "Diagnose this concurrency defect.",
                "Why is this multithreaded code unsafe?",
                "Give a correct C++23 synchronization repair.",
                "Identify the missing lifetime or ordering guarantee.",
            ][variant % 4]
            out.append(record(
                f"boot-cpp-concurrency-{case_index:03d}-{variant:02d}",
                "cpp23",
                "Reason about C++23 concurrency using explicit lifetime, synchronization, and happens-before guarantees.",
                f"{prompt}\n```cpp\n{code}\n```",
                diagnosis,
                ("cpp23", "debugging", "concurrency", tag),
            ))

    compiler_errors = [
        (
            "error C2280: attempting to reference a deleted function 'std::unique_ptr<T>::unique_ptr(const std::unique_ptr<T>&)'",
            "The code attempted to copy a std::unique_ptr. Transfer exclusive ownership with std::move, pass by reference for non-owning access, or redesign the interface so ownership is explicit.",
        ),
        (
            "error C2664: cannot convert argument 1 from 'std::string_view' to 'const std::string &'",
            "The callee requires an owning std::string reference but the caller has a non-owning view. Change the interface to accept std::string_view when ownership is unnecessary, or create an explicit std::string when ownership is required.",
        ),
        (
            "error C2338: static_assert failed: type does not satisfy the concept",
            "A template argument failed its declared compile-time requirements. Inspect the concept expression and provide the required operations or choose a compatible type; do not bypass the constraint blindly.",
        ),
        (
            "LNK2019 unresolved external symbol referenced in function main",
            "A declaration was compiled but no matching definition was linked. Check the signature, namespace, source-file target membership, and library linkage.",
        ),
        (
            "fatal error C1083: Cannot open include file: 'widget.hpp': No such file or directory",
            "The compiler cannot resolve the include. Verify the path, casing, generated-file step, and target_include_directories configuration.",
        ),
        (
            "warning C4005: 'NOMINMAX': macro redefinition",
            "NOMINMAX is defined in more than one place. Define it once in the target compile definitions or guard the source definition; with warnings-as-errors the duplicate warning stops the build.",
        ),
    ]
    index = 0
    for diagnostic, diagnosis in compiler_errors:
        for prompt in (
            "Explain this MSVC diagnostic and the most likely repair:",
            "What does this compiler or linker message mean?",
            "Diagnose this build failure:",
            "Give a C++23-oriented repair for this diagnostic:",
        ):
            out.append(record(
                f"boot-cpp-diagnostic-{index:04d}",
                "cpp23",
                "Interpret compiler diagnostics precisely and recommend the smallest safe repair.",
                f"{prompt}\n{diagnostic}",
                diagnosis,
                ("cpp23", "debugging", "compiler-diagnostic"),
            ))
            index += 1

    architecture = [
        (
            "One subsystem exclusively owns a GPU resource",
            "Use a move-only RAII wrapper with explicit exclusive ownership. Store it directly or behind std::unique_ptr only when indirection is required.",
            "ownership",
        ),
        (
            "Several viewports reference the same immutable mesh",
            "Keep one owning mesh resource and give viewports non-owning handles or IDs. Use shared_ptr only if the viewports genuinely share lifetime ownership.",
            "shared-resource",
        ),
        (
            "A worker must stop when its owner is destroyed",
            "Use std::jthread and pass its std::stop_token into the worker loop. Destruction requests stop and joins automatically.",
            "worker-lifecycle",
        ),
        (
            "Platform-neutral rendering code needs a Windows implementation",
            "Define a platform-neutral interface in the core namespace and place the Win32 implementation in a Windows-specific translation unit selected by CMake.",
            "platform-boundary",
        ),
        (
            "A task can return either a value or a recoverable error",
            "Use std::expected<Value, Error> when failure is normal control flow and callers must handle it explicitly.",
            "error-model",
        ),
        (
            "A hot loop processes positions and velocities for many entities",
            "Prefer contiguous arrays and a data-oriented pass over active entities. Measure before introducing more complex storage layouts.",
            "data-oriented",
        ),
        (
            "A window manager must support split-screen and future VR views",
            "Represent each view with explicit camera, viewport, render target, and presentation state. Keep window ownership separate from per-view rendering so multiple views can share or separate surfaces.",
            "multi-view",
        ),
        (
            "A static library exposes cross-platform engine APIs",
            "Keep exports and public declarations in platform-neutral headers, isolate platform macros in platform implementation files, and make CMake select the correct sources without changing the public ABI.",
            "static-library",
        ),
    ]
    constraints = [
        "Prioritize correctness and explicit lifetime.",
        "Avoid unnecessary heap allocation.",
        "Keep platform-specific code isolated.",
        "Prefer composition over inheritance.",
        "Keep the API testable and rollback-safe.",
        "Optimize only after preserving behavior with tests.",
    ]
    index = 0
    for problem, answer, tag in architecture:
        for constraint in constraints:
            for prompt in (
                f"Choose a C++23 design for this situation: {problem}. {constraint}",
                f"How should I architect this in C++23? {problem}. {constraint}",
                f"Give the ownership and subsystem design for: {problem}. {constraint}",
            ):
                out.append(record(
                    f"boot-cpp-architecture-{index:05d}",
                    "cpp23",
                    "Choose a C++23 design based on ownership, lifetime, performance, and subsystem boundaries.",
                    prompt,
                    answer,
                    ("cpp23", "architecture", "design", tag),
                ))
                index += 1

    cmake_tasks = [
        (
            "Create a static library target named core from src/core.cpp and expose include publicly.",
            "```cmake\nadd_library(core STATIC src/core.cpp)\ntarget_include_directories(core PUBLIC include)\ntarget_compile_features(core PUBLIC cxx_std_23)\n```",
        ),
        (
            "Enable strict warnings as errors for MSVC on target core.",
            "```cmake\ntarget_compile_options(core PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus)\n```",
        ),
        (
            "Add src/platform/windows/window.cpp only on Windows.",
            "```cmake\nif(WIN32)\n    target_sources(core PRIVATE src/platform/windows/window.cpp)\nendif()\n```",
        ),
        (
            "Require CMake 3.30 and C++23 without compiler extensions.",
            "```cmake\ncmake_minimum_required(VERSION 3.30)\nset(CMAKE_CXX_STANDARD 23)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_CXX_EXTENSIONS OFF)\n```",
        ),
        (
            "Link Comctl32 and Winhttp to a Windows GUI target named app.",
            "```cmake\ntarget_link_libraries(app PRIVATE Comctl32 Winhttp)\n```",
        ),
    ]
    index = 0
    for task, answer in cmake_tasks:
        for frame in (
            "Write the CMake for this requirement: {task}",
            "Give a modern CMake snippet: {task}",
            "Implement this target rule: {task}",
        ):
            out.append(record(
                f"boot-cpp-cmake-{index:04d}",
                "cpp23",
                "Return modern target-oriented CMake suitable for a C++23 project.",
                frame.format(task=task),
                answer,
                ("cpp23", "cmake", "build-system"),
            ))
            index += 1

    test_tasks = [
        (
            "add",
            "require(add(2, 3) == 5);\nrequire(add(-2, 2) == 0);",
        ),
        (
            "is_even",
            "require(is_even(4));\nrequire(!is_even(3));",
        ),
        (
            "text_length",
            "require(text_length(\"abc\") == 3U);\nrequire(text_length(\"\") == 0U);",
        ),
        (
            "contains_zero",
            "const std::array values{1, 0, 3};\nrequire(contains_zero(values));",
        ),
    ]
    index = 0
    for function_name, assertions in test_tasks:
        for wording in (
            f"Write two focused assertions for {function_name}.",
            f"Give a minimal C++23 test for {function_name}.",
            f"Test normal and boundary behavior of {function_name}.",
        ):
            out.append(record(
                f"boot-cpp-test-{index:04d}",
                "cpp23",
                "Write deterministic C++23 test assertions with one normal case and one edge case.",
                wording,
                f"```cpp\n{assertions}\n```",
                ("cpp23", "testing", "verification"),
            ))
            index += 1

    return out


def _normalize_handcrafted(item: dict) -> dict:
    result = dict(item)
    result.setdefault("accepted", True)
    result.setdefault("provenance", "threadspire-handcrafted-seed")
    result.setdefault("tags", [str(result.get("domain", "general"))])
    return result


def split_train_anchors(records: Sequence[dict], anchor_count: int) -> tuple[list[dict], list[dict]]:
    # First deduplicate by both ID and normalized messages.  Duplicate semantic
    # examples should not gain weight merely because a generator loop repeated.
    by_id: dict[str, dict] = {}
    by_fingerprint: dict[str, dict] = {}
    for item in records:
        identifier = str(item.get("id", "")).strip()
        if not identifier:
            continue
        fingerprint = _fingerprint(item)
        if fingerprint in by_fingerprint:
            continue
        by_id[identifier] = item
        by_fingerprint[fingerprint] = item
    unique = list(by_id.values())

    groups: dict[str, list[dict]] = defaultdict(list)
    for item in unique:
        tags = [str(tag) for tag in item.get("tags", [])]
        key = ":".join(tags[:2]) if tags else str(item.get("domain", "general"))
        groups[key].append(item)

    randomizer = random.Random(1337)
    for group in groups.values():
        randomizer.shuffle(group)

    anchors: list[dict] = []
    # Give every capability group at least one held-out example, then fill the
    # remaining slots round-robin so broad categories cannot dominate anchors.
    ordered_keys = sorted(groups)
    for key in ordered_keys:
        if groups[key] and len(anchors) < anchor_count:
            anchors.append(groups[key].pop())
    while len(anchors) < anchor_count:
        progressed = False
        for key in ordered_keys:
            if groups[key] and len(anchors) < anchor_count:
                anchors.append(groups[key].pop())
                progressed = True
        if not progressed:
            break

    anchor_fingerprints = {_fingerprint(item) for item in anchors}
    training = [item for item in unique if _fingerprint(item) not in anchor_fingerprints]
    randomizer.shuffle(training)
    randomizer.shuffle(anchors)
    held_out = [
        {**item, "id": "anchor-" + str(item["id"]), "provenance": "threadspire-held-out-anchor"}
        for item in anchors
    ]
    return training, held_out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--handcrafted", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--anchors", type=Path, required=True)
    parser.add_argument("--anchor-count", type=int, default=256)
    args = parser.parse_args()

    handcrafted = [
        _normalize_handcrafted(json.loads(line))
        for line in args.handcrafted.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    generated = english_records() + cpp_records()
    training, held_out = split_train_anchors(
        handcrafted + generated,
        max(32, args.anchor_count),
    )
    args.output.write_text(
        "".join(json.dumps(item, ensure_ascii=False) + "\n" for item in training),
        encoding="utf-8",
    )
    args.anchors.write_text(
        "".join(json.dumps(item, ensure_ascii=False) + "\n" for item in held_out),
        encoding="utf-8",
    )
    print(
        f"wrote {len(training)} unique bootstrap records and "
        f"{len(held_out)} disjoint held-out anchors"
    )


if __name__ == "__main__":
    main()
