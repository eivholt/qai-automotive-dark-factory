#!/usr/bin/env python3
"""Compose six unchanged Unreal GPU iron-palette captures into an evidence figure.

Requires Pillow. Images are pasted at their original 448x256 resolution, without
cropping, recoloring, contrast changes or annotations over captured pixels.
Example: python scripts/render_thermal_transfer_comparison.py --directory
artifacts/thermal-transfer-v2 --output docs/media/thermal-transfer-comparison.png
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

PANELS = (
    ("initial", "A  Initial", "50 cm gap · before exposure"),
    ("near", "B  Nearby radiation", "50 cm gap · 600 s exposure"),
    ("far", "C  Distant radiation", "100 cm gap · 600 s exposure"),
    ("blocked", "D  Radiation blocked", "50 cm gap · ambient opaque shield"),
    ("contact", "E  Contact heating", "0 cm exposure gap · source retracted"),
    ("cooled", "F  After source removal", "600 s heating + 600 s cooling"),
)
SIZE = (448, 256)


def font(size, bold=False):
    candidates = (["DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "C:/Windows/Fonts/segoeuib.ttf"]
                  if bold else ["DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "C:/Windows/Fonts/segoeui.ttf"])
    for candidate in candidates:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            pass
    raise ValueError("A DejaVu Sans or Segoe UI font is required for reproducible, legible labels")


def compose(directory, output):
    document = json.loads((directory / "transfer_states.json").read_text(encoding="utf-8-sig"))
    if document.get("passed") is not True:
        raise ValueError("The source transfer smoke did not report a pass")
    states = document["states"]
    report = json.loads((directory / "transfer_qa.json").read_text(encoding="utf-8-sig"))
    if report.get("passed") is not True:
        raise ValueError("Run the strict transfer verifier successfully before creating the evidence figure")
    margin, gap, top, row_height = 48, 24, 126, 374
    width = margin * 2 + SIZE[0] * 2 + gap
    height = top + row_height * 3 + 82
    canvas = Image.new("RGB", (width, height), "#ffffff")
    draw = ImageDraw.Draw(canvas)
    title_font, panel_font = font(30, True), font(21, True)
    label_font, small_font = font(16), font(15)
    draw.text((margin, 27), "Heat transfer · actual Unreal Engine GPU renders", font=title_font, fill="#152336")
    draw.text((margin, 71), "Iron display palette · fixed 20–120 °C apparent-temperature range", font=font(18), fill="#455467")
    pasted = []
    for index, (state, heading, description) in enumerate(PANELS):
        snapshot = states[state]
        source = next(p for p in snapshot["profiles"] if p["surface"] == "HeatTransferSource")
        receiver = next(p for p in snapshot["profiles"] if p["surface"] == "HeatTransferReceiver")
        face_c = receiver["core_surface_temperature_c"] + receiver["face_delta_temperature_c"][3]
        elapsed = snapshot["heat_exchange"]["elapsed_seconds"]
        path = directory / f"transfer_{state}_display_iron.png"
        with Image.open(path) as image:
            image.load()
            if image.format != "PNG" or image.size != SIZE or image.mode not in ("RGB", "RGBA"):
                raise ValueError(f"Expected an original 448x256 RGB/RGBA PNG: {path}")
            if image.mode == "RGBA" and image.getchannel("A").getextrema() != (255, 255):
                raise ValueError(f"Source capture is not opaque: {path}")
            captured = image.convert("RGB")
        x, y = margin + (index % 2) * (SIZE[0] + gap), top + (index // 2) * row_height
        draw.text((x, y), heading, font=panel_font, fill="#172a41")
        image_y = y + 36
        draw.rectangle((x - 1, image_y - 1, x + SIZE[0], image_y + SIZE[1]), outline="#c9d0d9")
        canvas.paste(captured, (x, image_y))
        box = (x, image_y, x + SIZE[0], image_y + SIZE[1])
        pasted.append((box, captured.tobytes()))
        caption_y = image_y + SIZE[1] + 11
        donor = f"Source {source['core_surface_temperature_c']:.0f} °C" if source["component_visible"] else "Source removed"
        draw.text((x, caption_y), f"{donor}  ·  receiver facing surface {face_c:.1f} °C", font=label_font, fill="#23374e")
        draw.text((x, caption_y + 23), f"{description}  ·  t = {elapsed:.0f} s", font=small_font, fill="#4b596a")
    footer_y = top + row_height * 3 + 3
    draw.text((margin, footer_y), "Surface labels are simulated physical temperatures; image colors show apparent temperature.", font=small_font, fill="#455467")
    draw.text((margin, footer_y + 24), "Original 448 × 256 captures shown whole, at native size. No brightness or contrast adjustments.", font=small_font, fill="#455467")
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output)
    with Image.open(output) as saved:
        for box, pixels in pasted:
            if saved.crop(box).tobytes() != pixels:
                raise AssertionError("Figure composition changed captured pixels")
    print(json.dumps({"output": str(output.resolve()), "size": list(canvas.size), "panels": len(PANELS), "native_capture_pixels_preserved": True}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    compose(args.directory, args.output)


if __name__ == "__main__":
    main()
