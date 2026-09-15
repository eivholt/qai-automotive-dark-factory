"""Two-bead private radiometry QA must not invent a third adhesive target."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("panel_thermal_qa", Path(__file__).resolve().parents[1]/"scripts/verify_panel_thermal.py")
QA = importlib.util.module_from_spec(spec)
spec.loader.exec_module(QA)


def fixture(stage="adhesive"):
    return {"sample_id":"sample", "split":"train", "stage":stage,
        "contract_version":QA.CONTRACT, "thermal_recipe":QA.THERMAL_RECIPE,
        "expected_decision":"R",
        "images":[{"camera_id":f"spot{i+1}","path":f"image{i}.png"} for i in range(3)],
        "inspection_views":[{"spot":i,"fov_deg":45,"framing_corners_view_cm":[[100,0,0] for _ in range(16)]} for i in range(3)],
        "ground_truth":{"sealant_thermal_target":"conducted-panel-surface","weld_heat_recipe":QA.THERMAL_RECIPE,"adhesive_view_beads":[0,1,0],"weld_view_contacts":[[0,1],[2,3],[4,5]],
            "adhesive":[{"index":0,"apparent_c":35},{"index":1,"apparent_c":60}],
            "welds":[{"index":i,"apparent_c":t} for i,t in enumerate([30,40,35,42,45,70])]}}


def png_gray(value):
    def chunk(kind, data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
    raw=(b"\0"+bytes([value])*448*3)*256
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",448,256,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(raw))+chunk(b"IEND",b"")


class TwoBeadThermalTests(unittest.TestCase):
    def test_three_views_map_to_only_two_beads(self):
        self.assertEqual(QA.thermal_targets(fixture()),([0,1,0],[35,60,35]))
        self.assertEqual(QA.thermal_targets(fixture("weld")),([0,1,2,3,4,5],[30,40,35,42,45,70]))

    def test_every_weld_is_required_and_pairs_cannot_hide_a_hot_contact(self):
        r=fixture("weld")
        for change in [lambda r:r["ground_truth"]["welds"].pop(),
                       lambda r:r["ground_truth"].update(weld_view_contacts=[[0,1],[2,3],[4,4]])]:
            bad=copy.deepcopy(r);change(bad)
            with self.assertRaises(ValueError):QA.thermal_targets(bad)
        self.assertEqual(max(QA.thermal_targets(r)[1]),70)

    def test_wrong_physical_count_mapping_order_or_old_contract_fails(self):
        changes=[lambda r:r["ground_truth"]["adhesive"].append({"index":2,"apparent_c":80}),
            lambda r:r["ground_truth"].update(adhesive_view_beads=[0,1,2]),
            lambda r:r["ground_truth"].update(adhesive_view_beads=[0,0,0]),
            lambda r:r["ground_truth"].update(adhesive_view_beads=[False,1,0]),
            lambda r:r.update(contract_version="automotive-panel-process-v2"),
            lambda r:r["images"].pop(),lambda r:r["inspection_views"].reverse(),
            lambda r:r["ground_truth"]["adhesive"][0].update(apparent_c=float("nan"))]
        for mutate in changes:
            with self.subTest(mutation=changes.index(mutate)):
                r=fixture();mutate(r)
                with self.assertRaises(ValueError):QA.thermal_targets(r)

    @unittest.skipUnless(importlib.util.find_spec("PIL"), "Rendered pixel QA requires optional Pillow")
    def test_hot_second_bead_and_repeated_first_view_are_audited(self):
        with tempfile.TemporaryDirectory() as d:
            folder=Path(d);row=fixture();manifest=folder/"manifest.jsonl"
            manifest.write_text(json.dumps(row)+"\n",encoding="utf-8")
            for i,t in enumerate([35,60,35]):(folder/f"image{i}.png").write_bytes(png_gray(round((t-20)/100*255)))
            result=QA.verify(manifest)
            self.assertTrue(result["passed"],result["failures"])
            self.assertEqual([r["contact"] for r in result["contacts"]],[0,1,0])
            self.assertEqual(result["samples"],3)
            # A fictional hot third bead cannot masquerade as a second view of bead0.
            (folder/"image2.png").write_bytes(png_gray(round((80-20)/100*255)))
            result=QA.verify(manifest)
            self.assertFalse(result["passed"])
            self.assertTrue(any("image2.png" in x for x in result["failures"]))

    @unittest.skipUnless(importlib.util.find_spec("PIL"), "Rendered pixel QA requires optional Pillow")
    def test_all_six_weld_pixels_are_checked_in_three_images(self):
        from PIL import Image, ImageDraw
        import math
        with tempfile.TemporaryDirectory() as d:
            folder=Path(d);row=fixture("weld");manifest=folder/"manifest.jsonl"
            for view in row["inspection_views"]:
                view["framing_corners_view_cm"]=[[100,side,0] for _ in range(8) for side in [-20,20]]
            for i in range(3):
                image=Image.new("RGB",(448,256));draw=ImageDraw.Draw(image)
                for part,side in enumerate([-20,20]):
                    x=round(224+side/100/math.tan(math.radians(22.5))*224)
                    value=round((row["ground_truth"]["welds"][2*i+part]["apparent_c"]-20)*255/100)
                    draw.rectangle((x-4,124,x+4,132),fill=(value,value,value))
                image.save(folder/f"image{i}.png")
            manifest.write_text(json.dumps(row)+"\n",encoding="utf-8")
            result=QA.verify(manifest)
            self.assertTrue(result["passed"],result["failures"])
            self.assertEqual([r["contact"] for r in result["contacts"]],list(range(6)))
            # The last weld is hot. A missing/occluded second region cannot pass.
            Image.new("RGB",(448,256)).save(folder/"image2.png")
            self.assertFalse(QA.verify(manifest)["passed"])

    @unittest.skipUnless(importlib.util.find_spec("PIL"), "Rendered pixel QA requires optional Pillow")
    def test_narrow_bead_center_excludes_neighbor_steel_but_detects_occlusion(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as d:
            folder=Path(d);row=fixture();manifest=folder/"manifest.jsonl"
            manifest.write_text(json.dumps(row)+"\n",encoding="utf-8")
            for i,t in enumerate([35,60,35]):
                image=Image.new("RGB",(448,256),(255,255,255))
                value=round((t-20)*255/100)
                for y in range(127,130):
                    for x in range(223,226):image.putpixel((x,y),(value,value,value))
                image.save(folder/f"image{i}.png")
            self.assertTrue(QA.verify(manifest)["passed"])
            Image.new("RGB",(448,256)).save(folder/"image1.png")
            self.assertFalse(QA.verify(manifest)["passed"])

    @unittest.skipUnless(importlib.util.find_spec("PIL"), "Rendered pixel QA requires optional Pillow")
    def test_default_audit_does_not_open_test_images(self):
        with tempfile.TemporaryDirectory() as d:
            row=fixture();row["split"]="test";p=Path(d)/"manifest.jsonl";p.write_text(json.dumps(row)+"\n",encoding="utf-8")
            result=QA.verify(p)
            self.assertFalse(result["passed"])
            self.assertEqual(result["samples"],0)
