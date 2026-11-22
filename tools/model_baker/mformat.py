"""mformat.py - Binary format serializer and reader for .mdbin model bundles."""

import os
import struct
import numpy as np

import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from asset_common.hashing import xxh64

MAGIC = 0x36324D44  # 'MD26'
VERSION = 1
HEADER_SIZE = 64
SECTION_TABLE_OFFSET = 64
SECTION_TABLE_SLOTS = 8
SECTION_TABLE_SIZE = SECTION_TABLE_SLOTS * 16  # 128
FIRST_SECTION_OFFSET = 192

MODEL_DTYPE = np.dtype([
    ("id_hash", "<u8"),
    ("bmin", "<i4", (3,)),
    ("bmax", "<i4", (3,)),
    ("vtx_base", "<u4", (3,)),
    ("idx_first", "<u4", (3,)),
    ("idx_count", "<u4", (3,)),
    ("vtx_count", "<u2", (3,)),
    ("part_count", "<u2"),
    ("vat_row_base", "<u4"),
    ("hp_first", "<u2"),
    ("hp_count", "<u2"),
    ("tri_count_lod0", "<u2"),
    ("category", "<u1"),
    ("flags", "<u1"),
    ("radius_xz", "<i4"),
    ("reserved", "<u4"),
], align=False)

VERTEX_DTYPE = np.dtype([
    ("pos", "<f4", (3,)),
    ("nrm", "<i2", (2,)),
    ("uv", "<u2", (2,)),
    ("trim_id", "<u1"),
    ("flags", "<u1"),
    ("part", "<u2"),
], align=False)

HARDPOINT_DTYPE = np.dtype([
    ("kind", "<u1"),
    ("part", "<u1"),
    ("reserved", "<u2"),
    ("pos", "<i4", (3,)),
], align=False)


def _round_up_64(val: int) -> int:
    return (val + 63) & ~63


def write_models(path: str, models_data: dict) -> None:
    """Serialize assembled models, vertices, indices, hardpoints, and VAT to path atomically."""
    models_arr = np.asarray(models_data["models"], dtype=MODEL_DTYPE)
    vertices_arr = np.asarray(models_data["vertices"], dtype=VERTEX_DTYPE)
    indices_arr = np.asarray(models_data["indices"], dtype="<u2")
    hardpoints_arr = np.asarray(models_data["hardpoints"], dtype=HARDPOINT_DTYPE)
    vat_arr = np.asarray(models_data["vat"], dtype="<f2")  # [vat_rows, 48, 4]

    model_count = len(models_arr)
    vertex_count = len(vertices_arr)
    index_count = len(indices_arr)
    hardpoint_count = len(hardpoints_arr)
    vat_rows = vat_arr.shape[0] if len(vat_arr.shape) > 0 else 0
    vat_cols = 48
    frames_per_model = 44

    # Calculate section layout
    sections = [
        (1, models_arr.tobytes(), model_count),
        (2, vertices_arr.tobytes(), vertex_count),
        (3, indices_arr.tobytes(), index_count),
        (4, hardpoints_arr.tobytes(), hardpoint_count),
        (5, vat_arr.tobytes(), vat_rows),
    ]

    sec_entries = []
    sec_payloads = []
    curr_offset = FIRST_SECTION_OFFSET

    for sec_id, data_bytes, count in sections:
        size_bytes = len(data_bytes)
        sec_entries.append((sec_id, curr_offset, size_bytes, count))
        sec_payloads.append(data_bytes)
        # Pad up to 64
        padded_end = _round_up_64(curr_offset + size_bytes)
        pad_len = padded_end - (curr_offset + size_bytes)
        if pad_len > 0:
            sec_payloads.append(b"\x00" * pad_len)
        curr_offset = padded_end

    total_bytes = curr_offset

    # Build section table (8 slots x 16 bytes)
    table_bytes = bytearray()
    for sec_id, off, sz, cnt in sec_entries:
        table_bytes.extend(struct.pack("<4I", sec_id, off, sz, cnt))
    # Unused 3 slots
    for _ in range(SECTION_TABLE_SLOTS - len(sec_entries)):
        table_bytes.extend(struct.pack("<4I", 0, 0, 0, 0))

    # Combine payload from offset 64 to total_bytes to compute hash
    pre_hash_bytes = bytearray(table_bytes)
    for p in sec_payloads:
        pre_hash_bytes.extend(p)

    content_xxh64 = xxh64(pre_hash_bytes, seed=0)

    # Build header (64 bytes)
    header = struct.pack(
        "<4I Q 7I 12s",
        MAGIC,
        VERSION,
        total_bytes,
        len(sec_entries),
        content_xxh64,
        model_count,
        vertex_count,
        index_count,
        hardpoint_count,
        vat_rows,
        vat_cols,
        frames_per_model,
        b"\x00" * 12,
    )

    full_blob = header + pre_hash_bytes
    assert len(full_blob) == total_bytes, f"Length {len(full_blob)} != total_bytes {total_bytes}"

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp_path = path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(full_blob)
    os.replace(tmp_path, path)


def read_models(path: str) -> dict:
    """Read .mdbin file and return header and structured views of sections."""
    with open(path, "rb") as f:
        data = f.read()

    size = len(data)
    if size < HEADER_SIZE:
        raise ValueError(f"File too small: {size} < {HEADER_SIZE}")

    (
        magic,
        version,
        total_bytes,
        section_count,
        content_xxh64,
        model_count,
        vertex_count,
        index_count,
        hardpoint_count,
        vat_rows,
        vat_cols,
        frames_per_model,
        _,
    ) = struct.unpack_from("<4I Q 7I 12s", data, 0)

    if magic != MAGIC:
        raise ValueError(f"Bad magic 0x{magic:08x} != 0x{MAGIC:08x}")
    if version != VERSION:
        raise ValueError(f"Bad version {version} != {VERSION}")
    if total_bytes != size:
        raise ValueError(f"Bad size: header says {total_bytes} != file size {size}")
    if (
        section_count != 5
        or vat_cols != 48
        or frames_per_model != 44
        or model_count < 1
        or model_count > 256
        or vat_rows != 44 * model_count
    ):
        raise ValueError(f"Bad header fields (sections={section_count}, models={model_count}, vat_rows={vat_rows})")

    # Verify section table
    sec_entries = {}
    for slot in range(SECTION_TABLE_SLOTS):
        sec_id, off, sz, cnt = struct.unpack_from("<4I", data, SECTION_TABLE_OFFSET + slot * 16)
        if slot < 5:
            if sec_id != slot + 1:
                raise ValueError(f"Bad section id {sec_id} in slot {slot} (expected {slot + 1})")
            if off % 64 != 0 or off < FIRST_SECTION_OFFSET or off + sz > total_bytes:
                raise ValueError(f"Bad section {sec_id} offset={off}, size={sz}")
            sec_entries[sec_id] = (off, sz, cnt)

    # Exact size checks
    if sec_entries[1][1] != 96 * model_count:
        raise ValueError(f"Bad models section size {sec_entries[1][1]}")
    if sec_entries[2][1] != 24 * vertex_count:
        raise ValueError(f"Bad vertices section size {sec_entries[2][1]}")
    if sec_entries[3][1] != 2 * index_count:
        raise ValueError(f"Bad indices section size {sec_entries[3][1]}")
    if sec_entries[4][1] != 16 * hardpoint_count:
        raise ValueError(f"Bad hardpoints section size {sec_entries[4][1]}")
    if sec_entries[5][1] != vat_rows * 48 * 8:
        raise ValueError(f"Bad vat section size {sec_entries[5][1]}")

    # Hash check
    actual_hash = xxh64(data[64:total_bytes], seed=0)
    if actual_hash != content_xxh64:
        raise ValueError(f"Bad XXH64 hash: 0x{actual_hash:016x} != 0x{content_xxh64:016x}")

    # Structured views
    models_view = np.frombuffer(data, dtype=MODEL_DTYPE, count=model_count, offset=sec_entries[1][0])
    vertices_view = np.frombuffer(data, dtype=VERTEX_DTYPE, count=vertex_count, offset=sec_entries[2][0])
    indices_view = np.frombuffer(data, dtype="<u2", count=index_count, offset=sec_entries[3][0])
    hardpoints_view = np.frombuffer(data, dtype=HARDPOINT_DTYPE, count=hardpoint_count, offset=sec_entries[4][0])
    vat_view = np.frombuffer(data, dtype="<f2", count=vat_rows * 48 * 4, offset=sec_entries[5][0]).reshape((vat_rows, 48, 4))

    return {
        "magic": magic,
        "version": version,
        "total_bytes": total_bytes,
        "section_count": section_count,
        "content_xxh64": content_xxh64,
        "model_count": model_count,
        "vertex_count": vertex_count,
        "index_count": index_count,
        "hardpoint_count": hardpoint_count,
        "vat_rows": vat_rows,
        "vat_cols": vat_cols,
        "frames_per_model": frames_per_model,
        "models": models_view,
        "vertices": vertices_view,
        "indices": indices_view,
        "hardpoints": hardpoints_view,
        "vat": vat_view,
    }
