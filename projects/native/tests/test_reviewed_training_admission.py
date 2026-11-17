"""CPU-only real compiler/tensor-loader admission contract (training pack required)."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "resources" / "training"))
import curriculum_compiler as compiler
import internal_model as internal


def reviewed(row, purpose="training"):
    row = deepcopy(row)
    row["independent_review"] = {"generator_id": "author-agent", "reviewer_id": "independent-review-agent", "verdict": "accepted", "purpose": purpose, "record_sha256": compiler.review_record_sha256(row), "review_artifact_sha256": "a" * 64}
    return row


class ReviewAdmission(unittest.TestCase):
    def test_policy_changes_invalidate_preflight(self):
        self.write([reviewed(self.row)])
        policy = self.source.parent / "denylist.json"
        compiler.compile_curriculum(self.source, self.output, self.manifest,
                                    scope="keymaker-foundation", denylist_path=policy)
        policy.write_text("[]")
        with self.assertRaisesRegex(RuntimeError, "policy changed"):
            internal.preflight_compiled_training([self.output], self.manifest, 256, "keymaker-foundation")
        compiler.compile_curriculum(self.source, self.output, self.manifest,
                                    scope="keymaker-foundation", denylist_path=policy)
        load = internal.load_training_sequences
        def change_during_load(*args):
            sequences = load(*args)
            policy.unlink()
            return sequences
        with patch.object(internal, "load_training_sequences", side_effect=change_during_load):
            with self.assertRaisesRegex(RuntimeError, "policy changed"):
                internal.preflight_compiled_training([self.output], self.manifest, 256, "keymaker-foundation")

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / "source.jsonl"
        self.output = self.root / "compiled.jsonl"
        self.manifest = self.root / "manifest.json"
        self.row = {"id": "one", "skill": "keymaker-foundation", "domain": "english", "accepted": True, "eligible_for_training": True, "human_accepted": False, "split": "train", "messages": [{"role": "user", "content": "The meeting is Monday. Change it to Tuesday. Which day is it?"}, {"role": "assistant", "content": "Tuesday."}]}

    def write(self, rows):
        self.source.write_text("".join(json.dumps(row)+"\n" for row in rows))

    def test_reviewed_compile_load_roundtrip(self):
        self.write([reviewed(self.row)])
        result = compiler.compile_curriculum(self.source, self.output, self.manifest, scope="keymaker-foundation")
        self.assertEqual(result["counts"]["selected_records"], 1)
        self.assertEqual(len(internal.load_training_sequences([self.output], 256, "keymaker-foundation")), 1)
        counts = internal.preflight_compiled_training([self.output], self.manifest, 256, "keymaker-foundation")
        self.assertEqual(counts["loaded_training_sequences"], 1)
        saved = json.loads(self.output.read_text())
        self.assertFalse(saved["human_accepted"])
        self.assertNotIn("reference_verified", saved)

    def test_invalid_or_mutated_reviews_not_admitted(self):
        cases = []
        row = reviewed(self.row); row["independent_review"]["reviewer_id"] = " AUTHOR-AGENT "; cases.append(row)
        row = reviewed(self.row); row["messages"][-1]["content"] = "Wednesday."; cases.append(row)
        row = reviewed(self.row); row["independent_review"]["verdict"] = "rejected"; cases.append(row)
        row = reviewed(self.row); row["independent_review"]["review_artifact_sha256"] = "missing"; cases.append(row)
        for row in cases:
            with self.subTest(row=row):
                self.write([row])
                result = compiler.compile_curriculum(self.source, self.output, self.manifest, scope="keymaker-foundation")
                self.assertEqual(result["counts"]["selected_records"], 0)
                self.assertEqual(internal.load_training_sequences([self.source], 256, "keymaker-foundation"), [])

    def test_review_does_not_bypass_private_or_rejected(self):
        for flags in ({"private": True}, {"human_rejected": True}, {"accepted": False}, {"is_error": True}):
            row = dict(self.row, **flags)
            self.write([reviewed(row)])
            self.assertEqual(internal.load_training_sequences([self.source], 256, "keymaker-foundation"), [])

    def test_preflight_catches_silent_unreviewed_subset_and_hash_mismatch(self):
        self.write([self.row])
        compiler.compile_curriculum(self.source, self.output, self.manifest, scope="keymaker-foundation")
        with self.assertRaisesRegex(RuntimeError, "compiler selected 1, tensor loader admitted 0"):
            internal.preflight_compiled_training([self.output], self.manifest, 256, "keymaker-foundation")
        self.output.write_text(self.output.read_text()+"\n")
        with self.assertRaisesRegex(RuntimeError, "does not match"):
            internal.preflight_compiled_training([self.output], self.manifest, 256, "keymaker-foundation")

    def test_heldout_cannot_be_reclassified_by_training_review(self):
        for flags in ({"split": "heldout"}, {"tags": ["held-out"]}, {"split": "held_out"}, {"split": "validation"}, {"kind": "evaluation"}, {"type": "eval"}):
            row = reviewed(dict(self.row, **flags))
            self.write([row])
            self.assertEqual(internal.load_training_sequences([self.source], 256, "keymaker-foundation"), [])
            result = compiler.compile_curriculum(self.source, self.output, self.manifest, scope="keymaker-foundation")
            self.assertEqual(result["counts"]["selected_records"], 0)

    def test_heldout_review_has_distinct_non_gradient_path(self):
        row = reviewed(dict(self.row, accepted=False, eligible_for_training=False, split="heldout"), "evaluation")
        self.write([row]); before = self.source.read_bytes()
        self.assertEqual(internal.load_training_sequences([self.source], 256, "keymaker-foundation"), [])
        self.assertEqual(len(internal.load_evaluation_sequences([self.source], 256, "keymaker-foundation")), 1)
        self.assertEqual(self.source.read_bytes(), before)
        self.write([reviewed(dict(self.row, accepted=False, eligible_for_training=False, split="heldout", private=True), "evaluation")])
        self.assertEqual(internal.load_evaluation_sequences([self.source], 256, "keymaker-foundation"), [])


if __name__ == "__main__":
    unittest.main()
