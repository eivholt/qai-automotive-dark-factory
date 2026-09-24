"""Native score-only lifecycle regression; real QNN numerical parity is separate."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class PrefillOnlyLifecycleTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ is needed for the native lifecycle fixture")
    def test_fresh_cache_full_logits_and_failure_cleanup(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="qairt-prefill-test-") as directory:
            executable = Path(directory) / "qairt-prefill-test"
            subprocess.run([
                "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "tests/qairt_fake"),
                str(root / "tests/qairt_prefill_only_test.cpp"), "-o", str(executable),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            self.assertIn("prefill-only lifecycle checks passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
