"""Exercise the compiled native lock against the actual Python worker lock."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "resources" / "training"))
from learning_policy import policy_lock
PROBE = sys.argv.pop(1)


class InteropTests(unittest.TestCase):
    def child(self, root, mode):
        process = subprocess.Popen([PROBE, str(root), mode], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        def cleanup():
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)
        self.addCleanup(cleanup)
        self.assertEqual(process.stdout.readline().strip(), "starting")
        return process

    def test_python_blocks_native_until_release(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with policy_lock(root):
                process = self.child(root, "once")
                with self.assertRaises(subprocess.TimeoutExpired):
                    process.wait(timeout=0.2)
            output, error = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 0, error)
            self.assertEqual(output.strip(), "locked")

    def test_native_blocks_python_until_release(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            process = self.child(root, "hold")
            self.assertEqual(process.stdout.readline().strip(), "locked")
            with self.assertRaises(TimeoutError):
                with policy_lock(root, timeout_seconds=0):
                    self.fail("Python bypassed the native lock")
            _, error = process.communicate(input="release\n", timeout=5)
            self.assertEqual(process.returncode, 0, error)
            with policy_lock(root, timeout_seconds=0):
                pass


if __name__ == "__main__":
    unittest.main()
