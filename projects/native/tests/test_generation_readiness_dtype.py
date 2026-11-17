"""Readiness must carry the real model precision into its inference wrapper."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'resources' / 'training'))
import torch
import internal_model as internal


class ReadinessDtypeTest(unittest.TestCase):
    def test_wrapper_uses_actual_precision(self):
        for dtype in (torch.float32, torch.float64):
            with self.subTest(dtype=dtype):
                config = internal.ModelConfig(context_length=128, d_model=16,
                    n_layers=1, n_heads=2, ffn_hidden=32, expert_hidden=8,
                    skills=['keymaker-foundation'])
                model = internal.EngCoderModel(config).to(dtype=dtype)
                seen = []
                def generate(loaded, *args, **kwargs):
                    seen.append(loaded.dtype)
                    self.assertIs(loaded.model, model)
                    self.assertFalse(kwargs['allow_verified_shortcuts'])
                    yield 'Unusable test output.', 0
                with patch.object(internal, 'generate_tokens', side_effect=generate):
                    result = internal.evaluate_generation_readiness(
                        model, config, 'test', {}, torch.device('cpu'))
                self.assertTrue(seen)
                self.assertTrue(all(actual == dtype for actual in seen))
                self.assertFalse(result.passed)


if __name__ == '__main__':
    unittest.main()
