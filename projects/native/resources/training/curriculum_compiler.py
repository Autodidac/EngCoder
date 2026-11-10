#!/usr/bin/env python3
"""Compile append-only EngCoder evidence into a small deterministic curriculum.

This module deliberately uses only the Python standard library.  Source JSONL
files are audit ledgers and are never rewritten; the selected JSONL and its
manifest are separate, atomically replaced artifacts.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import unicodedata
from typing import Any, Iterable, Iterator, Sequence


SCHEMA_VERSION = 1
COMPILER_POLICY = "engcoder-minimal-curriculum-v1"
DEFAULT_TOKEN_BUDGET = 131_072
DEFAULT_REPLAY_BUDGET = 32
MAX_TOKEN_BUDGET = 1_048_576
MAX_REPLAY_BUDGET = 256
MAX_DELTA_LIMIT = 256


@dataclass(frozen=True, slots=True)
class TrainingValidationSchedule:
    """Deterministic evaluation boundaries for a bounded training run.

    One interval covers every selected sequence at least once and is aligned
    to a completed gradient-accumulation boundary.  Foundation training waits
    for two such intervals before it may finish early; an already-trained
    expert waits for one.  ``maximum_steps`` remains a hard safety ceiling.
    """

    corpus_pass_steps: int
    interval_steps: int
    minimum_steps: int


def training_validation_schedule(
    sequence_count: int,
    batch_size: int,
    accumulation_steps: int,
    maximum_steps: int,
    *,
    foundation: bool,
) -> TrainingValidationSchedule:
    """Return a dependency-free, optimizer-aligned validation schedule."""

    sequences = max(1, int(sequence_count))
    batch = max(1, int(batch_size))
    accumulation = max(1, int(accumulation_steps))
    ceiling = max(0, int(maximum_steps))
    corpus_pass = max(1, math.ceil(sequences / batch))
    interval = ((corpus_pass + accumulation - 1) // accumulation) * accumulation
    minimum = interval * (2 if foundation else 1)
    return TrainingValidationSchedule(
        corpus_pass_steps=corpus_pass,
        interval_steps=interval,
        minimum_steps=min(ceiling, minimum),
    )

_KNOWN_REASONS = (
    "invalid_json",
    "invalid_record",
    "missing_training_pair",
    "non_english",
    "out_of_scope",
    "rejected",
    "not_eligible",
    "error",
    "private",
    "volatile",
    "branding",
    "emoji_noise",
    "superseded_by_correction",
    "anchor_overlap",
    "duplicate_identifier",
    "exact_duplicate",
    "canonical_duplicate",
    "answer_duplicate",
    "token_budget",
    "replay_budget",
)

_CONTRACTIONS = {
    "can't": "can not",
    "cannot": "can not",
    "won't": "will not",
    "shan't": "shall not",
    "ain't": "is not",
    "i'm": "i am",
    "you're": "you are",
    "we're": "we are",
    "they're": "they are",
    "it's": "it is",
    "that's": "that is",
    "what's": "what is",
    "here's": "here is",
    "there's": "there is",
    "i've": "i have",
    "we've": "we have",
    "they've": "they have",
    "you've": "you have",
}

_TOKEN_ALIASES = {
    "calculate": "compute",
    "evaluate": "compute",
    "solve": "compute",
    "describe": "explain",
    "clarify": "explain",
    "provide": "give",
    "supply": "give",
    "reply": "answer",
    "respond": "answer",
    "response": "answer",
    "definition": "define",
    "meaning": "define",
    "uppercase": "upper",
    "capitalise": "upper",
    "capitalize": "upper",
    "lowercase": "lower",
    "summarise": "summarize",
    "alphabetise": "sort",
    "alphabetize": "sort",
}

_POLITE_STOPWORDS = {
    "a",
    "an",
    "the",
    "please",
    "kindly",
    "could",
    "would",
    "you",
    "me",
    "this",
    "following",
    "requested",
    "request",
}

_NUMBER_WORDS = {
    "zero": 0,
    "one": 1,
    "two": 2,
    "three": 3,
    "four": 4,
    "five": 5,
    "six": 6,
    "seven": 7,
    "eight": 8,
    "nine": 9,
    "ten": 10,
    "eleven": 11,
    "twelve": 12,
}

_NON_ENGLISH_LABELS = {
    "arabic", "chinese", "dutch", "french", "german", "hindi",
    "italian", "japanese", "korean", "mandarin", "portuguese",
    "russian", "spanish", "zh", "ja", "ko", "fr", "de", "es",
}

_CODE_LABELS = {
    "c", "cmake", "code", "coder", "coding", "cpp", "cpp23", "cxx",
    "msvc", "python", "rust", "javascript", "typescript", "java",
}

_CPP_SCOPE_LABELS = {"c", "cmake", "cpp", "cpp23", "cxx", "msvc"}

_CODE_MARKERS = (
    "#include", "std::", "constexpr", "template<", "namespace ",
    "```cpp", "```c++", "```python", "nullptr", "co_await",
)

_ERROR_VALUES = {
    "error", "errored", "failed", "failure", "exception", "cancelled",
    "canceled", "timeout", "pending", "retry-pending", "retrying",
}

_PRIVATE_VALUES = {
    "private", "secret", "sensitive", "personal", "confidential",
    "user-memory-only", "retrieval-only",
}

_BRANDING_RE = re.compile(
    r"\b(?:as\s+(?:an?\s+)?(?:ai\s+)?(?:assistant|(?:language\s+)?model)(?:\s+(?:from|by))?"
    r"|(?:i\s+am|my\s+name\s+is)\s+(?:chatgpt|claude|gemini|copilot|grok|qwen|llama|nemotron|deepseek|mistral)"
    r"|i\s+am\s+an?\s+(?:ai\s+)?(?:assistant|language\s+model)"
    r"|i\s+am\s+an?\s+(?:openai|anthropic|google|microsoft|xai)\b"
    r"|as\s+(?:chatgpt|claude|gemini|copilot|grok|qwen|llama|nemotron|deepseek|mistral)\b"
    r"|(?:powered|created|built|trained|developed)\s+by\s+(?:openai|anthropic|google|microsoft|xai|meta|nvidia)\b"
    r"|(?:this\s+is|i\s+am\s+from|i\s+was\s+created\s+by)\s+(?:chatgpt|claude|gemini|copilot|grok|qwen|llama|nemotron|deepseek|mistral|openai|anthropic)\b)",
    re.IGNORECASE,
)

_PRIVATE_TEXT_RE = re.compile(
    r"(?:\b(?:my\s+(?:name|address|phone|email|birthday)|i\s+live\s+at|home\s+address|"
    r"social\s+security|ssn|credit\s+card|bank\s+account|confidential|proprietary|"
    r"internal\s+only|private\s+repo|not\s+public)\b"
    r"|\b(?:password|passcode|api[_ -]?key|access[_ -]?token|secret[_ -]?key|private[_ -]?key)"
    r"\s*(?:is|=|:)\s*[\"']?[^\s\"']{4,}"
    r"|\b(?:sk|ghp|github_pat|xox[baprs])[-_][A-Za-z0-9_-]{8,}\b"
    r"|\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"
    r"|-----BEGIN(?: [A-Z0-9]+)* PRIVATE KEY-----"
    r"|\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b"
    r"|\b\d{3}-\d{2}-\d{4}\b"
    r"|\b(?:\d[ -]*?){13,19}\b"
    r"|\b(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2})\b"
    r"|\b[A-Za-z]:[\\/]Users[\\/][^\s\\/]+"
    r"|/(?:home|Users)/[^\s/]+)",
    re.IGNORECASE,
)

_VOLATILE_TEXT_RE = re.compile(
    r"\b(?:today|latest|currently|right\s+now|most\s+recent|this\s+week|this\s+month|"
    r"as\s+of\s+(?:today|now|\d{4}(?:-\d{1,2}(?:-\d{1,2})?)?)|"
    r"current\s+(?:fact|version|price|rate|weather|news|president|leader|branch|commit|build|file)|"
    r"live\s+(?:price|rate|weather|news|status))\b",
    re.IGNORECASE,
)

_ERROR_CARD_RE = re.compile(
    r"^\s*(?:error|failure|exception|timeout|request\s+failed|operation\s+failed|"
    r"generation\s+failed)(?:\s*[:\-]|\s*$)",
    re.IGNORECASE,
)

_TOOL_TRACE_RE = re.compile(r"tool-trace-v1:[0-9a-f]{16}", re.IGNORECASE)

_TRAINING_EMOJI_RE = re.compile(
    "[\u00A9\u00AE\u203C\u2049\u2122\u2139\u2194-\u2199\u21A9-\u21AA"
    "\u231A-\u231B\u2328\u23CF\u23E9-\u23F3\u23F8-\u23FA\u24C2"
    "\u25AA-\u25AB\u25B6\u25C0\u25FB-\u25FE\u2600-\u27BF\u2934-\u2935"
    "\u2B05-\u2B07\u2B1B-\u2B1C\u2B50\u2B55\u3030\u303D\u3297\u3299"
    "\U0001F000-\U0001FFFF\U000E0020-\U000E007F]"
)
_TRAINING_KEYCAP_RE = re.compile(r"[#*0-9]\ufe0f?\u20e3")
_TRAINING_EMOJI_FORMAT_RE = re.compile("[\u200d\ufe0e\ufe0f\u20e3]")

_PATH_RE = re.compile(
    r"(?<![\w])(?:[a-zA-Z]:[\\/](?:[^\s<>:\"|?*]+[\\/]?)+"
    r"|(?:\.\.?[\\/]|/)(?:[^\s<>|]+[\\/]?)+)"
)
_NUMBER_RE = re.compile(r"(?<![\w])[-+]?(?:0x[0-9a-f]+|\d+(?:\.\d+)?)(?![\w])", re.I)
_IDENTIFIER_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
_WORD_RE = re.compile(r"[a-z0-9]+(?:\+[a-z0-9]+)?|[+*/%-]")
_EXACT_COPY_RE = re.compile(
    r"^\s*(?P<directive>(?:please\s+)?(?:reply|respond|return|output|write|say|use|copy|repeat)\b"
    r"[^:\n]{0,160}\b(?:exactly|only)\b[^:\n]{0,160})\s*:[ \t]*(?P<payload>[\s\S]+?)\s*$",
    re.IGNORECASE,
)


def _as_paths(values: Path | str | os.PathLike[str] | Iterable[Path | str | os.PathLike[str]] | None) -> tuple[Path, ...]:
    if values is None:
        return ()
    if isinstance(values, (str, os.PathLike)):
        return (Path(values),)
    return tuple(Path(value) for value in values)


def _resolved_paths(
    values: Path | str | os.PathLike[str] | Iterable[Path | str | os.PathLike[str]] | None,
) -> tuple[Path, ...]:
    return tuple(sorted(
        {path.resolve() for path in _as_paths(values)},
        key=lambda path: str(path).casefold(),
    ))


def _stable_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _text(value: Any) -> str:
    return "" if value is None else str(value)


def _record_hash(record: dict[str, Any]) -> str:
    return hashlib.sha256(_stable_json(record).encode("utf-8")).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _iter_jsonl(path: Path) -> Iterator[tuple[int, dict[str, Any] | None, str | None]]:
    with path.open("r", encoding="utf-8-sig", errors="replace") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except (TypeError, ValueError):
                yield line_number, None, "invalid_json"
                continue
            if not isinstance(value, dict):
                yield line_number, None, "invalid_record"
                continue
            yield line_number, value, None


def read_records(
    paths: Path | str | os.PathLike[str] | Iterable[Path | str | os.PathLike[str]],
) -> list[dict[str, Any]]:
    """Read JSON objects from one or more JSONL files, skipping bad lines.

    The helper is intentionally read-only.  ``compile_curriculum`` records bad
    lines in its manifest; this convenience function simply returns objects.
    """

    records: list[dict[str, Any]] = []
    for path in _as_paths(paths):
        for _line_number, record, _error in _iter_jsonl(path):
            if record is not None:
                records.append(record)
    return records


def _truthy(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def _normalize_label(value: Any) -> str:
    return re.sub(r"[^a-z0-9+]+", "-", str(value).casefold()).strip("-")


def _labels(record: dict[str, Any]) -> set[str]:
    values: list[Any] = []
    for name in ("language", "locale", "domain", "skill", "skill_id", "target", "curriculum"):
        if record.get(name) not in (None, ""):
            values.append(record[name])
    tags = record.get("tags", [])
    if isinstance(tags, (list, tuple, set)):
        values.extend(tags)
    elif tags not in (None, ""):
        values.append(tags)
    return {_normalize_label(value) for value in values if str(value).strip()}


def _expand_contractions(value: str) -> str:
    text = unicodedata.normalize("NFKC", str(value)).replace("’", "'").replace("`", "'")
    for contraction, expansion in _CONTRACTIONS.items():
        text = re.sub(rf"\b{re.escape(contraction)}\b", expansion, text, flags=re.IGNORECASE)
    text = re.sub(r"\b([A-Za-z]+)n't\b", r"\1 not", text, flags=re.IGNORECASE)
    return text


def _exact_text(value: str) -> str:
    text = _expand_contractions(value).casefold()
    text = re.sub(r"[^\w]+", " ", text, flags=re.UNICODE)
    return re.sub(r"\s+", " ", text).strip()


def _looks_identifier(token: str) -> bool:
    return bool(
        "_" in token
        or re.search(r"[a-z][A-Z]", token)
        or re.search(r"[A-Za-z]\d|\d[A-Za-z]", token)
    )


def _replace_slots(value: str, *, abstract_literals: bool = True) -> str:
    text = unicodedata.normalize("NFKC", value)
    text = _PATH_RE.sub(" pathslot ", text)
    text = re.sub(
        r"`([^`\n]{1,120})`",
        lambda match: " identifierslot " if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", match.group(1).strip()) else " literalslot ",
        text,
    )
    if abstract_literals:
        text = re.sub(r'"[^"\n]{1,200}"', " literalslot ", text)
        text = re.sub(r"(?<!\w)'[^'\n]{1,200}'(?!\w)", " literalslot ", text)
    text = _IDENTIFIER_RE.sub(
        lambda match: " identifierslot " if _looks_identifier(match.group(0)) else match.group(0),
        text,
    )
    return _NUMBER_RE.sub(" numberslot ", text)


def _tokens(value: str, *, abstract_literals: bool = True) -> list[str]:
    text = _replace_slots(_expand_contractions(value), abstract_literals=abstract_literals).casefold()
    result: list[str] = []
    for token in _WORD_RE.findall(text):
        token = _TOKEN_ALIASES.get(token, token)
        if token in _POLITE_STOPWORDS:
            continue
        if token.endswith("ies") and len(token) > 5:
            token = token[:-3] + "y"
        elif token.endswith("s") and len(token) > 5 and not token.endswith(("ss", "ous", "is")):
            token = token[:-1]
        if not result or result[-1] != token:
            result.append(token)
    return result


def _polarity_signature(value: str) -> str:
    text = " " + _exact_text(value) + " "
    negative = bool(re.search(
        r"\b(?:not|never|no|without|avoid(?:ed|ing)?|exclude(?:d|s|ing)?|"
        r"omit(?:ted|s|ting)?|forbid(?:den|s|ding)?|prohibit(?:ed|s|ing)?|"
        r"refuse(?:d|s|ing)?|deny|denied|denies|denying|disallow(?:ed|s|ing)?|"
        r"disable(?:d|s|ing)?|remove(?:d|s|ing)?|withhold(?:s|ing)?|unsafe)\b",
        text,
    ))
    positive = bool(re.search(
        r"\b(?:allow(?:ed|s|ing)?|permit(?:ted|s|ting)?|include(?:d|s|ing)?|"
        r"reveal(?:ed|s|ing)?|expose(?:d|s|ing)?|enable(?:d|s|ing)?|safe)\b",
        text,
    ))
    required = bool(re.search(r"\b(?:must|required|always|only|exactly)\b", text))
    optional = bool(re.search(r"\b(?:may|optional)\b", text))
    privacy = bool(re.search(r"\b(?:secret|private|credential|password|token|personal)\b", text))
    if negative:
        direction = "negative"
    elif positive:
        direction = "positive"
    else:
        direction = "neutral"
    modifiers = [direction]
    if required:
        modifiers.append("required")
    if optional:
        modifiers.append("optional")
    if privacy:
        modifiers.append("privacy")
    return "+".join(modifiers)


def _arithmetic_signature(value: str) -> str | None:
    text = _expand_contractions(value).casefold()
    number_count = len(_NUMBER_RE.findall(text))
    word_numbers = re.findall(
        r"\b(?:zero|one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve)\b",
        text,
    )
    if number_count + len(word_numbers) < 2:
        return None
    operations: list[tuple[str, str]] = [
        ("add", r"\+|\b(?:add|plus|sum|total)\b"),
        ("subtract", r"(?<!\w)-(?!\w)|\b(?:subtract|minus|difference)\b"),
        ("multiply", r"\*|×|\b(?:multiply|times|product)\b"),
        ("divide", r"/|÷|\b(?:divide|divided|quotient)\b"),
        ("modulo", r"%|\b(?:modulo|remainder)\b"),
    ]
    operation = next((name for name, pattern in operations if re.search(pattern, text)), None)
    if operation is None:
        return None
    qualifiers: list[str] = []
    for name, pattern in (
        ("round", r"\bround(?:ed|ing)?\b"),
        ("round-up", r"\b(?:up|ceil(?:ing)?)\b"),
        ("round-down", r"\b(?:down|floor(?:ed|ing)?)\b"),
        ("truncate", r"\btruncat(?:e|ed|ing|ion)\b"),
        ("nearest", r"\bnearest\b"),
        ("percent", r"%|\bpercent(?:age)?\b"),
        ("decimal", r"\bdecimal\b"),
        ("integer", r"\binteger\b"),
    ):
        if re.search(pattern, text):
            qualifiers.append(name)
    suffix = ":" + ",".join(qualifiers) if qualifiers else ""
    return f"arithmetic:{operation}:arity{min(number_count + len(word_numbers), 3)}{suffix}"


def _definition_signature(value: str) -> str | None:
    text = _expand_contractions(value).strip()
    patterns = (
        r"^\s*(?:please\s+)?define\s+(?:the\s+(?:word|term)\s+)?(.+?)\s*[?.!]*$",
        r"^\s*(?:please\s+)?(?:give|provide)\s+(?:a\s+)?(?:concise\s+)?definition\s+(?:of|for)\s+(.+?)\s*[?.!]*$",
        r"^\s*what\s+does\s+(.+?)\s+mean\s*[?.!]*$",
        r"^\s*what\s+is\s+the\s+(?:meaning|definition)\s+of\s+(.+?)\s*[?.!]*$",
        r"^\s*explain\s+(?:what\s+)?(?:the\s+(?:word|term)\s+)?(.+?)\s+means?\s*[?.!]*$",
        r"^\s*explain\s+(?:what\s+)?(.+?)\s+is\s*[?.!]*$",
        r"^\s*explain\s+(?:the\s+)?(?:word|term)\s+(.+?)\s*[?.!]*$",
        r"^\s*what\s+is\s+(?:a|an)\s+([^?!.]{1,80})\s*[?.!]*$",
        r"^\s*what\s+is\s+([^?!.]{1,80})\s*[?.!]*$",
    )
    term = next((match.group(1) for pattern in patterns if (match := re.match(pattern, text, re.I))), None)
    if term is None:
        return None
    term_tokens = _tokens(term, abstract_literals=False)
    if not term_tokens or len(term_tokens) > 8:
        return None
    return "define:" + "-".join(term_tokens)


def _conversation_signature(value: str) -> str | None:
    text = _exact_text(value)
    if re.fullmatch(r"(?:hi|hello|hey|howdy)(?: there)?", text):
        return "conversation:greeting"
    if re.fullmatch(
        r"(?:(?:what is|whats) (?:up|going on)|how is it going|how are you)",
        text,
    ):
        return "conversation:casual-check-in"
    if re.fullmatch(r"(?:thanks|thank you|many thanks)(?: very much)?", text):
        return "conversation:gratitude"
    if re.fullmatch(r"(?:bye|goodbye|farewell|see you|talk to you later)", text):
        return "conversation:farewell"
    return None


def _copy_word_case(value: str) -> str:
    letters = "".join(character for character in value if character.isalpha())
    if not letters:
        return "uncased"
    if letters.islower():
        return "lower"
    if letters.isupper():
        return "upper"
    if letters[0].isupper() and letters[1:].islower():
        return "capitalized"
    return "mixed"


def _copy_structure(value: str) -> tuple[int, int, str, str]:
    words = re.findall(r"\w+", value, flags=re.UNICODE)
    case_pattern = ",".join(_copy_word_case(word) for word in words) or "none"
    shape: list[str] = []
    index = 0
    while index < len(value):
        character = value[index]
        if character.isalnum() or character == "_":
            while index < len(value) and (value[index].isalnum() or value[index] == "_"):
                index += 1
            shape.append("w")
            continue
        if character == " ":
            end = index + 1
            while end < len(value) and value[end] == " ":
                end += 1
            count = end - index
            shape.append(f"s{count}" if count <= 3 else "s4+")
            index = end
            continue
        if character == "\t":
            end = index + 1
            while end < len(value) and value[end] == "\t":
                end += 1
            count = end - index
            shape.append(f"t{count}" if count <= 3 else "t4+")
            index = end
            continue
        if character == "\r" and index + 1 < len(value) and value[index + 1] == "\n":
            shape.append("nl")
            index += 2
            continue
        if character in "\r\n":
            shape.append("nl")
        else:
            shape.append(f"u{ord(character):x}")
        index += 1
    return len(words), value.count("\n") + 1, case_pattern, "-".join(shape)


def _exact_copy_signature(prompt: str, response: str) -> str | None:
    """Describe exact-copy behavior without retaining the copied payload."""

    match = _EXACT_COPY_RE.match(unicodedata.normalize("NFKC", prompt))
    if match is None:
        return None
    directive = match.group("directive")
    payload = match.group("payload")
    if payload.startswith("\r\n"):
        payload = payload[2:]
    elif payload.startswith("\n"):
        payload = payload[1:]

    quote_kind = "unquoted"
    copied = payload
    quote_names = {'"': "double", "'": "single", "`": "backtick"}
    if len(payload) >= 2 and payload[0] in quote_names and payload[-1] == payload[0]:
        inner = payload[1:-1]
        if response == inner:
            quote_kind = f"{quote_names[payload[0]]}-delimited"
            copied = inner
        elif response == payload:
            quote_kind = f"{quote_names[payload[0]]}-retained"
        else:
            return None
    elif response != payload:
        return None

    count_match = re.search(
        r"\b(?P<count>\d+|zero|one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve)"
        r"[ -]?words?\b",
        directive,
        re.IGNORECASE,
    )
    declared = "unspecified"
    if count_match is not None:
        count_text = count_match.group("count").casefold()
        declared = str(_NUMBER_WORDS.get(count_text, int(count_text) if count_text.isdigit() else count_text))

    word_count, line_count, case_pattern, shape = _copy_structure(copied)
    return (
        f"exact-copy:declared-{declared}:words-{word_count}:lines-{line_count}:"
        f"quote-{quote_kind}:case-{case_pattern}:shape-{shape}"
    )


def _transform_signature(value: str) -> str | None:
    text = _exact_text(value)
    operations = (
        ("upper", r"\b(?:uppercase|upper case|capitalize|capitalise)\b"),
        ("lower", r"\b(?:lowercase|lower case)\b"),
        ("reverse", r"\breverse\b"),
        ("sort", r"\b(?:sort|alphabetize|alphabetise|alphabetical)\b"),
        ("summarize", r"\b(?:summarize|summarise|summary)\b"),
        ("rewrite", r"\b(?:rewrite|rephrase|paraphrase)\b"),
    )
    operation = next((name for name, pattern in operations if re.search(pattern, text)), None)
    if operation is None:
        return None
    qualifiers: list[str] = []
    if re.search(r"\bdescending\b", text):
        qualifiers.append("descending")
    elif re.search(r"\bascending\b", text):
        qualifiers.append("ascending")
    elif re.search(r"\b(?:oldest|earliest)\s+first\b", text):
        qualifiers.append("oldest-first")
    elif re.search(r"\b(?:newest|latest)\s+first\b", text):
        qualifiers.append("newest-first")
    elif re.search(r"\blast\s+first\b", text):
        qualifiers.append("last-first")
    if re.search(r"\bconcise(?:ly)?\b", text):
        qualifiers.append("concise")
    suffix = ":" + ",".join(qualifiers) if qualifiers else ""
    return f"transform:{operation}{suffix}"


def _constraint_sensitive(value: str) -> bool:
    return bool(re.search(
        r"\b(?:allow|can|cannot|include|exclude|must|never|not|only|permit|private|refuse|reveal|safe|secret|should|without)\b",
        _exact_text(value),
    ))


def _answer_polarity(value: str) -> str:
    text = _exact_text(value)
    if re.match(r"^(?:no|cannot|can not|i refuse|refuse|do not|must not|will not)\b", text):
        return "negative"
    if re.match(r"^(?:yes|can|i can|i will|will|must|should)\b", text):
        return "positive"
    if re.search(r"\b(?:do not|must not|should not|never|cannot|can not|refuse)\b", text):
        return "negative"
    return "neutral"


def _dialogue(record: dict[str, Any]) -> tuple[str, str, str]:
    systems: list[str] = []
    users: list[str] = []
    assistants: list[str] = []
    messages = record.get("messages")
    if isinstance(messages, list):
        for item in messages:
            if not isinstance(item, dict):
                continue
            role = _text(item.get("role", "")).strip().casefold()
            content = _text(item.get("content", "")).strip()
            if not content:
                continue
            if role == "system":
                systems.append(content)
            elif role == "user":
                users.append(content)
            elif role == "assistant":
                assistants.append(content)

    if users:
        prompt = "\n".join(users)
    else:
        prompt = _text(record.get("prompt", record.get("question", ""))).strip()
        if not prompt:
            instruction = _text(record.get("instruction", "")).strip()
            input_text = _text(record.get("input", "")).strip()
            prompt = "\n".join(value for value in (instruction, input_text) if value)

    if assistants:
        response = assistants[-1]
    else:
        response = _text(
            record.get(
                "response",
                record.get("answer", record.get("completion", record.get("output", record.get("target", "")))),
            )
        ).strip()
    if _is_hard_correction(record):
        response = _text(record.get("correction", response)).strip()
    return "\n".join(systems), prompt, response


def _message_contents(record: dict[str, Any], roles: set[str] | None = None) -> list[str]:
    contents: list[str] = []
    messages = record.get("messages")
    if not isinstance(messages, list):
        return contents
    for item in messages:
        if not isinstance(item, dict):
            continue
        role = _text(item.get("role", "")).strip().casefold()
        content = _text(item.get("content", "")).strip()
        if content and (roles is None or role in roles):
            contents.append(content)
    return contents


def _supervised_responses(record: dict[str, Any], fallback: str) -> list[str]:
    if _is_hard_correction(record):
        return [fallback] if fallback else []
    assistants = _message_contents(record, {"assistant"})
    return assistants if assistants else ([fallback] if fallback else [])


def _all_training_text(record: dict[str, Any], system: str, prompt: str, response: str) -> str:
    hard_correction = _is_hard_correction(record)
    message_roles = {"system", "user"} if hard_correction else None
    values = [system, prompt, response, *_message_contents(record, message_roles)]
    names = ["prompt", "question", "instruction", "input", "correction"]
    if not hard_correction:
        names.extend(("response", "answer", "completion", "output", "target"))
    for name in names:
        value = _text(record.get(name, "")).strip()
        if value:
            values.append(value)
    return "\n".join(values)


def _exact_training_material(record: dict[str, Any]) -> str:
    messages = record.get("messages")
    if isinstance(messages, list):
        training_messages = [
            {
                "role": _text(item.get("role", "user")).strip().casefold() or "user",
                "content": _text(item.get("content", "")),
            }
            for item in messages
            if isinstance(item, dict) and _text(item.get("content", ""))
        ]
        if training_messages and any(
            item["role"] == "assistant" for item in training_messages
        ):
            return _stable_json(training_messages)
    system, prompt, response = _dialogue(record)
    return _stable_json([system, prompt, response])


def _record_skill(record: dict[str, Any]) -> str:
    return _normalize_label(record.get("skill", record.get("domain", "general"))) or "general"


def _has_durable_current_evidence(record: dict[str, Any]) -> bool:
    receipt = _text(record.get("tool_trace_hash", "")).strip()
    return bool(
        _truthy(record.get("reference_verified", False))
        and _TOOL_TRACE_RE.fullmatch(receipt)
    )


def behavior_signature(record_or_prompt: dict[str, Any] | str, response: str | None = None) -> str:
    """Return a conservative canonical behavior signature.

    Punctuation, case, common contractions, numeric operands, paths, quoted
    payloads, and identifier-shaped slots are normalized.  Constraint polarity
    and safety-sensitive answer polarity remain part of the key. Recognized
    exact-copy requests abstract payload vocabulary while retaining the output
    shape that the constraint requires.
    """

    if isinstance(record_or_prompt, dict):
        system, prompt, extracted_response = _dialogue(record_or_prompt)
        if response is None:
            response = extracted_response
    else:
        system = ""
        prompt = str(record_or_prompt)
    response = "" if response is None else str(response)

    exact_copy = _exact_copy_signature(prompt, response)
    core = (
        exact_copy
        or _arithmetic_signature(prompt)
        or _conversation_signature(prompt)
        or _definition_signature(prompt)
        or _transform_signature(prompt)
    )
    if core is None:
        canonical_tokens = _tokens(prompt, abstract_literals=True)
        core = "generic:" + "-".join(canonical_tokens)
    prompt_polarity = _polarity_signature(prompt + "\n" + system)
    signature = f"{core}|polarity:{prompt_polarity}"
    if exact_copy is None and _constraint_sensitive(prompt + "\n" + system):
        signature += f"|answer:{_answer_polarity(response)}"
    return signature


def _is_hard_correction(record: dict[str, Any]) -> bool:
    correction = _text(record.get("correction", "")).strip()
    return bool(
        correction
        and _truthy(record.get("human_rejected", False))
        and _text(record.get("provenance", "")) == "interactive-hard-correction"
    )


def _has_emoji(value: str) -> bool:
    return bool(
        _TRAINING_KEYCAP_RE.search(value)
        or _TRAINING_EMOJI_RE.search(value)
        or _TRAINING_EMOJI_FORMAT_RE.search(value)
    )


def _strip_training_emoji(value: str) -> str:
    marker = "\x1f"
    cleaned = _TRAINING_KEYCAP_RE.sub(marker, str(value))
    cleaned = _TRAINING_EMOJI_RE.sub(marker, cleaned)
    cleaned = _TRAINING_EMOJI_FORMAT_RE.sub("", cleaned)
    cleaned = re.sub(rf"[ \t]*{re.escape(marker)}+[ \t]*", " ", cleaned)
    return cleaned.strip()


def _clean_training_record(record: dict[str, Any]) -> dict[str, Any]:
    cleaned = json.loads(json.dumps(record, ensure_ascii=False))
    for name in (
        "prompt", "question", "instruction", "input", "response", "answer",
        "completion", "output", "target", "correction",
    ):
        if name in cleaned:
            cleaned[name] = _strip_training_emoji(_text(cleaned.get(name, "")))
    messages = cleaned.get("messages")
    if isinstance(messages, list):
        for item in messages:
            if isinstance(item, dict) and "content" in item:
                item["content"] = _strip_training_emoji(_text(item.get("content", "")))
    return cleaned


def _english_text(value: str) -> bool:
    letters = [character for character in value if character.isalpha()]
    if not letters:
        return True
    non_latin = 0
    for character in letters:
        try:
            name = unicodedata.name(character)
        except ValueError:
            non_latin += 1
            continue
        if "LATIN" not in name:
            non_latin += 1
    return non_latin <= max(2, len(letters) // 10)


def _is_english(record: dict[str, Any], combined_text: str, *, allow_code: bool = False) -> bool:
    labels = _labels(record)
    language = _normalize_label(record.get("language", record.get("locale", "")))
    if language and not (language == "keymaker" or language == "en" or language.startswith(("en-", "english-"))):
        return False
    if labels.intersection(_NON_ENGLISH_LABELS):
        return False
    if not allow_code and any(
        label in _CODE_LABELS
        or label.startswith(("cpp", "c++", "cmake", "python-", "rust-", "javascript-"))
        for label in labels
    ):
        return False
    lowered = combined_text.casefold()
    if not allow_code and any(marker in lowered for marker in _CODE_MARKERS):
        return False
    if not allow_code and lowered.count(";") >= 3 and lowered.count("{") + lowered.count("}") >= 2:
        return False
    return _english_text(combined_text)


def _scope_allows_code(scope: str | None) -> bool:
    normalized = _normalize_label(scope or "")
    return normalized.startswith(("cpp", "c++")) or normalized == "planning"


def _matches_scope(record: dict[str, Any], scope: str | None) -> bool:
    if not scope:
        return True
    normalized = _normalize_label(scope)
    if normalized.startswith("keymaker"):
        return True
    labels = _labels(record)
    if normalized in labels:
        return True
    if normalized.startswith(("cpp", "c++")) and any(
        label in _CPP_SCOPE_LABELS or label.startswith(("cpp", "c++", "cmake", "msvc"))
        for label in labels
    ):
        return True
    if normalized == "planning" and labels.intersection({"planning", "architecture", "design"}):
        return True
    return False


def _filter_reason(
    record: dict[str, Any],
    system: str,
    prompt: str,
    response: str,
    scope: str | None,
    *,
    had_emoji: bool = False,
) -> str | None:
    if not prompt or not response:
        return "emoji_noise" if had_emoji else "missing_training_pair"
    hard_correction = _is_hard_correction(record)
    if _truthy(record.get("is_error", False)) or any(
        record.get(name) not in (None, False, "", 0)
        for name in ("error", "generation_error", "last_error", "exception")
    ):
        return "error"
    status_values = {
        _normalize_label(record.get(name, ""))
        for name in ("status", "state", "outcome", "kind", "type")
    }
    if status_values.intersection(_ERROR_VALUES) or any(
        value.startswith(("error-", "failed-", "exception-")) for value in status_values
    ):
        return "error"
    if status_values.intersection({"rejected", "denied", "discarded", "not-accepted"}):
        return "rejected"
    if status_values.intersection({"not-eligible", "ineligible"}):
        return "not_eligible"
    if status_values.intersection(_PRIVATE_VALUES):
        return "private"
    if any(_truthy(record.get(name, False)) for name in (
        "private", "is_private", "contains_private_data", "contains_pii", "sensitive", "nonpublic",
    )):
        return "private"
    privacy_values = {
        _normalize_label(record.get(name, ""))
        for name in ("privacy", "visibility", "knowledge_destination")
    }
    if privacy_values.intersection(_PRIVATE_VALUES) or _labels(record).intersection(_PRIVATE_VALUES):
        return "private"
    if not hard_correction and (
        _truthy(record.get("human_rejected", False))
        or _truthy(record.get("rejected", False))
        or ("accepted" in record and not _truthy(record.get("accepted")))
    ):
        return "rejected"
    if "eligible_for_training" in record and not _truthy(record.get("eligible_for_training")):
        return "not_eligible"
    combined = _all_training_text(record, system, prompt, response)
    if _PRIVATE_TEXT_RE.search(combined):
        return "private"
    volatile = any(_truthy(record.get(name, False)) for name in (
        "volatile", "current_fact", "time_sensitive",
    )) or bool(_VOLATILE_TEXT_RE.search(combined))
    if volatile and not _has_durable_current_evidence(record):
        return "volatile"
    allow_code = _scope_allows_code(scope)
    if not _is_english(record, combined, allow_code=allow_code):
        return "non_english"
    if not _matches_scope(record, scope):
        return "out_of_scope"
    if allow_code and not hard_correction and not any(
        _truthy(record.get(name, False))
        for name in (
            "accepted", "eligible_for_training", "human_accepted",
            "deterministic_verified", "reference_verified",
        )
    ):
        return "not_eligible"
    supervised = _supervised_responses(record, response)
    if any(_ERROR_CARD_RE.search(value) for value in supervised):
        return "error"
    if any(_truthy(record.get(name, False)) for name in (
        "branding_noise", "provider_branding", "identity_leak",
    )) or any(_BRANDING_RE.search(_expand_contractions(value)) for value in supervised):
        return "branding"
    return None


def _numeric_score(value: Any) -> float:
    if isinstance(value, bool):
        return float(value)
    if isinstance(value, (int, float)) and math.isfinite(float(value)):
        return max(0.0, min(1.0, float(value)))
    if isinstance(value, str):
        try:
            parsed = float(value)
        except ValueError:
            parsed = math.nan
        if math.isfinite(parsed):
            return max(0.0, min(1.0, parsed))
    if isinstance(value, dict):
        scores = [
            float(item)
            for item in value.values()
            if isinstance(item, (int, float)) and not isinstance(item, bool) and math.isfinite(float(item))
        ]
        if scores:
            return max(0.0, min(1.0, sum(scores) / len(scores)))
    return 0.0


def _quality(record: dict[str, Any]) -> tuple[int, int, int, int, int, int, float]:
    correction = int(_is_hard_correction(record))
    human = int(_truthy(record.get("human_accepted", False)))
    reference = int(_truthy(record.get("reference_verified", False)))
    deterministic = int(_truthy(record.get("deterministic_verified", False)))
    gates = sum(int(_truthy(record.get(name, False))) for name in ("tests_passed", "compiler_passed"))
    accepted = int(_truthy(record.get("accepted", False)) or _truthy(record.get("eligible_for_training", False)))
    generated_penalty = -int(_truthy(record.get("self_generated", False)))
    return (correction, human, reference, deterministic, gates, accepted + generated_penalty, _numeric_score(record.get("score")))


def _timestamp(record: dict[str, Any]) -> float | None:
    for name in ("created_unix_ms", "updated_unix_ms", "timestamp_unix_ms", "created_at_unix_ms"):
        value = record.get(name)
        if isinstance(value, (int, float)) and math.isfinite(float(value)):
            return float(value)
        if isinstance(value, str):
            try:
                parsed_numeric = float(value)
            except ValueError:
                pass
            else:
                if math.isfinite(parsed_numeric):
                    return parsed_numeric
    for name in ("created_at", "updated_at", "timestamp"):
        value = str(record.get(name, "")).strip()
        if not value:
            continue
        try:
            parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
            if parsed.tzinfo is None:
                parsed = parsed.replace(tzinfo=timezone.utc)
            return parsed.timestamp() * 1000.0
        except ValueError:
            continue
    return None


def _prepared_record(record: dict[str, Any], identifier: str) -> dict[str, Any]:
    prepared = json.loads(json.dumps(record, ensure_ascii=False))
    prepared.setdefault("id", identifier)
    prepared.pop("curriculum_weight", None)
    if _is_hard_correction(prepared):
        correction = _text(prepared.get("correction", "")).strip()
        prepared["response"] = correction
        messages = prepared.get("messages")
        if isinstance(messages, list):
            last_assistant: dict[str, Any] = {"role": "assistant"}
            non_assistant: list[Any] = []
            for item in messages:
                if (
                    isinstance(item, dict)
                    and str(item.get("role", "")).strip().casefold() == "assistant"
                ):
                    last_assistant = dict(item)
                else:
                    non_assistant.append(item)
            last_assistant["role"] = "assistant"
            last_assistant["content"] = correction
            prepared["messages"] = [*non_assistant, last_assistant]
    return prepared


def _supervised_tokens(record: dict[str, Any]) -> int:
    messages = record.get("messages")
    assistant_contents: list[str] = []
    if isinstance(messages, list):
        assistant_contents = [
            str(item.get("content", ""))
            for item in messages
            if isinstance(item, dict) and str(item.get("role", "")).casefold() == "assistant"
        ]
    if not assistant_contents:
        _system, _prompt, response = _dialogue(record)
        if response:
            assistant_contents = [response]
    if not assistant_contents:
        return 0
    # Matches the supervised portion of ByteTokenizer: assistant role/content/
    # separator for each answer, plus the final EOS label.
    return 1 + sum(len(content.encode("utf-8")) + 2 for content in assistant_contents)


def _coverage(record: dict[str, Any]) -> tuple[str, ...]:
    labels = _labels(record)
    ignored = {
        "accepted", "keymaker", "english-conversation", "keymaker-foundation",
        "english-foundation", "en", "held-out", "sft", "training", "verified",
    }
    useful = sorted(label for label in labels if label and label not in ignored)
    if useful:
        return tuple(useful)
    return ("keymaker-foundation",)


def _contains_anchor_prompt(
    record: dict[str, Any],
    joined_prompt_key: str,
    anchor_prompt_keys: set[str],
) -> bool:
    if joined_prompt_key in anchor_prompt_keys:
        return True
    contexts = _message_contents(record, {"system", "user"})
    if not contexts:
        contexts = [
            _text(record.get(name, "")).strip()
            for name in ("prompt", "question", "instruction", "input")
            if _text(record.get(name, "")).strip()
        ]
    context_keys = [_exact_text(value) for value in contexts]
    for anchor_key in anchor_prompt_keys:
        if not anchor_key:
            continue
        padded_anchor = f" {anchor_key} "
        allow_containment = len(anchor_key.split()) >= 2 and len(anchor_key) >= 8
        for context_key in context_keys:
            if context_key == anchor_key:
                return True
            if allow_containment and padded_anchor in f" {context_key} ":
                return True
    return False


@dataclass
class _Candidate:
    record: dict[str, Any]
    identifier: str
    source: Path
    line_number: int
    source_kind: str
    raw_hash: str
    prompt_key: str
    exact_key: str
    behavior: str
    answer_key: str
    tokens: int
    quality: tuple[int, int, int, int, int, int, float]
    coverage: tuple[str, ...]
    timestamp: float | None
    is_delta: bool = False
    selection_phase: str = "replay"
    uid: str = field(init=False)

    def __post_init__(self) -> None:
        self.uid = f"{self.source.resolve()}:{self.line_number}:{self.raw_hash}"


def _candidate_sort_key(candidate: _Candidate) -> tuple[Any, ...]:
    quality = tuple(-value for value in candidate.quality)
    return (*quality, candidate.tokens, len(_stable_json(candidate.record)), candidate.identifier, candidate.raw_hash)


def _chronology_key(candidate: _Candidate) -> tuple[Any, ...]:
    # Timestamps make this invariant to JSONL ordering.  An append-only line
    # position is the meaningful fallback for legacy episodes without one.
    return (
        1 if candidate.timestamp is not None else 0,
        candidate.timestamp if candidate.timestamp is not None else float(candidate.line_number),
        str(candidate.source.resolve()).casefold(),
        candidate.line_number,
        candidate.identifier,
        candidate.raw_hash,
    )


def _authoritative_episode(candidate: _Candidate) -> bool:
    record = candidate.record
    return bool(
        _truthy(record.get("eligible_for_training", False))
        and (
            _is_hard_correction(record)
            or _truthy(record.get("human_accepted", False))
            or _truthy(record.get("deterministic_verified", False))
            or _truthy(record.get("reference_verified", False))
        )
    )


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def _fit_quality_first(
    candidates: Sequence[_Candidate],
    remaining_tokens: int,
) -> tuple[list[_Candidate], int, dict[str, str]]:
    selected: list[_Candidate] = []
    skipped: dict[str, str] = {}
    for candidate in sorted(candidates, key=_candidate_sort_key):
        if candidate.tokens <= remaining_tokens:
            selected.append(candidate)
            remaining_tokens -= candidate.tokens
        else:
            skipped[candidate.uid] = "token_budget"
    return selected, remaining_tokens, skipped


def _fit_balanced_replay(
    candidates: Sequence[_Candidate],
    remaining_tokens: int,
    replay_budget: int,
) -> tuple[list[_Candidate], int, dict[str, str]]:
    buckets: dict[str, list[_Candidate]] = defaultdict(list)
    for candidate in candidates:
        buckets[candidate.coverage[0]].append(candidate)
    for bucket in buckets.values():
        bucket.sort(key=_candidate_sort_key)

    selected: list[_Candidate] = []
    skipped: dict[str, str] = {}
    while len(selected) < replay_budget:
        heads = [bucket[0] for bucket in buckets.values() if bucket]
        if not heads:
            break
        made_progress = False
        # One head per competency per round; quality decides the order within
        # that equally covered round.
        for candidate in sorted(heads, key=_candidate_sort_key):
            bucket = buckets[candidate.coverage[0]]
            if not bucket or bucket[0] is not candidate:
                continue
            bucket.pop(0)
            if candidate.tokens <= remaining_tokens:
                selected.append(candidate)
                remaining_tokens -= candidate.tokens
                made_progress = True
                if len(selected) >= replay_budget:
                    break
            else:
                skipped[candidate.uid] = "token_budget"
        if not made_progress and not any(buckets.values()):
            break

    for bucket in buckets.values():
        for candidate in bucket:
            skipped[candidate.uid] = "replay_budget"
    return selected, remaining_tokens, skipped


def _cap_answer_templates(
    candidates: Sequence[_Candidate],
    maximum: int,
) -> tuple[list[_Candidate], int]:
    """Bound identical targets without erasing useful competency diversity."""

    if maximum == 0:
        return [], len(candidates)
    groups: dict[str, list[_Candidate]] = defaultdict(list)
    for candidate in candidates:
        groups[candidate.answer_key].append(candidate)
    kept: list[_Candidate] = []
    dropped = 0
    for answer_key in sorted(groups):
        ranked = sorted(
            groups[answer_key],
            key=lambda candidate: (
                0 if candidate.is_delta else 1,
                *_candidate_sort_key(candidate),
            ),
        )
        selected: list[_Candidate] = []
        selected_uids: set[str] = set()
        seen_coverage: set[str] = set()
        # Preserve distinct competencies before retaining repeated contexts for
        # one identical target string.
        for candidate in ranked:
            coverage = candidate.coverage[0]
            if coverage in seen_coverage:
                continue
            selected.append(candidate)
            selected_uids.add(candidate.uid)
            seen_coverage.add(coverage)
            if len(selected) >= maximum:
                break
        if len(selected) < maximum:
            for candidate in ranked:
                if candidate.uid in selected_uids:
                    continue
                selected.append(candidate)
                selected_uids.add(candidate.uid)
                if len(selected) >= maximum:
                    break
        kept.extend(selected)
        dropped += len(ranked) - len(selected)
    return kept, dropped


def compile_curriculum(
    sources: Path | str | os.PathLike[str] | Iterable[Path | str | os.PathLike[str]],
    output_path: Path | str | os.PathLike[str],
    manifest_path: Path | str | os.PathLike[str],
    *,
    anchor_paths: Path | str | os.PathLike[str] | Iterable[Path | str | os.PathLike[str]] = (),
    scope: str | None = None,
    token_budget: int = DEFAULT_TOKEN_BUDGET,
    max_per_cluster: int = 1,
    max_per_answer: int = 4,
    delta_ids: Iterable[str] | None = None,
    replay_budget: int = DEFAULT_REPLAY_BUDGET,
    delta_sources: Path | str | os.PathLike[str] | Iterable[Path | str | os.PathLike[str]] = (),
    delta_limit: int | None = None,
) -> dict[str, Any]:
    """Compile safe representatives and return the written audit manifest.

    ``token_budget`` counts supervised byte tokens, matching EngCoder's base
    tokenizer.  ``max_per_cluster`` bounds paraphrases of one behavior.  When
    delta IDs, delta sources, or ``delta_limit`` are supplied, fresh records are
    considered first and only ``replay_budget`` older representatives can be
    added in coverage-balanced rounds.
    """

    if token_budget < 0:
        raise ValueError("token_budget must be non-negative")
    if token_budget > MAX_TOKEN_BUDGET:
        raise ValueError(f"token_budget must not exceed {MAX_TOKEN_BUDGET}")
    if max_per_cluster < 0:
        raise ValueError("max_per_cluster must be non-negative")
    if max_per_answer < 0:
        raise ValueError("max_per_answer must be non-negative")
    if replay_budget < 0:
        raise ValueError("replay_budget must be non-negative")
    if replay_budget > MAX_REPLAY_BUDGET:
        raise ValueError(f"replay_budget must not exceed {MAX_REPLAY_BUDGET}")
    if delta_limit is not None and delta_limit < 0:
        raise ValueError("delta_limit must be non-negative")
    if delta_limit is not None and delta_limit > MAX_DELTA_LIMIT:
        raise ValueError(f"delta_limit must not exceed {MAX_DELTA_LIMIT}")

    delta_paths = _resolved_paths(delta_sources)
    delta_path_set = set(delta_paths)
    source_paths = tuple(path for path in _resolved_paths(sources) if path not in delta_path_set)
    anchors = _resolved_paths(anchor_paths)
    output = Path(output_path).resolve()
    manifest_output = Path(manifest_path).resolve()
    if not source_paths and not delta_paths:
        raise ValueError("at least one source or delta source is required")
    all_inputs = {*source_paths, *delta_paths, *anchors}
    if output == manifest_output:
        raise ValueError("output_path and manifest_path must be different")
    if output in all_inputs or manifest_output in all_inputs:
        raise ValueError("compiled outputs must not overwrite append-only inputs")
    for path in (*source_paths, *delta_paths, *anchors):
        if not path.is_file():
            raise FileNotFoundError(path)

    delta_id_values: Iterable[str]
    if isinstance(delta_ids, str):
        delta_id_values = (delta_ids,)
    else:
        delta_id_values = delta_ids or ()
    requested_delta_ids = {
        _text(value) for value in delta_id_values if _text(value)
    }
    delta_mode = bool(requested_delta_ids or delta_paths or delta_limit is not None)
    reasons = Counter({reason: 0 for reason in _KNOWN_REASONS})
    input_hashes = {str(path): _sha256_file(path) for path in (*source_paths, *delta_paths, *anchors)}
    input_lines = 0
    parsed_records = 0
    stripped_weights = 0
    stripped_emoji_records = 0

    raw_entries: list[tuple[Path, int, str, dict[str, Any]]] = []
    for source_kind, paths in (("source", source_paths), ("delta", delta_paths)):
        for path in paths:
            for line_number, record, error in _iter_jsonl(path):
                input_lines += 1
                if error:
                    reasons[error] += 1
                    continue
                assert record is not None
                parsed_records += 1
                raw_entries.append((path, line_number, source_kind, record))

    # Corrections supersede by exact normalized user prompt before other
    # filtering.  An unusable newest correction therefore cannot resurrect a
    # stale answer.
    latest_corrections: dict[tuple[str, str], tuple[tuple[Any, ...], str]] = {}
    raw_uids: dict[tuple[str, int], str] = {}
    for path, line_number, _kind, record in raw_entries:
        raw_hash = _record_hash(record)
        uid = f"{path.resolve()}:{line_number}:{raw_hash}"
        raw_uids[(str(path), line_number)] = uid
        if not _is_hard_correction(record):
            continue
        cleaned_record = _clean_training_record(record)
        _system, prompt, _response = _dialogue(cleaned_record)
        prompt_key = _exact_text(prompt)
        if not prompt_key:
            continue
        correction_key = (_record_skill(cleaned_record), prompt_key)
        timestamp = _timestamp(record)
        chronology = (
            1 if timestamp is not None else 0,
            timestamp if timestamp is not None else float(line_number),
            str(path.resolve()).casefold(),
            line_number,
            str(record.get("id", "")),
            raw_hash,
        )
        if correction_key not in latest_corrections or chronology > latest_corrections[correction_key][0]:
            latest_corrections[correction_key] = (chronology, uid)

    # The immutable ledger marks the first few human approvals in a behavior
    # batch as retrieval-only. Once four distinct, safe behaviors exist for a
    # skill, all four become one eligible batch; otherwise the compiler would
    # train only the threshold-crossing row and silently discard its context.
    human_behaviors: dict[str, set[str]] = defaultdict(set)
    for _path, _line_number, _source_kind, record in raw_entries:
        if not (
            _truthy(record.get("human_accepted", False))
            and not _truthy(record.get("human_rejected", False))
            and not _truthy(record.get("deterministic_verified", False))
            and not _truthy(record.get("reference_verified", False))
        ):
            continue
        probe = _clean_training_record(record)
        probe.pop("eligible_for_training", None)
        system, prompt, response = _dialogue(probe)
        if _filter_reason(probe, system, prompt, response, scope) is not None:
            continue
        skill = _normalize_label(probe.get("skill", probe.get("domain", "general")))
        human_behaviors[skill].add(behavior_signature(probe, response))
    qualified_human_skills = {
        skill for skill, behaviors in human_behaviors.items() if len(behaviors) >= 4
    }

    candidates: list[_Candidate] = []
    seen_requested_ids: set[str] = set()
    for path, line_number, source_kind, record in raw_entries:
        raw_hash = _record_hash(record)
        uid = raw_uids[(str(path), line_number)]
        identifier = _text(record.get("id", "")).strip() or f"compiled-{raw_hash[:16]}"
        if identifier in requested_delta_ids:
            seen_requested_ids.add(identifier)
        raw_system, raw_prompt, raw_response = _dialogue(record)
        had_emoji = _has_emoji("\n".join((raw_system, raw_prompt, raw_response)))
        cleaned_record = _clean_training_record(record)
        record_skill = _normalize_label(
            cleaned_record.get("skill", cleaned_record.get("domain", "general"))
        )
        human_batch_promoted = bool(
            record_skill in qualified_human_skills
            and _truthy(cleaned_record.get("human_accepted", False))
            and not _truthy(cleaned_record.get("human_rejected", False))
            and not _truthy(cleaned_record.get("deterministic_verified", False))
            and not _truthy(cleaned_record.get("reference_verified", False))
        )
        if human_batch_promoted:
            cleaned_record["eligible_for_training"] = True
        system, prompt, response = _dialogue(cleaned_record)
        prompt_key = _exact_text(prompt)
        correction = latest_corrections.get((_record_skill(cleaned_record), prompt_key))
        if correction is not None and uid != correction[1]:
            reasons["superseded_by_correction"] += 1
            continue
        reason = _filter_reason(
            cleaned_record,
            system,
            prompt,
            response,
            scope,
            had_emoji=had_emoji,
        )
        if reason is not None:
            reasons[reason] += 1
            continue

        prepared = _prepared_record(cleaned_record, identifier)
        if "curriculum_weight" in record:
            stripped_weights += 1
        if had_emoji:
            stripped_emoji_records += 1
        _prepared_system, prepared_prompt, prepared_response = _dialogue(prepared)
        behavior = behavior_signature(prepared, prepared_response)
        exact_material = _exact_training_material(prepared)
        exact_key = hashlib.sha256(exact_material.encode("utf-8")).hexdigest()
        candidate = _Candidate(
            record=prepared,
            identifier=identifier,
            source=path,
            line_number=line_number,
            source_kind=source_kind,
            raw_hash=raw_hash,
            prompt_key=prompt_key,
            exact_key=exact_key,
            behavior=behavior,
            answer_key=(
                prepared_response
                if behavior.startswith("exact-copy:")
                else _exact_text(prepared_response)
            ),
            tokens=_supervised_tokens(prepared),
            quality=_quality(cleaned_record),
            coverage=_coverage(cleaned_record),
            timestamp=_timestamp(cleaned_record),
            is_delta=(source_kind == "delta"),
        )
        candidates.append(candidate)

    # Held-out and anchor records reserve whole behavior clusters.  Anchors do
    # not need training eligibility; they only need a valid English pair.
    anchor_signatures: set[str] = set()
    anchor_prompt_keys: set[str] = set()
    anchor_records = 0
    for path in anchors:
        for _line_number, record, error in _iter_jsonl(path):
            if error or record is None:
                continue
            system, prompt, response = _dialogue(record)
            cleaned_record = _clean_training_record(record)
            system, prompt, response = _dialogue(cleaned_record)
            if not prompt or not response or not _is_english(
                cleaned_record,
                "\n".join((system, prompt, response)),
                allow_code=_scope_allows_code(scope),
            ):
                continue
            anchor_records += 1
            anchor_signatures.add(behavior_signature(cleaned_record, response))
            anchor_prompt_keys.add(_exact_text(prompt))
    if anchor_prompt_keys:
        kept: list[_Candidate] = []
        for candidate in candidates:
            # Reserve direct held-out text wherever it appears in model input,
            # including an individual turn or a system wrapper. A model must
            # still see different examples of the competency to generalize.
            if _contains_anchor_prompt(
                candidate.record,
                candidate.prompt_key,
                anchor_prompt_keys,
            ):
                reasons["anchor_overlap"] += 1
            else:
                kept.append(candidate)
        candidates = kept

    # A requested identifier denotes one newest eligible episode. Duplicate
    # ledger IDs must not silently relabel an arbitrary amount of history as a
    # fresh delta or make the audit manifest ambiguous.
    requested_groups: dict[str, list[_Candidate]] = defaultdict(list)
    for candidate in candidates:
        if candidate.identifier in requested_delta_ids:
            requested_groups[candidate.identifier].append(candidate)
    requested_winners: set[str] = set()
    for identifier in sorted(requested_groups):
        group = requested_groups[identifier]
        newest = max(group, key=_chronology_key)
        newest.is_delta = True
        requested_winners.add(newest.uid)
        reasons["duplicate_identifier"] += len(group) - 1
    if requested_groups:
        candidates = [
            candidate
            for candidate in candidates
            if candidate.identifier not in requested_groups or candidate.uid in requested_winners
        ]

    if delta_limit is not None and delta_limit > 0:
        newest = sorted(
            (candidate for candidate in candidates if _authoritative_episode(candidate)),
            key=_chronology_key,
            reverse=True,
        )[:delta_limit]
        for candidate in newest:
            candidate.is_delta = True

    # Exact duplicates are collapsed before canonical behavior clusters.  A
    # requested fresh record wins an otherwise identical tie.
    exact_groups: dict[str, list[_Candidate]] = defaultdict(list)
    for candidate in candidates:
        exact_groups[candidate.exact_key].append(candidate)
    exact_unique: list[_Candidate] = []
    for key in sorted(exact_groups):
        group = exact_groups[key]
        winner = min(group, key=lambda candidate: (0 if candidate.is_delta else 1, *_candidate_sort_key(candidate)))
        exact_unique.append(winner)
        reasons["exact_duplicate"] += len(group) - 1

    clusters: dict[str, list[_Candidate]] = defaultdict(list)
    for candidate in exact_unique:
        clusters[candidate.behavior].append(candidate)
    representatives: list[_Candidate] = []
    for behavior in sorted(clusters):
        group = clusters[behavior]
        ranked = sorted(group, key=lambda candidate: (0 if candidate.is_delta else 1, *_candidate_sort_key(candidate)))
        representatives.extend(ranked[:max_per_cluster])
        reasons["canonical_duplicate"] += max(0, len(ranked) - max_per_cluster)

    representatives, answer_duplicates = _cap_answer_templates(
        representatives,
        max_per_answer,
    )
    reasons["answer_duplicate"] += answer_duplicates

    delta_representatives = [candidate for candidate in representatives if candidate.is_delta]
    replay_representatives = [candidate for candidate in representatives if not candidate.is_delta]
    selected_delta: list[_Candidate] = []
    selected_replay: list[_Candidate] = []
    selection_skips: dict[str, str] = {}
    remaining_tokens = token_budget
    if delta_mode:
        selected_delta, remaining_tokens, skipped = _fit_quality_first(delta_representatives, remaining_tokens)
        selection_skips.update(skipped)
        selected_replay, remaining_tokens, skipped = _fit_balanced_replay(
            replay_representatives,
            remaining_tokens,
            replay_budget,
        )
        selection_skips.update(skipped)
    else:
        selected_replay, remaining_tokens, skipped = _fit_quality_first(representatives, remaining_tokens)
        selection_skips.update(skipped)
    for reason in selection_skips.values():
        reasons[reason] += 1

    for candidate in selected_delta:
        candidate.selection_phase = "delta"
    selected = selected_delta + selected_replay
    # The physical curriculum order is independent of source argument order and
    # of greedy budget traversal.
    selected.sort(key=lambda candidate: (candidate.behavior, candidate.identifier, candidate.raw_hash))
    selected_delta_ids = [candidate.identifier for candidate in selected if candidate.selection_phase == "delta"]
    replay_selected_ids = [candidate.identifier for candidate in selected if candidate.selection_phase != "delta"]
    selected_ids = [candidate.identifier for candidate in selected]

    rows = [_stable_json(candidate.record) for candidate in selected]
    output_bytes = (("\n".join(rows) + "\n") if rows else "").encode("utf-8")
    output_hash = hashlib.sha256(output_bytes).hexdigest()
    _atomic_write(output, output_bytes)

    combined_input_digest = hashlib.sha256()
    for path_text, digest in sorted(input_hashes.items(), key=lambda item: item[0].casefold()):
        combined_input_digest.update(path_text.encode("utf-8"))
        combined_input_digest.update(b"\0")
        combined_input_digest.update(digest.encode("ascii"))
        combined_input_digest.update(b"\n")

    coverage_counts = Counter(
        label
        for candidate in selected
        for label in candidate.coverage
    )
    counts = {
        "input_lines": input_lines,
        "parsed_records": parsed_records,
        "eligible_records": len(candidates),
        "anchor_records": anchor_records,
        "anchor_behavior_clusters": len(anchor_signatures),
        "behavior_clusters": len(clusters),
        "representatives": len(representatives),
        "answer_templates": len({candidate.answer_key for candidate in representatives}),
        "delta_candidates": len(delta_representatives),
        "replay_candidates": len(replay_representatives),
        "selected_records": len(selected),
        "selected_supervised_tokens": sum(candidate.tokens for candidate in selected),
        "delta_selected": len(selected_delta_ids),
        "replay_selected": len(replay_selected_ids),
        "stripped_curriculum_weight": stripped_weights,
        "stripped_emoji_records": stripped_emoji_records,
        "qualified_human_skills": len(qualified_human_skills),
    }
    manifest: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "policy": COMPILER_POLICY,
        "scope": scope,
        "sources": [str(path) for path in source_paths],
        "delta_sources": [str(path) for path in delta_paths],
        "anchors": [str(path) for path in anchors],
        "budgets": {
            "supervised_tokens": token_budget,
            "max_per_cluster": max_per_cluster,
            "max_per_answer": max_per_answer,
            "replay_records": replay_budget if delta_mode else None,
            "delta_limit": delta_limit,
        },
        "counts": counts,
        "reasons": {reason: reasons[reason] for reason in _KNOWN_REASONS},
        "selected_ids": selected_ids,
        "delta_selected_ids": selected_delta_ids,
        "replay_selected_ids": replay_selected_ids,
        "requested_delta_ids": sorted(requested_delta_ids),
        "unresolved_delta_ids": sorted(requested_delta_ids - seen_requested_ids),
        "coverage": dict(sorted(coverage_counts.items())),
        "hash": output_hash,
        "hashes": {
            "output_sha256": output_hash,
            "combined_input_sha256": combined_input_digest.hexdigest(),
            "inputs_sha256": dict(sorted(input_hashes.items(), key=lambda item: item[0].casefold())),
        },
    }
    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    _atomic_write(manifest_output, manifest_bytes)

    # Detect an external concurrent rewrite without ever modifying a ledger.
    final_hashes = {str(path): _sha256_file(path) for path in (*source_paths, *delta_paths, *anchors)}
    if final_hashes != input_hashes:
        raise RuntimeError("an append-only input changed while the curriculum was being compiled")
    return manifest


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="*", type=Path, help="append-only source JSONL")
    parser.add_argument("--input", "--source", dest="extra_inputs", action="append", type=Path, default=[])
    parser.add_argument("--output", required=True, type=Path, help="selected compact JSONL")
    parser.add_argument("--manifest", required=True, type=Path, help="audit manifest JSON")
    parser.add_argument("--anchor", dest="anchors", action="append", type=Path, default=[])
    parser.add_argument("--scope")
    parser.add_argument("--token-budget", "--max-supervised-tokens", type=int, default=DEFAULT_TOKEN_BUDGET)
    parser.add_argument("--max-per-cluster", "--per-cluster-budget", type=int, default=1)
    parser.add_argument("--max-per-answer", "--per-answer-budget", type=int, default=4)
    parser.add_argument("--delta-id", action="append", default=[])
    parser.add_argument("--delta-source", action="append", type=Path, default=[])
    parser.add_argument("--delta-limit", type=int)
    parser.add_argument("--replay-budget", "--replay-limit", type=int, default=DEFAULT_REPLAY_BUDGET)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    sources = [*arguments.inputs, *arguments.extra_inputs]
    if not sources and not arguments.delta_source:
        raise SystemExit("at least one input JSONL is required")
    manifest = compile_curriculum(
        sources,
        arguments.output,
        arguments.manifest,
        anchor_paths=arguments.anchors,
        scope=arguments.scope,
        token_budget=arguments.token_budget,
        max_per_cluster=arguments.max_per_cluster,
        max_per_answer=arguments.max_per_answer,
        delta_ids=arguments.delta_id,
        replay_budget=arguments.replay_budget,
        delta_sources=arguments.delta_source,
        delta_limit=arguments.delta_limit,
    )
    print(json.dumps({
        "output": str(arguments.output),
        "manifest": str(arguments.manifest),
        "selected_records": manifest["counts"]["selected_records"],
        "output_sha256": manifest["hash"],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
