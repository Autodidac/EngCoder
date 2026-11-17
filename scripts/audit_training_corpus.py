"""Read-only coverage audit. Counts and author scores are not competence evidence."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def audit(path: Path) -> dict:
    raw = path.read_bytes()
    domains, tags, answers, scores = Counter(), Counter(), Counter(), Counter()
    records = assistant_bytes = 0
    for line_number, line in enumerate(raw.decode("utf-8-sig").splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
            if not isinstance(row, dict):
                raise ValueError("record must be an object")
            messages = row.get("messages", [])
            if not isinstance(messages, list) or any(not isinstance(m, dict) for m in messages):
                raise ValueError("messages must be an array of objects")
            row_tags = row.get("tags", [])
            if not isinstance(row_tags, list) or any(not isinstance(t, str) for t in row_tags):
                raise ValueError("tags must be an array of strings")
            domain = row.get("domain", "unspecified")
            if not isinstance(domain, str):
                raise ValueError("domain must be a string")
            for message in messages:
                if message.get("role") != "assistant":
                    continue
                content = message.get("content", "")
                if not isinstance(content, str):
                    raise ValueError("assistant content must be a string")
                answers[content] += 1
                assistant_bytes += len(content.encode("utf-8"))
            domains[domain] += 1
            tags.update(set(row_tags))
            if "score" in row:
                scores[json.dumps(row["score"], sort_keys=True, ensure_ascii=False)] += 1
            records += 1
        except (ValueError, TypeError) as error:
            raise ValueError(f"{path}:{line_number}: {error}") from error
    return {
        "file": path.name, "sha256": hashlib.sha256(raw).hexdigest(),
        "bytes": len(raw), "records": records,
        "domains": dict(sorted(domains.items())), "overlapping_tags": dict(sorted(tags.items())),
        "assistant_utf8_bytes": assistant_bytes, "distinct_exact_answers": len(answers),
        "repeated_answer_occurrences": sum(count - 1 for count in answers.values()),
        "score_patterns": [{"value": json.loads(value), "records": count}
                           for value, count in scores.most_common()],
        "interpretation": "Tags overlap. UTF-8 bytes are not token counts. Exact repetition is not semantic redundancy. Scores are unverified metadata, not measured test results. This audit does not admit data or demonstrate learning.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    print(json.dumps([audit(path) for path in args.files], indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
