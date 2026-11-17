"""Real tiny CPU artifacts; readiness flags below are test fixtures, not evaluations."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "resources" / "training"))
import torch
import internal_model as internal


class PolicyLineageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        torch.set_num_threads(1)
        self.config = internal.ModelConfig(context_length=128, d_model=16, n_layers=1,
            n_heads=2, ffn_hidden=32, expert_hidden=8, skills=["keymaker-foundation"])
        self.model = internal.EngCoderModel(self.config).cpu()
        self.parent = "g000000"
        internal.save_generation(self.root, self.model, self.config, self.parent,
            status="initialized-untrained", parent=None, steps=0, tokens_seen=0,
            validation_loss=None, training_scope="all", promote=True)
        self.snapshot = internal.learning_policy_snapshot(self.root)

    def candidate(self, policy):
        return internal.save_generation(self.root, self.model, self.config, "g000001",
            status="trained-candidate", parent=self.parent, steps=1, tokens_seen=10,
            validation_loss=1.0, training_scope="all", promote=False,
            bootstrap_complete=True, full_backbone_trained=True, generation_ready=True,
            learning_policy=policy)

    def change_policy(self):
        policy = self.root / "learning" / "denylist.json"
        policy.write_text(json.dumps([{"id": "a" * 16, "prompt_hash": "b" * 16,
                                     "response_hash": "c" * 16}]))

    def test_stale_candidate_cannot_change_pointer(self):
        self.candidate(self.snapshot)
        pointer = internal.active_manifest_path(self.root)
        before = pointer.read_bytes()
        self.change_policy()
        with self.assertRaisesRegex(RuntimeError, "learning policy"):
            internal.promote_generation(self.root, "g000001", self.parent)
        self.assertEqual(pointer.read_bytes(), before)

    def test_matching_policy_promotes_but_later_rollback_is_rejected(self):
        self.candidate(self.snapshot)
        result = internal.promote_generation(self.root, "g000001", self.parent)
        self.assertEqual(result["status"], "promoted")
        self.change_policy()
        before = internal.active_manifest_path(self.root).read_bytes()
        with self.assertRaisesRegex(RuntimeError, "learning policy"):
            internal.activate_generation(self.root, "g000001", "g000001")
        self.assertEqual(internal.active_manifest_path(self.root).read_bytes(), before)

    def test_legacy_candidate_without_policy_is_not_implicitly_trusted(self):
        self.candidate(None)
        with self.assertRaisesRegex(RuntimeError, "learning policy"):
            internal.promote_generation(self.root, "g000001", self.parent)

    def test_policy_change_after_preflight_stops_before_model_load(self):
        self.change_policy()
        with patch.object(internal, "load_active_model") as load:
            with self.assertRaisesRegex(RuntimeError, "changed after admission"):
                internal.train_candidate(self.root, [], [], "english-foundation", 1,
                    0.001, 1, 1, 1, None, False, expected_learning_policy=self.snapshot)
            load.assert_not_called()

    def test_immediate_promotion_also_checks_policy(self):
        self.change_policy()
        before = internal.active_manifest_path(self.root).read_bytes()
        with self.assertRaisesRegex(RuntimeError, "learning policy"):
            internal.save_generation(self.root, self.model, self.config, "g000001",
                status="trained", parent=self.parent, steps=1, tokens_seen=10,
                validation_loss=1.0, training_scope="all", promote=True,
                bootstrap_complete=True, full_backbone_trained=True, generation_ready=True,
                learning_policy=self.snapshot)
        self.assertEqual(internal.active_manifest_path(self.root).read_bytes(), before)

    def test_midrun_change_stops_and_old_checkpoint_is_not_resumed(self):
        data = self.root / "training.jsonl"
        data.write_text(json.dumps({"id": "fixture", "domain": "english",
            "accepted": True, "reference_verified": True,
            "messages": [{"role": "user", "content": "Can we talk?"},
                         {"role": "assistant", "content": "Yes. What is on your mind?"}]}) + "\n")
        pointer = internal.active_manifest_path(self.root).read_bytes()
        save = internal.save_training_checkpoint
        def revoke_after_checkpoint(*args, **kwargs):
            save(*args, **kwargs)
            self.change_policy()
        parameters = dict(state_root=self.root, data_paths=[data], anchor_paths=[],
            scope="english-foundation", maximum_steps=3, learning_rate=0.001,
            batch_size=1, accumulation_steps=1, checkpoint_steps=1, control_dir=None,
            require_cuda=False, defer_promotion=True, bootstrap_target_steps=10)
        with patch.object(torch.cuda, "is_available", return_value=False):
            with patch.object(internal, "save_training_checkpoint", side_effect=revoke_after_checkpoint):
                with self.assertRaisesRegex(RuntimeError, "changed during training"):
                    internal.train_candidate(**parameters)
            checkpoints = list((internal.internal_root(self.root) / "checkpoints").rglob("latest.pt"))
            self.assertEqual(len(checkpoints), 1)
            checkpoint = torch.load(checkpoints[0], map_location="cpu", weights_only=False)
            self.assertEqual(checkpoint["steps"], 1)
            control = self.root / "control"
            control.mkdir()
            (control / "inference.priority").write_text("test")
            parameters["control_dir"] = control
            result = internal.train_candidate(**parameters)
            self.assertEqual(result["status"], "preempted")
            self.assertEqual(result["steps"], 0)
        self.assertEqual(internal.active_manifest_path(self.root).read_bytes(), pointer)


if __name__ == "__main__":
    unittest.main()
