import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import verify_sealant_coverage as QA
from test_panel_thermal import fixture, png_gray


@unittest.skipUnless(importlib.util.find_spec("PIL"), "Pixel coverage needs Pillow")
class SealantCoverageTests(unittest.TestCase):
    def test_hidden_side_fails_even_when_front_temperature_is_correct(self):
        from PIL import Image, ImageDraw
        import math
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            row = fixture()
            points = [[100, -30 + i * 1.5, 0] for i in range(39)]
            for view in row["inspection_views"]:
                view["sealant_surface_samples_view_cm"] = points
            path = root / "manifest.jsonl"
            path.write_text(json.dumps(row) + "\n")
            for index, temperature in enumerate([35, 60, 35]):
                (root / f"image{index}.png").write_bytes(png_gray(round((temperature-20)*255/100)))
            self.assertTrue(QA.verify(path)["passed"])
            # Keep the first (front) sample intact; obscure a later side segment.
            image = Image.open(root / "image1.png").convert("RGB")
            x = round(224 + points[20][1]/100/math.tan(math.radians(22.5))*224)
            ImageDraw.Draw(image).rectangle((x-4, 123, x+4, 133), fill=(0,0,0))
            image.save(root / "image1.png")
            result = QA.verify(path)
            self.assertFalse(result["passed"])
            self.assertIn(20, result["samples"][1]["failed_points"])

    def test_missing_surface_samples_cannot_pass_front_only_audit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.jsonl"
            path.write_text(json.dumps(fixture()) + "\n")
            result = QA.verify(path)
            self.assertFalse(result["passed"])
            self.assertTrue(any("perimeter samples" in message for message in result["failures"]))

    def test_hidden_cold_polymer_cannot_replace_panel_surface_measurement(self):
        from PIL import Image
        import math
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            row = fixture()
            for target in row["ground_truth"]["adhesive"]:
                target["apparent_c"] = 20.1
            points = [[100, -30 + i * 1.5, 0] for i in range(39)]
            for view in row["inspection_views"]:
                view["sealant_surface_samples_view_cm"] = points
            path = root / "manifest.jsonl"
            path.write_text(json.dumps(row) + "\n")
            for index in range(3):
                image = Image.new("RGB", (448, 256), (8, 8, 8))
                for point in points:
                    x = round(224 + point[1]/100/math.tan(math.radians(22.5))*224)
                    for py in range(127,130):
                        for px in range(x-1,x+1): image.putpixel((px,py), (0,0,0))
                image.save(root / f"image{index}.png")
            result = QA.verify(path)
            self.assertFalse(result["passed"])
            self.assertGreater(max(sample["maximum_error_c"] for sample in result["samples"]), 2.5)
