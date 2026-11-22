"""font_data.py - 4x6 stroke font definition and glyph metrics for ASCII 32..126."""

import struct
import numpy as np

# Stroke definitions on a 4 x 6 grid (x: 0..4, y: 0..6, descenders down to -1).
# Multiple strokes separated by " / ", points "x,y" separated by spaces.
UPPERCASE_STROKES = {
    'A': "0,0 2,6 4,0 / 0.8,2 3.2,2",
    'B': "0,0 0,6 2.6,6 3.6,5.2 3.6,3.8 2.6,3 0,3 / 2.6,3 3.8,2.2 3.8,0.8 2.8,0 0,0",
    'C': "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1",
    'D': "0,0 0,6 2.5,6 4,4.5 4,1.5 2.5,0 0,0",
    'E': "4,6 0,6 0,0 4,0 / 0,3 3,3",
    'F': "4,6 0,6 0,0 / 0,3 3,3",
    'G': "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,3 2.2,3",
    'H': "0,0 0,6 / 4,0 4,6 / 0,3 4,3",
    'I': "1,6 3,6 / 2,6 2,0 / 1,0 3,0",
    'J': "4,6 4,1 3,0 1,0 0,1",
    'K': "0,0 0,6 / 4,6 0,2.5 / 1.3,3.7 4,0",
    'L': "0,6 0,0 4,0",
    'M': "0,0 0,6 2,3 4,6 4,0",
    'N': "0,0 0,6 4,0 4,6",
    'O': "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0",
    'P': "0,0 0,6 3,6 4,5 4,4 3,3 0,3",
    'Q': "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0 / 2.5,2 4,0",
    'R': "0,0 0,6 3,6 4,5 4,4 3,3 0,3 / 2,3 4,0",
    'S': "4,5 3,6 1,6 0,5 0,4 1,3 3,3 4,2 4,1 3,0 1,0 0,1",
    'T': "0,6 4,6 / 2,6 2,0",
    'U': "0,6 0,1 1,0 3,0 4,1 4,6",
    'V': "0,6 2,0 4,6",
    'W': "0,6 1,0 2,3 3,0 4,6",
    'X': "0,6 4,0 / 4,6 0,0",
    'Y': "0,6 2,3 4,6 / 2,3 2,0",
    'Z': "0,6 4,6 0,0 4,0",
    '0': "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0 / 1,1.2 3,4.8",
    '1': "0.8,5 2,6 2,0 / 0.8,0 3.2,0",
    '2': "0,5 1,6 3,6 4,5 4,4 0,0 4,0",
    '3': "0,5 1,6 3,6 4,5 4,4 3,3 1.5,3 / 3,3 4,2 4,1 3,0 1,0 0,1",
    '4': "3,0 3,6 0,2 4,2",
    '5': "4,6 0.4,6 0,3.4 2.6,3.8 4,2.8 4,1 3,0 1,0 0,1",
    '6': "3.6,6 1,5 0,3.5 0,1 1,0 3,0 4,1 4,2.6 3,3.6 1,3.6 0,2.6",
    '7': "0,6 4,6 1.6,0",
    '8': "1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3 1,3 0,2 0,1 1,0 3,0 4,1 4,2 3,3",
    '9': "0.4,0 3,1 4,2.5 4,5 3,6 1,6 0,5 0,4.2 1,3.2 3,3.2 4,4.2",
    '.': "1.6,0 2.4,0",

    ',': "2,0 1.4,-1",
    ':': "2,1 2,1.4 / 2,4 2,4.4",
    ';': "2,1 1.5,-0.5 / 2,4 2,4.4",
    '!': "2,6 2,2 / 2,0 2,0.4",
    '?': "0,5 1,6 3,6 4,5 4,4 2,3 2,2 / 2,0 2,0.4",
    '-': "0.5,3 3.5,3",
    '+': "0.5,3 3.5,3 / 2,1.5 2,4.5",
    '/': "0,0 4,6",
    '\\': "4,6 0,0",
    '%': "0.2,6 0.2,4.8 1.2,4.8 1.2,6 0.2,6 / 2.8,0 2.8,1.2 3.8,1.2 3.8,0 2.8,0 / 0,0 4,6",
    '(': "3,6.5 1.5,5 1.5,1 3,-0.5",
    ')': "1,6.5 2.5,5 2.5,1 1,-0.5",
    '[': "3,6 1,6 1,0 3,0",
    ']': "1,6 3,6 3,0 1,0",
    '{': "3,6.5 2.2,6 2.2,3.8 1.2,3 2.2,2.2 2.2,0 3,-0.5",
    '}': "1,6.5 1.8,6 1.8,3.8 2.8,3 1.8,2.2 1.8,0 1,-0.5",
    '=': "0.5,2 3.5,2 / 0.5,4 3.5,4",
    '*': "2,1.5 2,4.5 / 0.7,2.2 3.3,3.8 / 0.7,3.8 3.3,2.2",
    '#': "1,0 1.6,6 / 2.4,0 3,6 / 0,2 4,2 / 0,4 4,4",
    '"': "1.2,6 1.2,4.5 / 2.8,6 2.8,4.5",
    "'": "2,6 2,4.5",
    '<': "3.5,5 0.5,3 3.5,1",
    '>': "0.5,5 3.5,3 0.5,1",
    '_': "0,-1 4,-1",
    '|': "2,6.5 2,-0.5",
    '^': "0.5,3.5 2,6 3.5,3.5",
    '`': "1.2,6 2.2,4.8",
    '~': "0,2.8 1,3.8 2,3 3,2.2 4,3.2",
    '$': "4,5 3,6 1,6 0,5 0,4 1,3 3,3 4,2 4,1 3,0 1,0 0,1 / 2,6.8 2,-0.8",
    '&': "4,0 0.5,4 0.5,5 1.5,6 2.5,5 2.5,4 0,1 1,0 2.5,0 4,2.5",
    '@': "3.2,2 3.2,4 1.5,4 1.5,2 3.2,2 / 3.2,2 4,2 4,5 3,6 1,6 0,5 0,1 1,0 3,0",
    ' ': "",
}


def parse_strokes(stroke_str: str, scale_x=1.0, scale_y=1.0) -> list[list[tuple[float, float]]]:
    """Parse stroke string into list of stroke polyline points [(x, y), ...]."""
    if not stroke_str.strip():
        return []
    strokes = []
    for part in stroke_str.split(" / "):
        part = part.strip()
        if not part:
            continue
        pts = []
        for pair in part.split():
            x_str, y_str = pair.split(",")
            pts.append((float(x_str) * scale_x, float(y_str) * scale_y))
        strokes.append(pts)
    return strokes


# Build full dictionary for ASCII 32..126
FONT_GLYPHS: dict[int, list[list[tuple[float, float]]]] = {}

for code in range(32, 127):
    ch = chr(code)
    if 'a' <= ch <= 'z':
        # Lowercase small-caps: reuse uppercase with x*0.9, y*0.72
        upper_ch = ch.upper()
        raw_str = UPPERCASE_STROKES[upper_ch]
        FONT_GLYPHS[code] = parse_strokes(raw_str, 0.9, 0.72)
    elif ch in UPPERCASE_STROKES:
        FONT_GLYPHS[code] = parse_strokes(UPPERCASE_STROKES[ch], 1.0, 1.0)
    else:
        raise ValueError(f"Missing character {ch} (ASCII {code})")


S_PX = 26.0 / 6.0
STROKE_HALF_WIDTH = 0.42  # in grid units
RANGE_PX = 4.0


def render_glyph(code: int) -> tuple[np.ndarray, bytes]:
    """Render a single 48x48 RGBA8 MSDF cell and its 16-byte metric record."""
    cell_idx = code - 32
    cell_col = cell_idx % 12
    cell_row = cell_idx // 12
    cell_x = cell_col * 48
    cell_y = cell_row * 48

    strokes = FONT_GLYPHS[code]

    if not strokes:  # space ' '
        cell_img = np.zeros((48, 48, 4), dtype=np.uint8)
        offset_x_q = int(round(-8.0 * 64.0))
        offset_y_q = 2304
        advance_q = int(round(3.0 * S_PX * 64.0))
        record = struct.pack("<4H 2h 2H", cell_x, cell_y, 48, 48, offset_x_q, offset_y_q, advance_q, 0)
        return cell_img, record

    all_x = [pt[0] for st in strokes for pt in st]
    xmin = min(all_x)
    xmax = max(all_x)

    offset_x_q = int(round((xmin * S_PX - 8.0) * 64.0))
    offset_y_q = 2304
    advance_q = int(round(((xmax - xmin) + 1.6) * S_PX * 64.0))
    record = struct.pack("<4H 2h 2H", cell_x, cell_y, 48, 48, offset_x_q, offset_y_q, advance_q, 0)

    # Convert stroke points to cell pixel coords:
    # px_x = 8 + (gx - xmin) * S
    # px_y = 36 - gy * S
    pixel_strokes = []
    for st in strokes:
        pst = [(8.0 + (gx - xmin) * S_PX, 36.0 - gy * S_PX) for gx, gy in st]
        pixel_strokes.append(pst)

    # Pixel grid centres
    cols = np.arange(48, dtype=np.float32) + 0.5
    rows = np.arange(48, dtype=np.float32) + 0.5
    PX, PY = np.meshgrid(cols, rows)  # shape (48, 48)

    min_dist2 = np.full((48, 48), 1e9, dtype=np.float32)

    for pst in pixel_strokes:
        if len(pst) == 1:
            cx, cy = pst[0]
            d2 = (PX - cx) ** 2.0 + (PY - cy) ** 2.0
            min_dist2 = np.minimum(min_dist2, d2)
        else:
            for k in range(len(pst) - 1):
                ax, ay = pst[k]
                bx, by = pst[k + 1]
                abx, aby = bx - ax, by - ay
                ab_len2 = abx * abx + aby * aby
                if ab_len2 < 1e-8:
                    d2 = (PX - ax) ** 2.0 + (PY - ay) ** 2.0
                else:
                    apx = PX - ax
                    apy = PY - ay
                    proj = np.clip((apx * abx + apy * aby) / ab_len2, 0.0, 1.0)
                    qx = ax + proj * abx
                    qy = ay + proj * aby
                    d2 = (PX - qx) ** 2.0 + (PY - qy) ** 2.0
                min_dist2 = np.minimum(min_dist2, d2)

    min_dist = np.sqrt(min_dist2)
    # Signed distance: positive inside the stroke
    sd_px = (STROKE_HALF_WIDTH * S_PX) - min_dist
    val = np.clip(0.5 + sd_px / (2.0 * RANGE_PX), 0.0, 1.0)
    byte_val = np.rint(val * 255.0).astype(np.uint8)

    # R = G = B = A
    cell_img = np.stack([byte_val, byte_val, byte_val, byte_val], axis=-1)

    # Border rows/cols 0 and 47 must be 0
    cell_img[0, :, :] = 0
    cell_img[47, :, :] = 0
    cell_img[:, 0, :] = 0
    cell_img[:, 47, :] = 0

    return cell_img, record
