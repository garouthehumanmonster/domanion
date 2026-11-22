"""anim.py - Procedural animation operations for VAT baking."""

from dataclasses import dataclass
import math
import numpy as np


def _smoothstep(x: float) -> float:
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


def _pulse_envelope(t: float, peak: float) -> float:
    if t <= 0.0 or t >= 1.0:
        return 0.0
    if t <= peak:
        u = t / peak
        return _smoothstep(u)
    else:
        u = (t - peak) / (1.0 - peak)
        return 1.0 - _smoothstep(u)


def _rotation_matrix(axis: int, angle_rad: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
    px, py, pz = pivot
    c = math.cos(angle_rad)
    s = math.sin(angle_rad)

    R = np.eye(4, dtype=np.float64)
    if axis == 0:  # X
        R[1, 1] = c
        R[1, 2] = -s
        R[2, 1] = s
        R[2, 2] = c
    elif axis == 1:  # Y
        R[0, 0] = c
        R[0, 2] = s
        R[2, 0] = -s
        R[2, 2] = c
    elif axis == 2:  # Z
        R[0, 0] = c
        R[0, 1] = -s
        R[1, 0] = s
        R[1, 1] = c
    else:
        raise ValueError(f"Invalid axis: {axis}")

    T = np.eye(4, dtype=np.float64)
    T[0, 3] = px
    T[1, 3] = py
    T[2, 3] = pz

    T_inv = np.eye(4, dtype=np.float64)
    T_inv[0, 3] = -px
    T_inv[1, 3] = -py
    T_inv[2, 3] = -pz

    return T @ R @ T_inv


def _translation_matrix(axis: int, offset: float) -> np.ndarray:
    T = np.eye(4, dtype=np.float64)
    T[axis, 3] = offset
    return T


class AnimOp:
    def matrix(self, t: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
        raise NotImplementedError


class SwayOp(AnimOp):
    def __init__(self, axis: int, amp_deg: float, cycles: int = 1):
        self.axis = axis
        self.amp_rad = math.radians(amp_deg)
        self.cycles = cycles

    def matrix(self, t: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
        ang = self.amp_rad * math.sin(2.0 * math.pi * self.cycles * t)
        return _rotation_matrix(self.axis, ang, pivot)


class SpinOp(AnimOp):
    def __init__(self, axis: int, turns: int):
        self.axis = axis
        self.turns = turns

    def matrix(self, t: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
        ang = math.radians(360.0 * self.turns * t)
        return _rotation_matrix(self.axis, ang, pivot)


class BobOp(AnimOp):
    def __init__(self, axis: int, amp_m: float, cycles: int = 1):
        self.axis = axis
        self.amp_m = amp_m
        self.cycles = cycles

    def matrix(self, t: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
        offset = self.amp_m * math.sin(2.0 * math.pi * self.cycles * t)
        return _translation_matrix(self.axis, offset)


class PulseOp(AnimOp):
    def __init__(self, axis: int, amp_deg: float, peak: float = 0.35):
        self.axis = axis
        self.amp_rad = math.radians(amp_deg)
        self.peak = peak

    def matrix(self, t: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
        e = _pulse_envelope(t, self.peak)
        ang = self.amp_rad * e
        return _rotation_matrix(self.axis, ang, pivot)


class KickOp(AnimOp):
    def __init__(self, axis: int, amp_m: float, peak: float = 0.2):
        self.axis = axis
        self.amp_m = amp_m
        self.peak = peak

    def matrix(self, t: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
        e = _pulse_envelope(t, self.peak)
        offset = self.amp_m * e
        return _translation_matrix(self.axis, offset)


class TipOp(AnimOp):
    def __init__(self, axis: int, final_deg: float):
        self.axis = axis
        self.final_rad = math.radians(final_deg)

    def matrix(self, t: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
        ang = self.final_rad * t * t
        return _rotation_matrix(self.axis, ang, pivot)


class SinkOp(AnimOp):
    def __init__(self, axis: int, final_m: float):
        self.axis = axis
        self.final_m = final_m

    def matrix(self, t: float, pivot: tuple[float, float, float] | np.ndarray) -> np.ndarray:
        offset = self.final_m * t
        return _translation_matrix(self.axis, offset)


# Public factory functions
def sway(axis: int, amp_deg: float, cycles: int = 1) -> AnimOp:
    return SwayOp(axis, amp_deg, cycles)


def spin(axis: int, turns: int) -> AnimOp:
    return SpinOp(axis, turns)


def bob(axis: int, amp_m: float, cycles: int = 1) -> AnimOp:
    return BobOp(axis, amp_m, cycles)


def pulse(axis: int, amp_deg: float, peak: float = 0.35) -> AnimOp:
    return PulseOp(axis, amp_deg, peak)


def kick(axis: int, amp_m: float, peak: float = 0.2) -> AnimOp:
    return KickOp(axis, amp_m, peak)


def tip(axis: int, final_deg: float) -> AnimOp:
    return TipOp(axis, final_deg)


def sink(axis: int, final_m: float) -> AnimOp:
    return SinkOp(axis, final_m)
