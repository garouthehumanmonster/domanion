"""trims.py - Trim sheet definition, UV layout, and palette export."""

import json
import os
import sys

TRIM_NAMES = [
    "steel_plain", "steel_worn", "armor_olive", "armor_desert", "armor_grey",
    "camo_woodland", "camo_desert", "camo_urban", "cloth_tunic", "cloth_dark",
    "leather", "skin", "wood_planks", "wood_log", "stone_block", "stone_smooth",
    "brick", "concrete", "concrete_panel", "glass_window", "roof_tile", "roof_metal",
    "rubber_tread", "hazard_stripe", "glow_emissive", "solar_panel", "foliage_leaf",
    "bark", "grass_tuft", "gold", "oil_dark", "ice_crystal",
]

PREVIEW_RGB = [
    (180, 185, 190),  # steel_plain
    (120, 125, 130),  # steel_worn
    (85, 100, 60),    # armor_olive
    (195, 175, 130),  # armor_desert
    (100, 105, 110),  # armor_grey
    (55, 75, 45),     # camo_woodland
    (180, 160, 120),  # camo_desert
    (115, 120, 130),  # camo_urban
    (160, 80, 60),    # cloth_tunic
    (50, 50, 60),     # cloth_dark
    (110, 65, 40),    # leather
    (220, 175, 145),  # skin
    (150, 105, 60),   # wood_planks
    (90, 60, 35),     # wood_log
    (140, 140, 140),  # stone_block
    (170, 170, 175),  # stone_smooth
    (160, 70, 55),    # brick
    (165, 165, 160),  # concrete
    (145, 145, 145),  # concrete_panel
    (100, 160, 200),  # glass_window
    (175, 65, 45),    # roof_tile
    (75, 85, 95),     # roof_metal
    (35, 35, 35),     # rubber_tread
    (220, 180, 30),   # hazard_stripe
    (80, 230, 240),   # glow_emissive
    (30, 50, 100),    # solar_panel
    (45, 130, 45),    # foliage_leaf
    (75, 50, 30),     # bark
    (70, 160, 50),    # grass_tuft
    (240, 200, 50),   # gold
    (25, 25, 30),     # oil_dark
    (180, 220, 245),  # ice_crystal
]


def _build_trims() -> list[dict]:
    trims = []
    for t, (name, rgb) in enumerate(zip(TRIM_NAMES, PREVIEW_RGB)):
        col = t % 8
        row = t // 8
        x0 = col * 256 + 4
        x1 = (col + 1) * 256 - 4
        y0 = row * 256 + 4
        y1 = (row + 1) * 256 - 4
        trims.append({
            "id": t,
            "name": name,
            "col": col,
            "row": row,
            "px": [x0, y0, x1, y1],
            "preview_rgb": list(rgb),
        })
    return trims


TRIMS = _build_trims()
_TRIM_MAP = {t["name"]: t["id"] for t in TRIMS}


def trim_id(name: str) -> int:
    """Return trim ID for a given trim name."""
    if name not in _TRIM_MAP:
        raise KeyError(f"Unknown trim: {name}")
    return _TRIM_MAP[name]


def trim_rect_uv(trim_idx: int) -> tuple[float, float, float, float]:
    """Return (u0, v0, u1, v1) float64 on [0, 1] for sheet texture."""
    if not (0 <= trim_idx < len(TRIMS)):
        raise IndexError(f"Trim id {trim_idx} out of range")
    px = TRIMS[trim_idx]["px"]
    return (
        px[0] / 2048.0,
        px[1] / 2048.0,
        px[2] / 2048.0,
        px[3] / 2048.0,
    )


def write_trims_json(path: str) -> None:
    """Export trims definition to JSON."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(TRIMS, f, indent=2, sort_keys=True)


if __name__ == "__main__":
    if "--write" in sys.argv:
        out_path = os.path.join(
            os.path.dirname(__file__), "..", "..", "assets", "source", "trims.json"
        )
        write_trims_json(out_path)
        print(f"Wrote trims to {out_path}")
