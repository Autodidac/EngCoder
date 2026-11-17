import json
from pathlib import Path
import tempfile
import unittest

from audit_training_corpus import audit


class CorpusAuditTests(unittest.TestCase):
    def test_counts_are_exact_and_source_is_unchanged(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "data.jsonl"
            row = {"domain": "english", "tags": ["conversation", "conversation"],
                   "messages": [{"role": "user", "content": "Hello"},
                                {"role": "assistant", "content": "Hi!"}], "score": {"tests": .95}}
            raw = ((json.dumps(row) + "\n") * 2).encode()
            path.write_bytes(raw)
            report = audit(path)
            self.assertEqual(report["records"], 2)
            self.assertEqual(report["overlapping_tags"], {"conversation": 2})
            self.assertEqual(report["distinct_exact_answers"], 1)
            self.assertEqual(report["repeated_answer_occurrences"], 1)
            self.assertEqual(report["assistant_utf8_bytes"], 6)
            self.assertEqual(report["score_patterns"][0]["records"], 2)
            self.assertEqual(path.read_bytes(), raw)

    def test_bad_rows_fail_with_line_number(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "data.jsonl"
            for bad in ("[]", '{"messages":[null]}', '{"tags":"english"}', "broken"):
                path.write_text("{}\n" + bad, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, r":2:"):
                    audit(path)


if __name__ == "__main__":
    unittest.main()
