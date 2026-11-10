"""Bounded C++ repair research, verification, training, and teacher evaluation."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import struct
import subprocess
from tempfile import TemporaryDirectory
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener


SOURCES = {
    "alg.clamp": ("https://eel.is/c%2B%2Bdraft/alg.clamp", "Bounded value",
                  "For ordered bounds, retain the input inside the interval and saturate outside it."),
    "expr.rel": ("https://eel.is/c++draft/expr.rel", "Relational operators",
                 "Choose strict or inclusive comparisons according to which endpoints are valid."),
    "basic.fundamental": ("https://eel.is/c++draft/basic.fundamental", "Fundamental types",
                          "Unsigned arithmetic wraps modulo its width, so widen before adding when checking a limit."),
}
FAMILIES = ("clamp", "index", "closed", "capacity")
TOKENS = ("<=", ">=", "<", ">", "&&", "+", "-", "return", "0", "1", "static_cast", "a", "b", "x")
DIM = len(FAMILIES) * len(TOKENS)
MODEL = struct.Struct(f"<4s{DIM}f")
MAX_MODEL_BYTES = 256
LETTERS = "ABCD"
U32_MAX = (1 << 32) - 1


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        return None


def research() -> list[dict]:
    """Look up contract topics in a fixed primary-source index; retain only provenance and authored facts."""
    opener = build_opener(NoRedirect())
    records = []
    for section, (url, marker, fact) in SOURCES.items():
        with opener.open(Request(url, headers={"User-Agent": "EngCoderBoundaryLab/1"}), timeout=12) as response:
            data = response.read(128 * 1024 + 1)
            if response.headers.get_content_type() != "text/html" or len(data) > 128 * 1024:
                raise ValueError(f"unexpected source response for {section}")
        if section.encode() not in data or marker.encode() not in data:
            raise ValueError(f"reference identity check failed for {section}")
        records.append({"section": section, "url": url, "sha256": hashlib.sha256(data).hexdigest(),
                        "retrieved_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                        "fact": fact, "use": "reference only; raw content excluded"})
    return records


@dataclass(frozen=True)
class Case:
    key: str
    family: str
    instruction: str
    signature: str
    original: str
    choices: tuple[str, ...]
    probes: tuple[tuple[int, int, int, int], ...]

    def prompt(self) -> str:
        options = "\n".join(f"{letter}. {code}" for letter, code in zip(LETTERS, self.choices))
        return (f"{self.instruction}\nFunction: {self.signature}\nCurrent body: {self.original}\n"
                f"Choose the replacement body that satisfies the contract. Reply with one letter A, B, C, or D.\n{options}")


def make_case(family: str, i: int, held_out: bool, rng: random.Random) -> Case:
    lower = rng.randrange(400, 4000) if held_out else rng.randrange(-70, 71)
    upper = lower + rng.randrange(1, 99)
    count = rng.randrange(300, 4000) if held_out else rng.randrange(0, 90)
    if i == 0:
        upper = lower
        count = 0
    noun = ("servo", "sample", "rack", "job") if held_out else ("slider", "pixel", "sensor", "queue")
    if family == "clamp":
        signature = "int repair(int x, int a, int b)"
        instruction = f"For a {noun[0]}, clamp x to the inclusive interval [a,b], with a <= b."
        choices = ["if (x < a) return a; if (x > b) return b; return x;",
                   "if (x < a) return a; if (x > b) return b-1; return x;",
                   "if (x < a) return a+1; if (x > b) return b; return x;",
                   "if (x < a) return x; if (x > b) return b; return x;"]
        if held_out:
            choices = [f"{high}; {low};{tail}" for low, high, tail in
                       (option.split(";", 2) for option in choices)]
            choices = [option.replace("b-1", "b-2").replace("a+1", "a+2") for option in choices]
        points = [lower-1, lower, (lower+upper)//2, upper, upper+1, -2147483647-1, 2147483647]
        probes = [(x, lower, upper, min(max(x, lower), upper)) for x in points]
    elif family == "index":
        signature = "bool repair(int x, int a, int b)"
        instruction = f"A {noun[1]} at signed zero-based index x is valid for a elements. Parameter b is unused."
        choices = ["return x >= 0 && x < a;", "return x >= 0 && x <= a;",
                   "return x > 0 && x < a;", "return x < a;"]
        if held_out:
            choices = reverse_conjunctions(choices)
        points = [-1, 0, 1, count-1, count, count+1, -2147483647-1, 2147483647]
        probes = [(x, n, 0, int(0 <= x < n)) for n in (count, count+1) for x in points+[count+2]]
    elif family == "closed":
        signature = "bool repair(int x, int a, int b)"
        instruction = f"Accept the {noun[2]} reading x exactly when it is between a and b, including both limits."
        choices = ["return x >= a && x <= b;", "return x > a && x <= b;",
                   "return x >= a && x < b;", "return x >= a;"]
        if held_out:
            choices = reverse_conjunctions(choices)
        points = [lower-1, lower, (lower+upper)//2, upper, upper+1, -2147483647-1, 2147483647]
        probes = [(x, lower, upper, int(lower <= x <= upper)) for x in points]
    else:
        signature = "bool repair(std::uint32_t x, std::uint32_t a, std::uint32_t b)"
        instruction = f"A {noun[3]} requests a items with x already used. Accept when the mathematical sum is at most b."
        choices = ["return static_cast<std::uint64_t>(x) + a <= b;",
                   "return x + a <= b;",
                   "return static_cast<std::uint64_t>(x) + a < b;",
                   "return static_cast<std::uint64_t>(x) + a >= b;"]
        if held_out:
            choices = [option.replace("static_cast<std::uint64_t>(x) + a", "a + static_cast<std::uint64_t>(x)")
                       .replace("return x + a", "return a + x") for option in choices]
        samples = [(0,0,0), (count,1,count+1), (count,2,count+1), (count,0,count-1 if count else 0),
                   (U32_MAX,1,U32_MAX), (U32_MAX,0,U32_MAX), (1,U32_MAX,U32_MAX)]
        probes = [(x, a, b, int(x+a <= b)) for x, a, b in samples]
    original = choices[1 + rng.randrange(3)]
    rng.shuffle(choices)
    return Case(f"{'fresh' if held_out else 'train'}-{family}-{i:02}", family, instruction, signature,
                original, tuple(choices), tuple(probes))


def reverse_conjunctions(options: list[str]) -> list[str]:
    reversed_options = []
    for option in options:
        if " && " in option:
            left, right = option.removeprefix("return ").removesuffix(";").split(" && ")
            option = f"return ({right}) && ({left});"
        reversed_options.append(option)
    return reversed_options


def make_split(held_out: bool) -> list[Case]:
    rng = random.Random(81731 if held_out else 24091)
    return [make_case(family, i, held_out, rng) for family in FAMILIES
            for i in range(4 if held_out else 16)]


def command(args: list[str], cwd: Path, seconds: int = 180) -> str:
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=seconds)
    if result.returncode:
        raise RuntimeError(f"{args[0]} exited {result.returncode}\n{(result.stdout+result.stderr)[-6000:]}")
    return result.stdout


def compile_verify(cases: list[Case], out: Path) -> dict[str, list[int]]:
    """Compile authored code choices only. Teacher text never reaches a compiler."""
    source = ['#include <cstdint>', '#include <iostream>']
    main = ['int main() {']
    for index, case in enumerate(cases):
        for choice, code in enumerate(case.choices + (case.original,)):
            name = f"case_{index}_{choice}"
            source.append(case.signature.replace("repair(", name+"(") + " {" + code + "}")
            errors = [f"({name}({x}LL,{a}LL,{b}LL)!={expected})" for x, a, b, expected in case.probes]
            main.append(f'std::cout << "{case.key} {choice} " << ({" + ".join(errors)}) << "\\n";')
    main.append("}")
    project = out / "verifier"
    project.mkdir()
    (project / "verify.cpp").write_text("\n".join(source + main), encoding="utf-8")
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\nproject(BoundaryVerifier LANGUAGES CXX)\n"
        "add_executable(boundary_verify verify.cpp)\n"
        "target_compile_features(boundary_verify PRIVATE cxx_std_23)\n"
        'set_target_properties(boundary_verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")\n',
        encoding="utf-8")
    command(["cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"], project)
    command(["cmake", "--build", "build", "--config", "Release", "--parallel", "1"], project)
    exe = project / "build/bin/Release" / ("boundary_verify.exe" if os.name == "nt" else "boundary_verify")
    records = {case.key: [-1] * 5 for case in cases}
    for line in command([str(exe)], project, 15).splitlines():
        key, choice, failures = line.split()
        records[key][int(choice)] = int(failures)
    if any(row.count(0) != 1 or row[4] <= 0 or min(row) < 0 for row in records.values()):
        raise ValueError("verifier requires a unique repair and a reproduced defect for each case")
    return records


def features(family: str, code: str) -> list[float]:
    words = re.findall(r"<=|>=|&&|[<>+\-]|[A-Za-z_0-9]+", code)
    group = FAMILIES.index(family)
    return [float(words.count(token)) if lane == group else 0.0
            for lane in range(len(FAMILIES)) for token in TOKENS]


def score(weights: tuple | list, row: list[float]) -> float:
    return sum(w*x for w, x in zip(weights, row))


def select(weights: tuple | list, family: str, options: tuple[str, ...]) -> int:
    return max(range(len(options)), key=lambda i: score(weights, features(family, options[i])))


def train(cases: list[Case], labels: dict[str, int]) -> list[float]:
    """Fit a shared linear softmax ranker using only compiler-verified training labels."""
    weights = [0.0] * DIM
    rng = random.Random(23019)
    order = list(cases)
    rows = {case.key: [features(case.family, code) for code in case.choices] for case in cases}
    for _ in range(24):
        rng.shuffle(order)
        for case in order:
            vectors = rows[case.key]
            logits = [score(weights, row) for row in vectors]
            exp = [math.exp(value - max(logits)) for value in logits]
            target = vectors[labels[case.key]]
            for j in range(DIM):
                expected = sum(p*row[j] for p, row in zip(exp, vectors)) / sum(exp)
                weights[j] += 0.12 * (target[j] - expected - 0.001 * weights[j])
    return weights


def teacher_headers() -> dict[str, str]:
    token = os.getenv("ENGCODER_BOUNDARY_TEACHER_TOKEN", "")
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


def validate_teacher(endpoint: str, model: str) -> None:
    if not re.fullmatch(r"http://127\.0\.0\.1:[0-9]{1,5}", endpoint) or not 0 < int(endpoint.rsplit(":", 1)[1]) < 65536:
        raise ValueError("teacher endpoint must be HTTP loopback on a valid port")
    if not re.fullmatch(r"[A-Za-z0-9._/:-]{1,160}", model):
        raise ValueError("invalid teacher model identifier")


def teacher_available(endpoint: str, model: str) -> bool:
    request = Request(endpoint + "/v1/models", headers=teacher_headers())
    try:
        with build_opener(NoRedirect(), ProxyHandler({})).open(request, timeout=3) as response:
            models = json.loads(response.read(128 * 1024))["data"]
        return any(isinstance(entry, dict) and entry.get("id") == model for entry in models)
    except (OSError, ValueError, TypeError, KeyError):
        return False


def query_teacher(case: Case, endpoint: str, model: str, constrained: bool) -> dict:
    payload = {
        "model": model, "messages": [{"role": "user", "content": case.prompt()}],
        "temperature": 0, "max_tokens": 8 if constrained else 2048,
    }
    if constrained:
        # llama.cpp-only comparison mode. The grammar has four equally
        # available letters and exposes no answer or test result.
        payload.update({"grammar": "root ::= [ABCD]", "chat_template_kwargs": {"enable_thinking": False}})
    request = Request(endpoint + "/v1/chat/completions", headers=teacher_headers(), data=json.dumps(payload).encode())
    try:
        with build_opener(NoRedirect(), ProxyHandler({})).open(request, timeout=30) as response:
            if response.status != 200:
                return {"error": f"HTTP {response.status}"}
            payload = json.loads(response.read(128 * 1024))
        choice = payload["choices"][0]
        answer = choice["message"]["content"]
        match = re.fullmatch(r"\s*(?:Answer:\s*)?(?:\*\*)?([A-D])\.?\s*(?:\*\*)?\s*", answer or "")
        finish = choice.get("finish_reason", "")
        usable = match and finish == "stop" and not choice["message"].get("tool_calls")
        return {"choice": match.group(1) if usable else None, "finish": finish, "usage": payload.get("usage")}
    except (OSError, ValueError, TypeError, KeyError, IndexError, AttributeError) as error:
        return {"error": type(error).__name__}


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def prompt_fingerprint(cases: list[Case]) -> str:
    return hashlib.sha256(json.dumps([case.prompt() for case in cases], ensure_ascii=True).encode()).hexdigest()


def run(out: Path) -> dict:
    out.mkdir(parents=True)
    references = research()
    write_json(out / "references.json", references)
    training = make_split(False)
    fresh = make_split(True)
    receipts = compile_verify(training + fresh, out)
    labels = {case.key: receipts[case.key].index(0) for case in training}
    # Changing only the option order or probe values does not create a new
    # example for this ranker. Keep those compiler receipts, then fit once
    # per distinct option set.
    unique = {}
    for case in training:
        key = (case.family, tuple(sorted(case.choices)), case.choices[labels[case.key]])
        unique.setdefault(key, case)
    weights = train(list(unique.values()), labels)
    packed = MODEL.pack(b"ECB1", *weights)
    if len(packed) > MAX_MODEL_BYTES:
        raise ValueError("repair ranker exceeded its byte budget")
    model_path = out / "boundary-ranker.bin"
    model_path.write_bytes(packed)
    deployed = read_weights(model_path)
    rows = []
    for case in fresh:
        grade = receipts[case.key]
        chosen = select(deployed, case.family, case.choices)
        rows.append({"id": case.key, "family": case.family, "prompt": case.prompt(), "chosen": LETTERS[chosen],
                     "correct": LETTERS[grade.index(0)], "chosen_cpp": case.choices[chosen],
                     "original_cpp": case.original, "probe_failures": grade, "probe_count": len(case.probes)})
    # Hold-out results never enter training; the selector is reloaded from its bounded artifact.
    report = {"scope": "rank authored C++ boundary repair options", "verified_training_cases": len(training),
              "distinct_training_examples": len(unique),
              "held_out_cases": len(fresh), "parameters": DIM, "model_bytes": len(packed),
              "byte_budget": MAX_MODEL_BYTES, "sha256": hashlib.sha256(packed).hexdigest(),
              "held_out_prompt_sha256": prompt_fingerprint(fresh),
              "untrained_first_choice_pass": sum(row["probe_failures"][0] == 0 for row in rows),
              "trained_pass": sum(row["chosen"] == row["correct"] for row in rows),
              "exhaustive_verifier_pass": len(rows), "candidates": rows,
              "active_weights_changed": False}
    write_json(out / "training-evidence.json", [
        {"id": case.key, "family": case.family, "prompt": case.prompt(),
         "verified_repair": case.choices[labels[case.key]], "probe_failures": receipts[case.key],
         "reference": {"clamp": "alg.clamp", "index": "expr.rel", "closed": "expr.rel",
                       "capacity": "basic.fundamental"}[case.family]} for case in training])
    write_json(out / "report.json", report)
    return report


def read_weights(path: Path, expected_sha256: str | None = None) -> tuple:
    with path.open("rb") as stream:
        packed = stream.read(MODEL.size + 1)
    if len(packed) != MODEL.size:
        raise ValueError("invalid boundary model size")
    if expected_sha256 is not None and hashlib.sha256(packed).hexdigest() != expected_sha256:
        raise ValueError("boundary model checksum mismatch")
    magic, *weights = MODEL.unpack(packed)
    if magic != b"ECB1" or not all(math.isfinite(value) for value in weights):
        raise ValueError("invalid boundary model")
    return tuple(weights)


def load_candidate(path: Path) -> tuple:
    manifest = json.loads(path.with_name("report.json").read_text(encoding="utf-8"))
    digest = manifest.get("sha256") if isinstance(manifest, dict) else None
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("missing boundary model checksum in report.json")
    return read_weights(path, digest)


def route_case(case: Case, weights: tuple, grades: list[int], teachers: list[list[str]]) -> dict:
    """Try the smallest lane first and stop as soon as a choice passes the independent probes."""
    chosen = select(weights, case.family, case.choices)
    result = {"case": case.key, "verified": False, "attempts": []}

    def record(lane: str, letter: str, **detail) -> bool:
        index = LETTERS.index(letter)
        passed = grades[index] == 0
        result["attempts"].append({"lane": lane, "choice": letter, "probe_failures": grades[index],
                                   "status": "passed" if passed else "failed-probes", **detail})
        if passed:
            result.update(verified=True, selected_lane=lane, repair=case.choices[index])
        return passed

    if record("boundary-ranker", LETTERS[chosen]):
        return result
    for endpoint, model in teachers:
        if not teacher_available(endpoint, model):
            result["attempts"].append({"lane": model, "status": "unavailable"})
            continue
        answer = query_teacher(case, endpoint, model, constrained=False)
        if not answer.get("choice"):
            result["attempts"].append({"lane": model, "status": "no-usable-choice", **answer})
        elif record(model, answer["choice"], finish=answer["finish"]):
            break
    return result


def compare(run_dir: Path, endpoint: str, model: str, constrained: bool = False) -> dict:
    validate_teacher(endpoint, model)
    report = json.loads((run_dir / "report.json").read_text(encoding="utf-8"))
    cases = make_split(True)
    scores = {row["id"]: row["correct"] for row in report["candidates"]}
    if [row["prompt"] for row in report["candidates"]] != [case.prompt() for case in cases]:
        raise ValueError("the run does not match the current held-out prompt split")
    outcomes = []
    for case in cases:
        result = query_teacher(case, endpoint, model, constrained)
        outcomes.append({"id": case.key, **result, "passed": result.get("choice") == scores[case.key]})
    summary = {"model": model, "attempted": len(cases), "passed": sum(row["passed"] for row in outcomes),
               "answered": sum(row.get("choice") in LETTERS for row in outcomes if row.get("choice")),
               "mode": "constrained-choice" if constrained else "plain-prompt",
               "token_limit": 8 if constrained else 2048,
               "truncated": sum(row.get("finish") == "length" for row in outcomes),
               "transport_errors": sum("error" in row for row in outcomes),
               "held_out_prompt_sha256": prompt_fingerprint(cases),
               "results": outcomes}
    file_id = hashlib.sha256(model.encode()).hexdigest()[:12] + ("-choice" if constrained else "")
    write_json(run_dir / f"teacher-{file_id}.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    train_mode = modes.add_parser("train")
    train_mode.add_argument("output", type=Path)
    train_mode.add_argument("--teacher", action="append", nargs=2, metavar=("LOOPBACK_URL", "MODEL"), default=[])
    teacher_mode = modes.add_parser("teacher")
    teacher_mode.add_argument("output", type=Path)
    teacher_mode.add_argument("endpoint")
    teacher_mode.add_argument("model")
    teacher_mode.add_argument("--constrained", action="store_true",
                              help="Use the llama.cpp A/B/C/D decoder grammar.")
    suggest_mode = modes.add_parser("suggest")
    suggest_mode.add_argument("model_file", type=Path)
    suggest_mode.add_argument("family", choices=FAMILIES)
    suggest_mode.add_argument("options", nargs=4)
    route_mode = modes.add_parser("route", help="Replay a held-out case with freshly compiled probes.")
    route_mode.add_argument("output", type=Path)
    held_out = {case.key: case for case in make_split(True)}
    route_mode.add_argument("case", choices=held_out)
    route_mode.add_argument("--fallback", action="append", nargs=2, default=[], metavar=("LOOPBACK_URL", "MODEL"),
                            help="Add a preconfigured local model in ascending capability order (up to two).")
    args = parser.parse_args()
    if args.mode == "train":
        if len(args.teacher) > 2:
            raise ValueError("at most two teacher comparisons per run")
        for endpoint, model in args.teacher:
            validate_teacher(endpoint, model)
        result = run(args.output.resolve())
        result["teachers"] = [
            {key: value for key, value in compare(args.output.resolve(), endpoint, model).items() if key != "results"}
            for endpoint, model in args.teacher]
        write_json(args.output.resolve() / "report.json", result)
    elif args.mode == "teacher":
        result = compare(args.output.resolve(), args.endpoint, args.model, args.constrained)
    elif args.mode == "route":
        if len(args.fallback) > 2 or len({tuple(pair) for pair in args.fallback}) != len(args.fallback):
            raise ValueError("provide at most two distinct fallback models")
        for endpoint, model in args.fallback:
            validate_teacher(endpoint, model)
        case = held_out[args.case]
        weights = load_candidate(args.output / "boundary-ranker.bin")
        with TemporaryDirectory(prefix="verify-route-", dir=args.output.resolve()) as directory:
            grades = compile_verify([case], Path(directory))[case.key]
        result = route_case(case, weights, grades, args.fallback)
    else:
        if any(len(option) > 512 or not option.isascii() for option in args.options):
            raise ValueError("repair options must be short ASCII snippets")
        index = select(load_candidate(args.model_file), args.family, tuple(args.options))
        result = {"suggestion": LETTERS[index], "cpp": args.options[index], "verified": False}
    print(json.dumps({key: value for key, value in result.items() if key not in {"candidates", "results"}}))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"Boundary lab: {error}") from None
