"""Exercise real OS lock ownership without a model or training pack."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

MODULES = Path(__file__).resolve().parents[1] / "resources" / "training"
sys.path.insert(0, str(MODULES))
from learning_policy import policy_lock


class PolicyLockTests(unittest.TestCase):
    def test_contention_exception_release_and_persistent_inode(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with policy_lock(root):
                with self.assertRaises(TimeoutError):
                    with policy_lock(root, timeout_seconds=0):
                        self.fail("second owner acquired the lock")
            with self.assertRaisesRegex(RuntimeError, "body failure"):
                with policy_lock(root):
                    raise RuntimeError("body failure")
            with policy_lock(root, timeout_seconds=0):
                self.assertTrue((root / "learning" / "policy.lock").exists())
            self.assertTrue((root / "learning" / "policy.lock").exists())

    def test_os_releases_ownership_after_holder_is_terminated(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            script = ("import sys,time;from pathlib import Path;"
                      "sys.path.insert(0,sys.argv[1]);from learning_policy import policy_lock\n"
                      "with policy_lock(Path(sys.argv[2])):\n"
                      " print('locked',flush=True)\n time.sleep(30)\n")
            process = subprocess.Popen([sys.executable, "-u", "-c", script, str(MODULES), str(root)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                       env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
            try:
                self.assertEqual(process.stdout.readline().strip(), "locked")
                with self.assertRaises(TimeoutError):
                    with policy_lock(root, timeout_seconds=0):
                        self.fail("child lock was not exclusive")
                process.terminate()
                process.wait(timeout=5)
                with policy_lock(root, timeout_seconds=0):
                    pass
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
