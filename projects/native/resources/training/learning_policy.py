"""Cross-process learning policy serialization; never delete the lock file.

Native and Python callers lock byte zero of learning/policy.lock on Windows,
or flock the same inode on POSIX. The OS releases ownership on process exit.
This lock protects short policy/promotion transactions, never tensor training.
"""
from contextlib import contextmanager
import os
from pathlib import Path
import time


@contextmanager
def policy_lock(state_root: Path, timeout_seconds: float = 5.0):
    if timeout_seconds < 0:
        raise ValueError("policy lock timeout must be nonnegative")
    path = Path(state_root) / "learning" / "policy.lock"
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(path, os.O_CREAT | os.O_RDWR, 0o600)
    acquired = False
    deadline = time.monotonic() + timeout_seconds
    try:
        while True:
            try:
                if os.name == "nt":
                    import msvcrt
                    os.lseek(descriptor, 0, os.SEEK_SET)
                    msvcrt.locking(descriptor, msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                acquired = True
                break
            except OSError as error:
                import errno
                if error.errno not in {errno.EACCES, errno.EAGAIN, errno.EDEADLK}:
                    raise
                if time.monotonic() >= deadline:
                    raise TimeoutError("Learning policy is busy; retry the operation") from error
                time.sleep(min(0.025, max(0.0, deadline - time.monotonic())))
        yield
    finally:
        try:
            if acquired:
                if os.name == "nt":
                    import msvcrt
                    os.lseek(descriptor, 0, os.SEEK_SET)
                    msvcrt.locking(descriptor, msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(descriptor, fcntl.LOCK_UN)
        finally:
            os.close(descriptor)
