"""preview.py - Standalone PNG renderer using standard library zlib for 1024x1024 map preview."""

import os
import struct
import zlib
import numpy as np
from .params import Surface, Biome, NodeKind


def _png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    """Pack a single PNG chunk with length, type, payload, and CRC32."""
    length = len(data)
    crc = zlib.crc32(chunk_type + data) & 0xFFFFFFFF
    return struct.pack(">I", length) + chunk_type + data + struct.pack(">I", crc)


def write_preview(path_png: str, data: dict) -> None:
    """Render 1024x1024 preview image with topography, biomes, starts, landings, and resources."""
    surface = data["surface"]     # [512, 512]
    biome = data["biome"]         # [512, 512]
    h_m = data["h_m"]             # [N, N]
    starts = data["starts"]
    landings = data["landings"]
    resources = data["resources"]

    # 1. Base color from 512x512 logic grid upscaled x2 to 1024x1024
    base_rgb = np.zeros((512, 512, 3), dtype=np.float32)

    # Surface colors
    base_rgb[surface == Surface.DeepWater] = (10, 30, 90)
    base_rgb[surface == Surface.ShallowWater] = (40, 110, 170)
    base_rgb[surface == Surface.Beach] = (220, 205, 150)
    base_rgb[surface == Surface.Cliff] = (70, 65, 60)

    # Land by biome
    land_mask = (surface == Surface.Land) | (surface == Surface.Rough)
    base_rgb[land_mask & (biome == Biome.Grass)] = (90, 140, 60)
    base_rgb[land_mask & (biome == Biome.Forest)] = (40, 95, 45)
    base_rgb[land_mask & (biome == Biome.Arid)] = (190, 170, 100)
    base_rgb[land_mask & (biome == Biome.Tundra)] = (170, 175, 160)
    base_rgb[land_mask & (biome == Biome.Rock)] = (120, 115, 110)

    # Upscale 2x nearest
    rgb_1024 = np.repeat(np.repeat(base_rgb, 2, axis=0), 2, axis=1)

    # 2. Hillshade from heightmap sampled at 1024x1024
    N = h_m.shape[0]
    if N >= 1024:
        step = N // 1024
        h1024 = h_m[::step, ::step][:1024, :1024].astype(np.float32)
    else: # N == 512
        h1024 = np.repeat(np.repeat(h_m, 2, axis=0), 2, axis=1).astype(np.float32)

    # Central difference per metre (spacing 4.0 m)
    gx = np.empty_like(h1024)
    gx[:, 1:-1] = (h1024[:, 2:] - h1024[:, :-2]) / 8.0
    gx[:, 0] = (h1024[:, 1] - h1024[:, 0]) / 4.0
    gx[:, -1] = (h1024[:, -1] - h1024[:, -2]) / 4.0

    gz = np.empty_like(h1024)
    gz[1:-1, :] = (h1024[2:, :] - h1024[:-2, :]) / 8.0
    gz[0, :] = (h1024[1, :] - h1024[0, :]) / 4.0
    gz[-1, :] = (h1024[-1, :] - h1024[-2, :]) / 4.0

    shade = np.clip(0.85 + 1.1 * (gx + gz), 0.35, 1.25)
    img_rgb = np.clip(rgb_1024 * shade[:, :, None], 0.0, 255.0).astype(np.uint8)

    # 3. Draw Overlays
    def draw_box(x_m, z_m, size, color):
        col = int(np.clip(round(x_m / 4.0), 0, 1023))
        row = int(np.clip(round(z_m / 4.0), 0, 1023))
        half = size // 2
        r0 = max(0, row - half)
        r1 = min(1024, row + half + 1)
        c0 = max(0, col - half)
        c1 = min(1024, col + half + 1)
        img_rgb[r0:r1, c0:c1] = color

    # Starts = red 7x7
    for s in starts:
        xm = s["x_m"] if "x_m" in s else float(s["x_q"]) / 16.0
        zm = s["z_m"] if "z_m" in s else float(s["z_q"]) / 16.0
        draw_box(xm, zm, 7, (255, 0, 0))

    # Landings = yellow 5x5
    for l in landings:
        if isinstance(l, dict):
            xm = l["x_m"] if "x_m" in l else float(l["x_q"]) / 16.0
            zm = l["z_m"] if "z_m" in l else float(l["z_q"]) / 16.0
        else:
            xm, zm = l[0], l[1]
        draw_box(xm, zm, 5, (255, 255, 0))

    # Resources = 3x3 dots
    res_colors = {
        NodeKind.Forest:  (20, 100, 20),
        NodeKind.Berries: (50, 205, 50),
        NodeKind.Herd:    (50, 205, 50),
        NodeKind.Iron:    (160, 160, 160),
        NodeKind.Gold:    (255, 180, 0),
        NodeKind.Oil:     (10, 10, 10),
        NodeKind.Silicon: (0, 220, 220),
        NodeKind.Relic:   (220, 0, 220),
    }

    for r in resources:
        xm = r["x_m"] if "x_m" in r else float(r["x_q"]) / 16.0
        zm = r["z_m"] if "z_m" in r else float(r["z_q"]) / 16.0
        c = res_colors.get(r["kind"], (255, 255, 255))
        draw_box(xm, zm, 3, c)

    # 4. Standard library PNG encoding
    raw_rows = []
    for r in range(1024):
        raw_rows.append(b"\x00" + img_rgb[r].tobytes())
    raw_data = b"".join(raw_rows)
    compressed = zlib.compress(raw_data, level=6)

    # PNG chunks: Signature, IHDR, IDAT, IEND
    png_sig = b"\x89PNG\r\n\x1a\n"
    ihdr_data = struct.pack(">IIBBBBB", 1024, 1024, 8, 2, 0, 0, 0)
    ihdr_chunk = _png_chunk(b"IHDR", ihdr_data)
    idat_chunk = _png_chunk(b"IDAT", compressed)
    iend_chunk = _png_chunk(b"IEND", b"")

    os.makedirs(os.path.dirname(os.path.abspath(path_png)), exist_ok=True)
    with open(path_png, "wb") as f:
        f.write(png_sig + ihdr_chunk + idat_chunk + iend_chunk)
