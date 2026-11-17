"""CPU tensor checks for isolated addon updates (requires training pack)."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "resources" / "training"))
import torch
import internal_model as internal


class TrainingScopeTest(unittest.TestCase):
    def setUp(self):
        torch.set_num_threads(1)
        torch.manual_seed(41)
        self.model = internal.EngCoderModel(internal.ModelConfig(
            context_length=128, d_model=16, n_layers=1, n_heads=2,
            ffn_hidden=32, expert_hidden=8,
            skills=["keymaker-foundation", "cpp23-core", "cpp23-debugging"],
        )).cpu().eval()
        self.tokens = torch.tensor([[1, 4, 80, 81, 82, 5, 83, 84, 2]])

    def update(self, scope, route):
        parameters = internal.configure_training_scope(self.model, scope)
        optimizer = torch.optim.AdamW(parameters, lr=0.03, weight_decay=0.1)
        for _ in range(3):
            optimizer.zero_grad(set_to_none=True)
            result = self.model(self.tokens, skills=[route], labels=self.tokens)
            result["loss"].backward()
            optimizer.step()

    def test_cpp_update_changes_only_selected_addon_and_preserves_conversation(self):
        before = {name: value.detach().clone() for name, value in self.model.named_parameters()}
        with torch.inference_mode():
            conversation_before = self.model(self.tokens, skills=["keymaker-foundation"])["logits"].clone()
        self.update("cpp23-core", "cpp23-core")
        changed = set()
        for name, value in self.model.named_parameters():
            if not torch.equal(before[name], value):
                changed.add(name)
                self.assertIn(".experts.cpp23-core.", name)
        self.assertTrue(changed, "The selected addon must actually learn")
        with torch.inference_mode():
            conversation_after = self.model(self.tokens, skills=["keymaker-foundation"])["logits"]
        self.assertTrue(torch.equal(conversation_before, conversation_after))

    def test_scoped_foundation_update_keeps_shared_and_cpp_weights_frozen(self):
        before = {name: value.detach().clone() for name, value in self.model.named_parameters()}
        self.update("keymaker-foundation", "keymaker-foundation")
        changed = {name for name, value in self.model.named_parameters()
                   if not torch.equal(before[name], value)}
        self.assertTrue(changed)
        self.assertTrue(all(".experts.keymaker-foundation." in name for name in changed))

    def test_full_bootstrap_can_learn_shared_weights_without_cpp_gradient(self):
        before = {name: value.detach().clone() for name, value in self.model.named_parameters()}
        self.update("all", "keymaker-foundation")
        self.assertFalse(torch.equal(before["embedding.weight"], self.model.embedding.weight))
        self.assertFalse(torch.equal(before["final_norm.weight"], self.model.final_norm.weight))
        for name, value in self.model.named_parameters():
            if ".experts.cpp23-" in name:
                self.assertTrue(torch.equal(before[name], value), name)

    def test_legacy_scoped_checkpoint_cannot_restore_shared_changes(self):
        parameters = internal.configure_training_scope(self.model, "cpp23-core")
        legacy = {"optimizer": {"param_groups": [{"params": list(range(len(parameters) + 1))}]}}
        self.assertFalse(internal.checkpoint_training_scope_matches(legacy, self.model, "cpp23-core"))
        current = {"trainable_parameter_names": [
            name for name, parameter in self.model.named_parameters() if parameter.requires_grad
        ]}
        self.assertTrue(internal.checkpoint_training_scope_matches(current, self.model, "cpp23-core"))
        current["trainable_parameter_names"].append("final_norm.weight")
        self.assertFalse(internal.checkpoint_training_scope_matches(current, self.model, "cpp23-core"))

    def test_legacy_full_bootstrap_checkpoint_still_resumes(self):
        parameters = internal.configure_training_scope(self.model, "all")
        legacy = {"optimizer": {"param_groups": [{"params": list(range(len(parameters)))}]}}
        self.assertTrue(internal.checkpoint_training_scope_matches(legacy, self.model, "all"))


if __name__ == "__main__":
    unittest.main()
