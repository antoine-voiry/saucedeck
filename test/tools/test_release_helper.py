import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import release_helper  # noqa: E402


class Result:
    def __init__(self, rc):
        self.returncode = rc


def fail(*a, **k):
    raise AssertionError("run must not be called")


class ReleaseTest(unittest.TestCase):
    def test_no_hs_cli(self):
        self.assertFalse(release_helper.release(which=lambda n: None, run=fail))

    def test_calls_hs(self):
        calls = []

        def run(cmd, **kw):
            calls.append((cmd, kw))
            return Result(0)

        self.assertTrue(release_helper.release(which=lambda n: "/opt/homebrew/bin/hs", run=run))
        self.assertEqual(calls, [(["/opt/homebrew/bin/hs", "-t", "2", "-c", "saucedeck.release()"],
                                  {"timeout": 5, "capture_output": True})])

    def test_nonzero(self):
        self.assertFalse(release_helper.release(which=lambda n: "hs", run=lambda c, **k: Result(1)))

    def test_errors_swallowed(self):
        for exc in (subprocess.TimeoutExpired("hs", 5), OSError("x")):
            def run(c, **k):
                raise exc
            self.assertFalse(release_helper.release(which=lambda n: "hs", run=run))


if __name__ == "__main__":
    unittest.main()
