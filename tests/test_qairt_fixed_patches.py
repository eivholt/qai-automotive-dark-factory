"""Pure packing regression; actual pinned-processor equivalence is measured on IQ9."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class FixedPatchesTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ is needed for the native packing fixture")
    def test_exact_layout_normalization_and_sensor_contract(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="qairt-patches-test-") as directory:
            executable = Path(directory) / "qairt-patches-test"
            subprocess.run([
                "g++", "-std=c++20", "-O2", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror",
                str(root / "tests/qairt_fixed_patches_test.cpp"), "-o", str(executable),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            self.assertIn("fixed patch layout and contract checks passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
