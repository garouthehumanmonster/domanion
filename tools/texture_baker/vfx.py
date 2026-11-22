"""vfx.py - VFX flipbook atlas assembler and frame coordinate lookup."""

import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vfx_effects import EFFECTS, render_frame

VFX_INFO = [(name, kind == "loop", 16) for _, name, kind in EFFECTS]


def frame_origin(effect: int, frame: int) -> tuple[int, int]:
    """Return top-left pixel (x, y) coordinates of the given (effect, frame) on the 2048x2048 atlas."""
    assert 0 <= effect < 16
    assert 0 <= frame < 16
    cell_x = (effect % 4) * 512
    cell_y = (effect // 4) * 512
    fx = (frame % 4) * 128
    fy = (frame // 4) * 128
    return (cell_x + fx, cell_y + fy)


def assemble_vfx_atlas() -> np.ndarray:
    """Render all 12 effects into a 2048x2048 premultiplied RGBA8 atlas."""
    atlas = np.zeros((2048, 2048, 4), dtype=np.uint8)

    for e_id, name, kind in EFFECTS:
        for f in range(16):
            if kind == "loop":
                t = float(f) / 16.0
            else:
                t = float(f) / 15.0

            frame = render_frame(e_id, t)
            frame_byte = np.rint(np.clip(frame, 0.0, 1.0) * 255.0).astype(np.uint8)
            ox, oy = frame_origin(e_id, f)
            atlas[oy : oy + 128, ox : ox + 128] = frame_byte

    return atlas
