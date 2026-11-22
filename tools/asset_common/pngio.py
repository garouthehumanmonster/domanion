"""pngio.py - Standard library PNG encoding and basic decoding."""

import os
import struct
import zlib
import numpy as np


def _chunk(tag: bytes, data: bytes) -> bytes:
    length = len(data)
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", length) + tag + data + struct.pack(">I", crc)


def write_png(path: str, rgb_uint8: np.ndarray) -> None:
    """Write an RGB uint8 array [H, W, 3] to a PNG file using stdlib."""
    rgb = np.ascontiguousarray(rgb_uint8, dtype=np.uint8)
    if rgb.ndim != 3 or rgb.shape[2] != 3:
        raise ValueError("Expected [H, W, 3] array")
    h, w, _ = rgb.shape

    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter byte 0 (None)
        raw.extend(rgb[y].tobytes())

    compressed = zlib.compress(bytes(raw), level=6)
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    data = sig + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", compressed) + _chunk(b"IEND", b"")

    dirname = os.path.dirname(os.path.abspath(path))
    if dirname:
        os.makedirs(dirname, exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def read_png_info(path: str) -> tuple[int, int]:
    """Validate PNG signature and IHDR CRC; return (width, height)."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid PNG signature or file too short")

    length, tag = struct.unpack_from(">I4s", data, 8)
    if tag != b"IHDR":
        raise ValueError("First chunk must be IHDR")
    chunk_data = data[16 : 16 + length]
    crc = struct.unpack_from(">I", data, 16 + length)[0]
    expected_crc = zlib.crc32(tag + chunk_data) & 0xFFFFFFFF
    if crc != expected_crc:
        raise ValueError("IHDR CRC mismatch")

    w, h = struct.unpack_from(">II", chunk_data, 0)
    return w, h


def decode_png_rgb(path: str) -> np.ndarray:
    """Decode a PNG produced by write_png into an RGB uint8 array [H, W, 3]."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 8 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid PNG signature")

    w, h = read_png_info(path)
    pos = 8
    idat_parts = []
    while pos < len(data):
        length, tag = struct.unpack_from(">I4s", data, pos)
        chunk_data = data[pos + 8 : pos + 8 + length]
        crc = struct.unpack_from(">I", data, pos + 8 + length)[0]
        expected_crc = zlib.crc32(tag + chunk_data) & 0xFFFFFFFF
        if crc != expected_crc:
            raise ValueError(f"{tag.decode('latin1', errors='replace')} CRC mismatch")
        if tag == b"IDAT":
            idat_parts.append(chunk_data)
        elif tag == b"IEND":
            break
        pos += 12 + length

    raw = zlib.decompress(b"".join(idat_parts))
    stride = 1 + w * 3
    if len(raw) != h * stride:
        raise ValueError("Decompressed size mismatch")

    img = np.zeros((h, w, 3), dtype=np.uint8)
    for y in range(h):
        offset = y * stride
        if raw[offset] != 0:
            raise ValueError("Unsupported PNG row filter")
        img[y] = np.frombuffer(raw, dtype=np.uint8, count=w * 3, offset=offset + 1).reshape((w, 3))
    return img
