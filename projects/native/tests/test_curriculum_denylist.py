"""CPU-only tests of authoritative deny policy at internal compiler admission."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "resources" / "training"))
import curriculum_compiler as compiler
import curriculum_guard as guard


class DenylistAdmission(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.policy = self.root / "learning" / "denylist.json"
        self.policy.parent.mkdir()
        self.source = self.root / "seed.jsonl"
        self.safe = {"id": "safe", "domain": "english", "accepted": True, "prompt": "I lost track of our discussion.", "response": "What part would you like to revisit?"}
        self.denied = {"id": "denied", "domain": "english", "accepted": True, "prompt": "Could you clarify what you meant?", "response": "Which part needs a clearer explanation?"}

    def ban(self, prompt="", response=""):
        self.policy.write_text(json.dumps([{"prompt_hash": compiler._learning_content_hash(prompt, ""), "response_hash": compiler._learning_content_hash("", response)}]))

    def compile(self, rows):
        self.source.write_text("".join(json.dumps(r)+"\n" for r in rows))
        return compiler.compile_curriculum(self.source, self.root / "selected.jsonl", self.root / "manifest.json", denylist_path=self.policy)

    def test_prompt_ban_filters_seed_clone_before_selection(self):
        self.ban(prompt=self.denied["prompt"])
        result = self.compile([self.safe, self.denied])
        self.assertEqual(result["selected_ids"], ["safe"])
        self.assertEqual(result["reasons"]["banned"], 1)
        self.assertIsNotNone(result["learning_denylist"]["sha256"])

    def test_raw_response_and_correction_are_checked(self):
        for field in ("response", "correction"):
            with self.subTest(field=field):
                row = dict(self.denied, correction="Here is a clearer version.")
                self.ban(response=row[field])
                self.assertEqual(self.compile([self.safe, row])["reasons"]["banned"], 1)

    def test_any_dialogue_turn_is_checked(self):
        for role in ("user", "assistant"):
            with self.subTest(role=role):
                row = {"id": "dialogue", "domain": "english", "accepted": True, "messages": [{"role": "user", "content": self.denied["prompt"]}, {"role": "assistant", "content": self.denied["response"]}, {"role": "user", "content": "Can we move on?"}, {"role": "assistant", "content": "Sure. What would you like to discuss?"}]}
                self.ban(prompt=self.denied["prompt"] if role == "user" else "", response=self.denied["response"] if role == "assistant" else "")
                self.assertEqual(self.compile([self.safe, row])["reasons"]["banned"], 1)

    def test_banned_new_correction_does_not_resurrect_old_answer(self):
        correction = dict(self.denied, id="corrected", human_rejected=True,
                          provenance="interactive-hard-correction",
                          correction="Let me restate it more clearly.")
        self.ban(response=correction["correction"])
        result = self.compile([self.safe, self.denied, correction])
        self.assertEqual(result["selected_ids"], ["safe"])
        self.assertEqual(result["reasons"]["banned"], 1)
        self.assertEqual(result["reasons"]["superseded_by_correction"], 1)

    def test_malformed_policy_fails_closed(self):
        for payload in ('{', '{}', '[1]', '[{}]', '[{"prompt_hash": "bad", "response_hash": "bad"}]'):
            with self.subTest(payload=payload):
                self.policy.write_text(payload)
                with self.assertRaisesRegex(RuntimeError, "denylist cannot be verified"):
                    self.compile([self.safe])

    def test_missing_policy_is_allowed_and_audited(self):
        result = self.compile([self.safe])
        self.assertEqual(result["selected_ids"], ["safe"])
        self.assertIsNone(result["learning_denylist"]["sha256"])

    def test_guard_uses_authoritative_root_after_episode_relocation(self):
        self.ban(response=self.denied["response"])
        self.source.write_text(json.dumps(self.safe)+"\n")
        episodes = self.root / "learning" / "verified-evidence.jsonl"
        episodes.write_text(json.dumps(self.denied)+"\n")
        for ready in (False, True):
            with self.subTest(ready=ready):
                args = argparse.Namespace(command="train-internal", state_root=self.root, seed_dataset=self.source, episodes=episodes, anchors=None, scope="keymaker-foundation", max_steps=1, bootstrap_target_steps=0, delta_limit=0)
                with patch.object(guard, "established", return_value=ready), patch.dict("os.environ", {}, clear=True), contextlib.redirect_stderr(io.StringIO()):
                    guard.enforce_namespace(args)
                self.assertIsNone(args.episodes)
                rows = [json.loads(line) for line in Path(args.seed_dataset).read_text().splitlines()]
                self.assertEqual([row["id"] for row in rows], ["safe"])

    def test_policy_change_during_compilation_fails(self):
        original = compiler._iter_jsonl
        def mutate(path):
            yield from original(path)
            self.ban(prompt=self.denied["prompt"])
        with patch.object(compiler, "_iter_jsonl", side_effect=mutate):
            with self.assertRaisesRegex(RuntimeError, "denylist changed"):
                self.compile([self.safe, self.denied])
        self.assertFalse((self.root / "selected.jsonl").exists())


if __name__ == "__main__":
    unittest.main()
