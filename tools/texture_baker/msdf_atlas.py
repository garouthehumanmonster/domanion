"""msdf_atlas.py - MSDF atlas assembler (font glyphs + UI/tactical icons)."""

import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from font_data import render_glyph
from shapes import get_icon_contours
from msdf import render_icon

GLYPH_CODES = list(range(32, 127))


def build_msdf_atlas() -> tuple[np.ndarray, bytes]:
    """Assemble 1024x512 RGBA8 MSDF atlas and 95 glyph records (16 B each)."""
    atlas = np.zeros((512, 1024, 4), dtype=np.uint8)
    records = []

    # 1. 95 Font Glyphs: 48x48 px, 12 columns, row-major from (0, 0)
    for c in GLYPH_CODES:
        cell_img, rec = render_glyph(c)
        records.append(rec)

        idx = c - 32
        col = idx % 12
        row = idx // 12
        x = col * 48
        y = row * 48

        atlas[y : y + 48, x : x + 48] = cell_img

    # 2. 32 Icons: 64x64 px, 16 columns from (0, 384)
    for i in range(32):
        cnts = get_icon_contours(i)
        icon_img = render_icon(cnts)

        col = i % 16
        row = i // 16
        x = col * 64
        y = 384 + row * 64

        atlas[y : y + 64, x : x + 64] = icon_img

    glyph_bytes = b"".join(records)
    assert len(glyph_bytes) == 95 * 16
    return atlas, glyph_bytes
