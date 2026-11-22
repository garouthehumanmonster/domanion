"""texformat.py - Binary format serializer and reader for .tex26 texture container."""

import os
import struct
import numpy as np

import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from asset_common.hashing import fnv1a64, xxh64
from asset_common.bc import decode_texel, level_size


MAGIC = 0x36325854  # 'TX26'
VERSION = 1
HEADER_SIZE = 64
RECORD_SIZE = 64
GLYPH_REC_SIZE = 16


def _round_up_64(val: int) -> int:
    return (val + 63) & ~63


class TexSpec:
    """Specification of a texture to be packed into .tex26."""

    def __init__(self, name, fmt, width, height, layers, mips, flags, data):
        self.name = name
        self.fmt = int(fmt)
        self.width = int(width)
        self.height = int(height)
        self.layers = int(layers)
        self.mips = int(mips)
        self.flags = int(flags)
        # data: list of mips, each mip is a list of layer byte blobs
        self.data = data


def level_layer_offset(fmt, w, h, layers, mips, level, layer) -> int:
    """Compute relative byte offset of (level, layer) from texture data start."""
    off = 0
    for l in range(level):
        lw = max(1, w >> l)
        lh = max(1, h >> l)
        off += layers * level_size(fmt, lw, lh)
    lw = max(1, w >> level)
    lh = max(1, h >> level)
    off += layer * level_size(fmt, lw, lh)
    return off


def write_tex26(path: str, specs: list[TexSpec], glyph_bytes: bytes) -> None:
    """Write texture container .tex26 atomically to path."""
    tex_count = len(specs)
    first_data_offset = _round_up_64(HEADER_SIZE + tex_count * RECORD_SIZE)

    curr_offset = first_data_offset
    table_records = []
    data_payloads = []

    for spec in specs:
        # Concatenate levels and layers
        blobs = []
        for l in range(spec.mips):
            for lay in range(spec.layers):
                blobs.append(spec.data[l][lay])
        tex_bytes = b"".join(blobs)

        # Probes
        probe_a = bytes(decode_texel(spec.fmt, spec.data[0][0], spec.width, spec.height, 37, 91))
        last_lvl = spec.mips - 1
        last_w = max(1, spec.width >> last_lvl)
        last_h = max(1, spec.height >> last_lvl)
        probe_b = bytes(decode_texel(spec.fmt, spec.data[last_lvl][0], last_w, last_h, 0, 0))

        name_hash = fnv1a64(spec.name)
        data_offset = curr_offset
        data_bytes = len(tex_bytes)

        rec = struct.pack(
            "<Q I H H H H I I I 4s 4s 24s",
            name_hash,
            spec.fmt,
            spec.width,
            spec.height,
            spec.layers,
            spec.mips,
            data_offset,
            data_bytes,
            spec.flags,
            probe_a,
            probe_b,
            b"\x00" * 24,
        )
        table_records.append(rec)
        data_payloads.append(tex_bytes)

        padded_end = _round_up_64(curr_offset + data_bytes)
        pad_len = padded_end - (curr_offset + data_bytes)
        if pad_len > 0:
            data_payloads.append(b"\x00" * pad_len)
        curr_offset = padded_end

    # Glyph table offset
    glyph_offset = curr_offset
    glyph_count = len(glyph_bytes) // GLYPH_REC_SIZE
    data_payloads.append(glyph_bytes)

    total_bytes = _round_up_64(glyph_offset + len(glyph_bytes))
    glyph_pad = total_bytes - (glyph_offset + len(glyph_bytes))
    if glyph_pad > 0:
        data_payloads.append(b"\x00" * glyph_pad)

    # Table payload
    table_bytes = b"".join(table_records)
    # Pad table up to first_data_offset
    table_pad = first_data_offset - (HEADER_SIZE + len(table_bytes))
    if table_pad > 0:
        table_bytes += b"\x00" * table_pad

    # Combined post-header data for hash
    post_header_bytes = bytearray(table_bytes)
    for p in data_payloads:
        post_header_bytes.extend(p)

    content_hash = xxh64(post_header_bytes, seed=0)

    header = struct.pack(
        "<4I Q 2I 32s",
        MAGIC,
        VERSION,
        total_bytes,
        tex_count,
        content_hash,
        glyph_offset,
        glyph_count,
        b"\x00" * 32,
    )

    full_blob = header + post_header_bytes
    assert len(full_blob) == total_bytes, f"Length {len(full_blob)} != total_bytes {total_bytes}"

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp_path = path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(full_blob)
    os.replace(tmp_path, path)


def read_tex26(path: str) -> dict:
    """Read .tex26 file and return validated header, texture records, and glyph table."""
    with open(path, "rb") as f:
        data = f.read()

    size = len(data)
    if size < HEADER_SIZE:
        raise ValueError(f"File too small: {size} < {HEADER_SIZE}")

    magic, version, total_bytes, tex_count, content_hash, glyph_offset, glyph_count = struct.unpack(
        "<4I Q 2I", data[:32]
    )

    if magic != MAGIC:
        raise ValueError(f"Bad magic 0x{magic:08X} != 0x{MAGIC:08X}")
    if version != VERSION:
        raise ValueError(f"Bad version {version} != {VERSION}")
    if total_bytes != size:
        raise ValueError(f"Bad total_bytes {total_bytes} != size {size}")
    if not (1 <= tex_count <= 16):
        raise ValueError(f"Bad texture_count {tex_count}")
    if glyph_count != 95:
        raise ValueError(f"Bad glyph_count {glyph_count} != 95")

    computed_hash = xxh64(data[HEADER_SIZE:], seed=0)
    if computed_hash != content_hash:
        raise ValueError(f"Bad content hash 0x{computed_hash:016X} != 0x{content_hash:016X}")

    if glyph_offset % 64 != 0 or glyph_offset + glyph_count * GLYPH_REC_SIZE > total_bytes:
        raise ValueError(f"Bad glyph table offset/size {glyph_offset}")

    KNOWN_NAMES = {
        fnv1a64(n): n for n in [
            "trim_albedo", "trim_normal", "trim_orm", "trim_emissive",
            "terrain_albedo", "terrain_normal", "terrain_rh",
            "vfx_atlas", "msdf_atlas"
        ]
    }
    textures = []
    for i in range(tex_count):
        off = HEADER_SIZE + i * RECORD_SIZE
        name_hash, fmt, w, h, layers, mips, d_off, d_bytes, flags, p_a, p_b = struct.unpack(
            "<Q I H H H H I I I 4s 4s", data[off : off + 40]
        )
        if d_off % 64 != 0 or d_off + d_bytes > total_bytes:
            raise ValueError(f"Texture {i} invalid data range offset={d_off} bytes={d_bytes}")

        tex_data = memoryview(data)[d_off : d_off + d_bytes]
        textures.append({
            "name": KNOWN_NAMES.get(name_hash, f"tex_{i}"),
            "name_hash": name_hash,
            "format": fmt,
            "width": w,
            "height": h,
            "layers": layers,
            "mips": mips,
            "data_offset": d_off,
            "data_bytes": d_bytes,
            "flags": flags,
            "probe_a": tuple(p_a),
            "probe_b": tuple(p_b),
            "data": tex_data,
        })

    glyph_dtype = np.dtype([
        ("cell_x", "<u2"),
        ("cell_y", "<u2"),
        ("cell_w", "<u2"),
        ("cell_h", "<u2"),
        ("offset_x_q", "<i2"),
        ("offset_y_q", "<i2"),
        ("advance_q", "<u2"),
        ("reserved", "<u2"),
    ])
    glyphs = np.frombuffer(
        data, dtype=glyph_dtype, count=glyph_count, offset=glyph_offset
    )

    return {
        "magic": magic,
        "version": version,
        "total_bytes": total_bytes,
        "texture_count": tex_count,
        "content_hash": content_hash,
        "glyph_offset": glyph_offset,
        "glyph_count": glyph_count,
        "textures": textures,
        "glyphs": glyphs,
    }
