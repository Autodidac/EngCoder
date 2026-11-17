"""Exercise the worker boundary used by native training, without PyTorch."""
from __future__ import annotations

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
import curriculum_guard
from curriculum_compiler import bootstrap_step_budget


class FoundationBootstrapBoundary(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="engcoder-bootstrap-boundary-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / "evidence.jsonl"
        self.source.write_text("\n".join(json.dumps(row) for row in (
            {"id": "conversation", "domain": "english", "accepted": True,
             "messages": [{"role": "user", "content": "I lost track of what we were discussing."},
                          {"role": "assistant", "content": "What part would you like to revisit?"}]},
            {"id": "code", "domain": "cpp23-core", "accepted": True,
             "messages": [{"role": "user", "content": "Write a C++ vector declaration."},
                          {"role": "assistant", "content": "std::vector<int> values;"}]},
        )) + "\n", encoding="utf-8")

    def apply(self, *, ready=False, scope="keymaker-foundation", target=0, steps=64):
        arguments = argparse.Namespace(
            command="train-internal", state_root=self.root, seed_dataset=self.source,
            episodes=None, anchors=None, scope=scope, max_steps=steps,
            bootstrap_target_steps=target, delta_limit=0,
        )
        with patch.object(curriculum_guard, "established", return_value=ready), \
                patch.dict("os.environ", {}, clear=True), \
                contextlib.redirect_stderr(io.StringIO()):
            return curriculum_guard.enforce_namespace(arguments)

    def test_native_cold_start_trains_backbone_with_same_safety_cap(self):
        effective = self.apply()
        self.assertEqual(effective.bootstrap_target_steps, 64)
        self.assertEqual(effective.max_steps, 64)
        self.assertEqual(effective.scope, "english-foundation")
        rows = [json.loads(line) for line in Path(effective.seed_dataset).read_text().splitlines()]
        self.assertEqual([row["id"] for row in rows], ["conversation"])

    def test_explicit_cumulative_target_survives_small_resume_budget(self):
        effective = self.apply(target=500, steps=16)
        self.assertEqual(effective.bootstrap_target_steps, 500)
        self.assertEqual(effective.max_steps, 16)

    def test_cold_addon_request_keeps_existing_english_only_gate(self):
        effective = self.apply(scope="cpp23-core", steps=7)
        self.assertEqual(effective.scope, "english-foundation")
        self.assertEqual(effective.bootstrap_target_steps, 7)
        self.assertNotIn("std::vector", Path(effective.seed_dataset).read_text())

    def test_established_foundation_does_not_restart_bootstrap(self):
        effective = self.apply(ready=True)
        self.assertEqual(effective.bootstrap_target_steps, 0)
        self.assertEqual(effective.scope, "keymaker-foundation")
        self.assertEqual(effective.max_steps, 64)

    def test_established_addon_does_not_train_foundation(self):
        effective = self.apply(ready=True, scope="cpp23-core")
        self.assertEqual(effective.bootstrap_target_steps, 0)
        self.assertEqual(effective.scope, "cpp23-core")
        rows = [json.loads(line) for line in Path(effective.seed_dataset).read_text().splitlines()]
        self.assertEqual([row["id"] for row in rows], ["code"])

    def test_repair_extension_never_exceeds_requested_cap(self):
        self.assertEqual(bootstrap_step_budget(1, 500, 500), 1)
        self.assertEqual(bootstrap_step_budget(16, 600, 500), 16)
        self.assertEqual(bootstrap_step_budget(100, 500, 500), 50)

    def test_unfinished_foundation_stops_at_target_or_request_cap(self):
        self.assertEqual(bootstrap_step_budget(16, 100, 500), 16)
        self.assertEqual(bootstrap_step_budget(64, 490, 500), 10)
        self.assertEqual(bootstrap_step_budget(64, 499, 500), 1)


if __name__ == "__main__":
    unittest.main()
