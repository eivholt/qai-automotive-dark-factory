"""Run with DF_GENIEX_PROC_SOURCE pointing at the pinned geniex-proc checkout."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class FixedProcessorPNGTests(unittest.TestCase):
    def test_actual_decoder_contract_and_recovery(self):
        source = os.environ.get("DF_GENIEX_PROC_SOURCE")
        if not source or not shutil.which("g++"):
            self.skipTest("Set DF_GENIEX_PROC_SOURCE and install g++ for the real PNG decoder fixture")
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow is required to generate the PNG fixtures")
        source = Path(source)
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="qairt-png-test-") as temp:
            directory = Path(temp)
            image = Image.frombytes("L", (448, 256), bytes((x * 37 + y * 83) % 256
                                                         for y in range(256) for x in range(448)))
            for mode in ("L", "LA", "RGB", "RGBA"):
                image.convert(mode).save(directory / (mode + ".png"))
            for name, case in {
                "color": Image.new("RGB", (448, 256), (127, 128, 127)),
                "alpha": Image.new("RGBA", (448, 256), (127, 127, 127, 254)),
                "wrong-size": Image.new("L", (449, 256), 127),
                "uint16": Image.new("I;16", (448, 256), 257),
                "one-bit": Image.new("1", (448, 256), 1),
            }.items():
                case.save(directory / (name + ".png"))
            (directory / "truncated.png").write_bytes((directory / "L.png").read_bytes()[:40])
            image.save(directory / "wrong-format.png", format="BMP")
            executable = directory / "qairt-png-test"
            command = ["g++", "-std=c++20", "-O2", "-ffp-contract=off"]
            for include in ("include", "third-party", "third-party/xtensor/include", "third-party/xtl/include"):
                command += ["-I", str(source / include)]
            subprocess.run(command + [str(root / "tests/qairt_fixed_processor_png_test.cpp"),
                                      "-o", str(executable)], check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(executable), str(directory)], check=True,
                                    capture_output=True, text=True, timeout=10)
            self.assertIn("fixed PNG decode, contract and recovery checks passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
