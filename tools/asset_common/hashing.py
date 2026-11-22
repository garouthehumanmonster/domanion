"""hashing.py - Pure-Python XXH64 and FNV-1a-64 implementations."""

import struct

M64 = 0xFFFFFFFFFFFFFFFF
P1 = 0x9E3779B185EBCA87
P2 = 0xC2B2AE3D27D4EB4F
P3 = 0x165667B19E3779F9
P4 = 0x85EBCA77C2B2AE63
P5 = 0x27D4EB2F165667C5

FNV64_OFFSET = 0xCBF29CE484222325
FNV64_PRIME = 0x100000001B3


def _rotl64(x: int, r: int) -> int:
    return ((x << r) | (x >> (64 - r))) & M64


def _round64(acc: int, input_val: int) -> int:
    acc = (acc + ((input_val * P2) & M64)) & M64
    acc = _rotl64(acc, 31)
    return (acc * P1) & M64


def _merge_round64(acc: int, val: int) -> int:
    val = _round64(0, val)
    acc = (acc ^ val) & M64
    return ((acc * P1) + P4) & M64


def xxh64(data: bytes | memoryview | bytearray, seed: int = 0) -> int:
    """Pure-Python XXH64 hash matching canonical XXH64 specification."""
    if not isinstance(data, (bytes, memoryview, bytearray)):
        data = bytes(data)

    total_len = len(data)
    seed = seed & M64
    pos = 0
    remaining = total_len

    if remaining >= 32:
        v1 = (seed + P1 + P2) & M64
        v2 = (seed + P2) & M64
        v3 = seed
        v4 = (seed - P1) & M64

        while remaining >= 32:
            val1, val2, val3, val4 = struct.unpack_from("<4Q", data, pos)
            v1 = _round64(v1, val1)
            v2 = _round64(v2, val2)
            v3 = _round64(v3, val3)
            v4 = _round64(v4, val4)
            pos += 32
            remaining -= 32

        h64 = (_rotl64(v1, 1) + _rotl64(v2, 7) + _rotl64(v3, 12) + _rotl64(v4, 18)) & M64
        h64 = _merge_round64(h64, v1)
        h64 = _merge_round64(h64, v2)
        h64 = _merge_round64(h64, v3)
        h64 = _merge_round64(h64, v4)
    else:
        h64 = (seed + P5) & M64

    h64 = (h64 + total_len) & M64

    while remaining >= 8:
        val64 = struct.unpack_from("<Q", data, pos)[0]
        k1 = _round64(0, val64)
        h64 = (h64 ^ k1) & M64
        h64 = ((_rotl64(h64, 27) * P1) + P4) & M64
        pos += 8
        remaining -= 8

    if remaining >= 4:
        val32 = struct.unpack_from("<I", data, pos)[0]
        k1 = (val32 * P1) & M64
        h64 = (h64 ^ k1) & M64
        h64 = ((_rotl64(h64, 23) * P2) + P3) & M64
        pos += 4
        remaining -= 4

    while remaining > 0:
        val8 = data[pos]
        k1 = (val8 * P5) & M64
        h64 = (h64 ^ k1) & M64
        h64 = (_rotl64(h64, 11) * P1) & M64
        pos += 1
        remaining -= 1

    h64 = (h64 ^ (h64 >> 33)) & M64
    h64 = (h64 * P2) & M64
    h64 = (h64 ^ (h64 >> 29)) & M64
    h64 = (h64 * P3) & M64
    h64 = (h64 ^ (h64 >> 32)) & M64

    return h64


def fnv1a64(data: bytes | str) -> int:
    """FNV-1a-64 hash."""
    if isinstance(data, str):
        data = data.encode("utf-8")
    h = FNV64_OFFSET
    for b in data:
        h = ((h ^ b) * FNV64_PRIME) & M64
    return h
