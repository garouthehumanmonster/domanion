"""tformat.py - Binary .thtr file writer, parser, and height quantization helpers."""

import os
import math
import struct
import numpy as np
from .params import (
    MAGIC, VERSION, SECTION_COUNT, HEADER_BYTES, SECTION_TABLE_BYTES,
    LOGIC_N, WORLD_M, PLAYER_SLOTS, START_COUNT, SEA_LEVEL_CODE,
    H_CODE_SCALE, H_CODE_OFFSET
)
from .rng import xxh64


def m_to_code(h_m):
    """Convert height in metres to 16-bit unsigned integer height code."""
    arr = (np.asarray(h_m, dtype=np.float64) + H_CODE_OFFSET) * H_CODE_SCALE
    return np.clip(np.rint(arr), 0, 65535).astype(np.uint16)


def code_to_m(code):
    """Convert 16-bit unsigned integer height code back to metres as float64."""
    arr = np.asarray(code, dtype=np.float64)
    return -H_CODE_OFFSET + (arr * (640.0 / 65535.0))


def q16(metres: float) -> int:
    """Convert metres to 1/16th metre fixed integer units (0..65535)."""
    return int(np.clip(np.round(float(metres) * 16.0), 0, 65535))


def pack_starts(starts_list) -> bytes:
    """Pack 8 start records (16 bytes each)."""
    out = bytearray()
    for s in starts_list:
        # dict or tuple: slot, flags, x_q, z_q, plateau_hcode, flat_radius_q
        if isinstance(s, dict):
            slot = s["slot"]
            flags = s.get("flags", 1)
            x_q = s["x_q"]
            z_q = s["z_q"]
            plateau_hcode = s["plateau_hcode"]
            flat_radius_q = s["flat_radius_q"]
        else:
            slot, flags, x_q, z_q, plateau_hcode, flat_radius_q = s[:6]
        rec = struct.pack("<BBHHHH6s", slot, flags, x_q, z_q, plateau_hcode, flat_radius_q, b"\x00" * 6)
        out.extend(rec)
    return bytes(out)


def pack_resources(resources_list) -> bytes:
    """Pack resource records (16 bytes each)."""
    out = bytearray()
    for r in resources_list:
        # kind, owner, amount, x_q, z_q, footprint_q
        if isinstance(r, dict):
            kind = r["kind"]
            owner = r["owner"]
            amount = r["amount"]
            x_q = r["x_q"]
            z_q = r["z_q"]
            footprint_q = r["footprint_q"]
        else:
            kind, owner, amount, x_q, z_q, footprint_q = r[:6]
        rec = struct.pack("<BBHHHH6s", kind, owner, amount, x_q, z_q, footprint_q, b"\x00" * 6)
        out.extend(rec)
    return bytes(out)


def pack_landings(landings_list) -> bytes:
    """Pack landing records (8 bytes each)."""
    out = bytearray()
    for l in landings_list:
        # x_q, z_q, heading_a16
        if isinstance(l, dict):
            x_q = l["x_q"]
            z_q = l["z_q"]
            heading_a16 = l["heading_a16"]
        else:
            if isinstance(l[2], (tuple, list)):
                xm, zm, (dx, dz) = l
                x_q = q16(xm)
                z_q = q16(zm)
                ang = math.atan2(dz, dx)
                if ang < 0.0:
                    ang += 2.0 * math.pi
                heading_a16 = int(round((ang / (2.0 * math.pi)) * 65536.0)) % 65536
            else:
                x_q, z_q, heading_a16 = l[:3]
        rec = struct.pack("<HHH2s", int(x_q), int(z_q), int(heading_a16), b"\x00\x00")
        out.extend(rec)
    return bytes(out)


def write_theatre(path: str, seed_eff: int, N: int, height_codes: np.ndarray,
                  surface: np.ndarray, slope: np.ndarray, biome: np.ndarray,
                  distwater: np.ndarray, starts: list, resources: list, landings: list) -> int:
    """Write complete binary .thtr file atomically."""
    # Ensure byte buffers for sections
    sec1_data = np.ascontiguousarray(height_codes, dtype=np.uint16).tobytes()
    sec2_data = np.ascontiguousarray(surface, dtype=np.uint8).tobytes()
    sec3_data = np.ascontiguousarray(slope, dtype=np.uint8).tobytes()
    sec4_data = np.ascontiguousarray(biome, dtype=np.uint8).tobytes()
    sec5_data = np.ascontiguousarray(distwater, dtype=np.uint8).tobytes()
    sec6_data = pack_starts(starts)
    sec7_data = pack_resources(resources)
    sec8_data = pack_landings(landings)

    sections_info = [
        (1, len(sec1_data), N * N, sec1_data),
        (2, len(sec2_data), LOGIC_N * LOGIC_N, sec2_data),
        (3, len(sec3_data), LOGIC_N * LOGIC_N, sec3_data),
        (4, len(sec4_data), LOGIC_N * LOGIC_N, sec4_data),
        (5, len(sec5_data), LOGIC_N * LOGIC_N, sec5_data),
        (6, len(sec6_data), len(starts), sec6_data),
        (7, len(sec7_data), len(resources), sec7_data),
        (8, len(sec8_data), len(landings), sec8_data),
    ]

    cur_offset = HEADER_BYTES + SECTION_TABLE_BYTES # 64 + 128 = 192 (multiple of 64)
    table_entries = []
    aligned_payloads = []

    for sec_id, size_bytes, count, raw_bytes in sections_info:
        # Align cur_offset to 64
        rem = cur_offset % 64
        pad = (64 - rem) % 64
        if pad > 0:
            aligned_payloads.append(b"\x00" * pad)
            cur_offset += pad

        sec_offset = cur_offset
        table_entries.append((sec_id, sec_offset, size_bytes, count))
        aligned_payloads.append(raw_bytes)
        cur_offset += size_bytes

    # Align total file size to 64
    final_rem = cur_offset % 64
    final_pad = (64 - final_rem) % 64
    if final_pad > 0:
        aligned_payloads.append(b"\x00" * final_pad)
        cur_offset += final_pad

    total_bytes = cur_offset

    # Build section table (128 bytes)
    sec_table = bytearray()
    for sec_id, sec_offset, size_bytes, count in table_entries:
        sec_table.extend(struct.pack("<IIII", sec_id, sec_offset, size_bytes, count))

    # Assemble everything after header to compute content_xxh64
    post_header_data = bytes(sec_table) + b"".join(aligned_payloads)
    content_hash = xxh64(post_header_data, seed=0)

    # Build 64-byte Header
    header = struct.pack(
        "<IIIIQQHHHBBHH20s",
        MAGIC,
        VERSION,
        total_bytes,
        SECTION_COUNT,
        content_hash,
        int(seed_eff) & 0xFFFFFFFFFFFFFFFF,
        N,
        LOGIC_N,
        int(WORLD_M),
        PLAYER_SLOTS,
        len(starts),
        SEA_LEVEL_CODE,
        0, # reserved
        b"\x00" * 20
    )

    full_data = header + post_header_data
    assert len(full_data) == total_bytes

    # Atomic write
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp_path = path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(full_data)
    os.replace(tmp_path, path)
    return total_bytes


def read_theatre(path: str) -> dict:
    """Read and validate binary .thtr file, returning header and array views."""
    with open(path, "rb") as f:
        data = f.read()

    if len(data) < HEADER_BYTES + SECTION_TABLE_BYTES:
        raise ValueError(f"File too small: {len(data)} bytes")

    (magic, version, total_bytes, sec_count,
     content_hash, seed_eff, height_n, logic_n,
     world_m, player_slots, start_count, sea_code,
     reserved) = struct.unpack_from("<IIIIQQHHHBBHH", data, 0)

    if magic != MAGIC:
        raise ValueError(f"Bad magic: 0x{magic:08X} expected 0x{MAGIC:08X}")
    if version != VERSION:
        raise ValueError(f"Bad version: {version} expected {VERSION}")
    if total_bytes != len(data):
        raise ValueError(f"Total bytes mismatch: header={total_bytes}, actual={len(data)}")
    if sec_count != SECTION_COUNT:
        raise ValueError(f"Bad section count: {sec_count}")

    # Verify content hash over [64, total_bytes)
    computed_hash = xxh64(data[HEADER_BYTES:total_bytes], seed=0)
    if computed_hash != content_hash:
        raise ValueError(f"Hash mismatch: file=0x{content_hash:016X}, computed=0x{computed_hash:016X}")

    # Read section table
    sections = {}
    for i in range(SECTION_COUNT):
        sec_id, sec_off, sec_sz, sec_cnt = struct.unpack_from("<IIII", data, HEADER_BYTES + i * 16)
        if sec_off % 64 != 0:
            raise ValueError(f"Section {sec_id} offset {sec_off} not 64-byte aligned")
        if sec_off + sec_sz > total_bytes:
            raise ValueError(f"Section {sec_id} out of bounds")
        sections[sec_id] = (sec_off, sec_sz, sec_cnt)

    height_off, height_sz, _ = sections[1]
    surf_off, surf_sz, _ = sections[2]
    slope_off, slope_sz, _ = sections[3]
    biome_off, biome_sz, _ = sections[4]
    dist_off, dist_sz, _ = sections[5]
    starts_off, starts_sz, starts_cnt = sections[6]
    res_off, res_sz, res_cnt = sections[7]
    land_off, land_sz, land_cnt = sections[8]

    # Unpack structured sections
    starts = []
    for i in range(starts_cnt):
        slot, flags, x_q, z_q, p_hcode, f_rad_q = struct.unpack_from("<BBHHHH6x", data, starts_off + i * 16)
        starts.append({"slot": slot, "flags": flags, "x_q": x_q, "z_q": z_q,
                       "plateau_hcode": p_hcode, "flat_radius_q": f_rad_q})

    resources = []
    for i in range(res_cnt):
        kind, owner, amount, x_q, z_q, foot_q = struct.unpack_from("<BBHHHH6x", data, res_off + i * 16)
        resources.append({"kind": kind, "owner": owner, "amount": amount,
                          "x_q": x_q, "z_q": z_q, "footprint_q": foot_q})

    landings = []
    for i in range(land_cnt):
        x_q, z_q, heading_a16 = struct.unpack_from("<HHH2x", data, land_off + i * 8)
        landings.append({"x_q": x_q, "z_q": z_q, "heading_a16": heading_a16})

    return {
        "magic": magic,
        "version": version,
        "total_bytes": total_bytes,
        "content_hash": content_hash,
        "seed_eff": seed_eff,
        "height_n": height_n,
        "logic_n": logic_n,
        "world_m": world_m,
        "player_slots": player_slots,
        "start_count": start_count,
        "sea_level_code": sea_code,
        "height_codes": np.frombuffer(data, dtype=np.uint16, count=height_n * height_n, offset=height_off).reshape((height_n, height_n)),
        "surface": np.frombuffer(data, dtype=np.uint8, count=logic_n * logic_n, offset=surf_off).reshape((logic_n, logic_n)),
        "slope": np.frombuffer(data, dtype=np.uint8, count=logic_n * logic_n, offset=slope_off).reshape((logic_n, logic_n)),
        "biome": np.frombuffer(data, dtype=np.uint8, count=logic_n * logic_n, offset=biome_off).reshape((logic_n, logic_n)),
        "distwater": np.frombuffer(data, dtype=np.uint8, count=logic_n * logic_n, offset=dist_off).reshape((logic_n, logic_n)),
        "starts": starts,
        "resources": resources,
        "landings": landings
    }
