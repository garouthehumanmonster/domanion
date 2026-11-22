"""model.py - Model and Hardpoint definitions and declarative part builder helper."""

from dataclasses import dataclass
import inspect
from typing import Any, Callable
import numpy as np

from meshbuilder import Part
from rig import Rig


@dataclass
class Hardpoint:
    kind: int
    part: int
    pos: tuple[float, float, float]  # (x, y, z) in metres


@dataclass
class Model:
    id: str
    category: int
    target_dim_m: float
    parts: list[Part]
    rig: Rig
    hardpoints: list[Hardpoint]


def P(
    prim_fn: Callable[..., Any],
    *args: Any,
    part: int = 0,
    at: tuple[float, float, float] = (0.0, 0.0, 0.0),
    rot: tuple[float, float, float] = (0.0, 0.0, 0.0),
    trim: str = "steel_plain",
    team: bool = False,
    emissive: bool = False,
    scroll: bool = False,
    mirror: bool = False,
    keep: bool = False,
    name: str = "",
    **kwargs: Any,
) -> Part:
    """Declarative helper for creating a Part with auto-LOD propagation."""
    sig = inspect.signature(prim_fn)
    takes_lod = "lod" in sig.parameters

    def make(lod: int):
        if takes_lod:
            return prim_fn(*args, lod=lod, **kwargs)
        else:
            return prim_fn(*args, **kwargs)

    return Part(
        part=part,
        make=make,
        pos=at,
        rot_deg=rot,
        mirror_z=mirror,
        trim=trim,
        team=team,
        emissive=emissive,
        scroll=scroll,
        keep=keep,
        name=name,
    )
