"""Compile/run the actual C++ scoring logic; no model, SDK, or NPU required."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class GenieScoringTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "Native g++ is required for the C++ scoring fixture")
    def test_full_vocabulary_scoring(self):
        source = Path(__file__).with_name("genie_single_token_scoring_test.cpp")
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "genie-scoring-test"
            subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
