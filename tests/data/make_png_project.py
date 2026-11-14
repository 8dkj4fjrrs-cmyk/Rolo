#!/usr/bin/env python3
"""Makes tests/data/png_project: one scene showing every kind of PNG file.

    python3 tests/data/make_png_project.py tests/data/png_project

Rolo decodes PNG files itself (ADR 0024). The test player.draws_png_images
plays this project; a file that cannot be read is a warning, which fails it.
Each run makes the same files. Needs Pillow (pip install pillow).
"""
import json
import math
import sys
from pathlib import Path

from PIL import Image

W, H = 128, 128


def pattern(x, y):
    """A colour wheel fading out towards the edge: soft alpha shows blending."""
    dx, dy = (x - W / 2) / (W / 2), (y - H / 2) / (H / 2)
    d = math.hypot(dx, dy)
    a = math.atan2(dy, dx)
    r = int(127.5 + 127.5 * math.cos(a))
    g = int(127.5 + 127.5 * math.cos(a - 2.094))
    b = int(127.5 + 127.5 * math.cos(a + 2.094))
    alpha = max(0, min(255, int(255 * (1.15 - d) / 0.5)))
    return r, g, b, alpha


def rgba():
    im = Image.new("RGBA", (W, H))
    im.putdata([pattern(x, y) for y in range(H) for x in range(W)])
    return im


def main(out):
    assets = out / "assets"
    scenes = out / "scenes"
    assets.mkdir(parents=True, exist_ok=True)
    scenes.mkdir(parents=True, exist_ok=True)
    base = rgba()
    grey16 = Image.new("I;16", (W, H))
    grey16.putdata([(x * 65535 // (W - 1)) for y in range(H) for x in range(W)])
    palette_alpha = base.quantize(colors=64)   # a palette with a transparency table (tRNS)
    kinds = [
        ("rgba.png", base, {}),
        ("rgb.png", base.convert("RGB"), {}),
        ("grey.png", base.convert("L"), {}),
        ("grey_alpha.png", base.convert("LA"), {}),
        ("palette.png", base.convert("RGB").quantize(colors=32), {}),
        ("palette_alpha.png", palette_alpha, {}),
        ("grey16.png", grey16, {}),
        ("interlaced.PNG", base, {"interlace": True}),
    ]
    entities = [{
        "id": "0000000000000001", "name": "Backdrop", "active": True,
        "components": [
            {"type": "rolo.Transform", "position": [640, 360], "rotation": 0, "scale": [1, 1]},
            {"type": "rolo.Sprite", "image": "504e470000000002", "size": [1280, 720], "color": "#ffffffff",
             "flipX": False, "flipY": False},
        ]}]
    for i, (name, image, options) in enumerate(kinds):
        if options.get("interlace"):
            # Pillow cannot write interlaced PNGs: this one is made by make_interlaced().
            (assets / name).write_bytes(make_interlaced(image))
        else:
            image.save(assets / name, optimize=False)
        asset_id = "504e4700000000%02x" % (i + 1)
        (assets / (name + ".meta")).write_text(json.dumps(
            {"format": "rolo.asset", "version": 1, "id": asset_id, "type": "image", "filter": "smooth"}, indent=2) + "\n")
        entities.append({
            "id": "00000000000000%02x" % (i + 16), "name": name.split(".")[0], "active": True,
            "components": [
                {"type": "rolo.Transform", "position": [200 + 290 * (i % 4), 520 - 300 * (i // 4)], "rotation": 0,
                 "scale": [1, 1]},
                {"type": "rolo.Sprite", "image": asset_id, "size": [256, 256], "color": "#ffffffff",
                 "flipX": False, "flipY": False},
            ]})
    scene = {"format": "rolo.scene", "version": 1, "name": "PNG kinds", "entities": entities}
    (scenes / "kinds.lscene").write_text(json.dumps(scene, indent=2) + "\n")
    (scenes / "kinds.lscene.meta").write_text(json.dumps(
        {"format": "rolo.asset", "version": 1, "id": "504e4700000000ff", "type": "scene"}, indent=2) + "\n")
    (out / "project.rolo").write_text(json.dumps(
        {"format": "rolo.project", "version": 1, "name": "PNG Test", "startScene": "scenes/kinds.lscene"},
        indent=2) + "\n")


def make_interlaced(image):
    """An Adam7-interlaced RGBA PNG of `image`, written by hand (zlib only)."""
    import struct
    import zlib

    px = image.convert("RGBA").load()
    w, h = image.size

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = bytearray()
    for x0, y0, dx, dy in [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2),
                           (0, 1, 1, 2)]:
        for y in range(y0, h, dy):
            row = bytearray()
            for x in range(x0, w, dx):
                row += bytes(px[x, y])
            if row:
                raw += b"\x00" + row
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 1))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "tests/data/png_project"))
