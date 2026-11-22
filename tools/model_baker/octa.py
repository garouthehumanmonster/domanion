"""octa.py - Octahedral normal encoding/decoding and unorm16 helpers."""

import numpy as np


def encode(n: np.ndarray) -> np.ndarray:
    """Encode float unit vectors [..., 3] to octahedral snorm16 [..., 2]."""
    arr = np.asarray(n, dtype=np.float64)
    orig_shape = arr.shape
    arr = arr.reshape(-1, 3)

    norm = np.linalg.norm(arr, axis=1, keepdims=True)
    norm = np.where(norm == 0.0, 1.0, norm)
    arr = arr / norm

    x = arr[:, 0]
    y = arr[:, 1]
    z = arr[:, 2]

    denom = np.abs(x) + np.abs(y) + np.abs(z)
    denom = np.where(denom == 0.0, 1.0, denom)
    px = x / denom
    py = y / denom

    neg_z = z < 0.0
    sx = np.where(px >= 0.0, 1.0, -1.0)
    sy = np.where(py >= 0.0, 1.0, -1.0)

    px_neg = (1.0 - np.abs(py)) * sx
    py_neg = (1.0 - np.abs(px)) * sy

    px = np.where(neg_z, px_neg, px)
    py = np.where(neg_z, py_neg, py)

    p = np.stack([px, py], axis=1)
    snorm = np.rint(np.clip(p, -1.0, 1.0) * 32767.0).astype(np.int16)
    return snorm.reshape(orig_shape[:-1] + (2,))


def decode(snorm: np.ndarray) -> np.ndarray:
    """Decode octahedral snorm16 [..., 2] to float64 unit vectors [..., 3]."""
    arr = np.asarray(snorm, dtype=np.float64)
    orig_shape = arr.shape
    arr = arr.reshape(-1, 2)

    p = arr / 32767.0
    px = p[:, 0]
    py = p[:, 1]
    pz = 1.0 - np.abs(px) - np.abs(py)

    neg_z = pz < 0.0
    sx = np.where(px >= 0.0, 1.0, -1.0)
    sy = np.where(py >= 0.0, 1.0, -1.0)

    px_neg = (1.0 - np.abs(py)) * sx
    py_neg = (1.0 - np.abs(px)) * sy

    px = np.where(neg_z, px_neg, px)
    py = np.where(neg_z, py_neg, py)

    v = np.stack([px, py, pz], axis=1)
    norm = np.linalg.norm(v, axis=1, keepdims=True)
    norm = np.where(norm == 0.0, 1.0, norm)
    v = v / norm
    return v.reshape(orig_shape[:-1] + (3,))


def unorm16(x: np.ndarray | float) -> np.ndarray:
    """Convert float values in [0, 1] to uint16 [0, 65535]."""
    arr = np.asarray(x, dtype=np.float64)
    return np.rint(np.clip(arr, 0.0, 1.0) * 65535.0).astype(np.uint16)


def unorm16_to_float(u: np.ndarray | int) -> np.ndarray:
    """Convert uint16 values to float64 in [0, 1]."""
    arr = np.asarray(u, dtype=np.float64)
    return arr / 65535.0
