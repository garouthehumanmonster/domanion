"""rig.py - Skeletal hierarchy, animation assignment, and forward kinematics."""

from dataclasses import dataclass
import numpy as np

import params
from anim import AnimOp


@dataclass
class RigPart:
    name: str
    pivot: np.ndarray  # float64 [3]
    parent: int


class Rig:
    """Rig representing a model's skeletal hierarchy with up to 16 parts."""

    def __init__(self):
        self.parts: list[RigPart] = []
        self.ops: dict[tuple[int, str], list[AnimOp]] = {}

    def add_part(self, name: str, pivot: tuple[float, float, float] | np.ndarray, parent: int = -1) -> int:
        idx = len(self.parts)
        if idx >= params.MAX_PARTS:
            raise ValueError(f"Exceeded MAX_PARTS ({params.MAX_PARTS})")

        if idx == 0:
            if parent != -1:
                raise ValueError("Root part 0 must have parent == -1")
        else:
            if not (0 <= parent < idx):
                raise ValueError(f"Part {idx} parent {parent} must be in [0, {idx - 1}]")

        p_arr = np.asarray(pivot, dtype=np.float64)
        self.parts.append(RigPart(name=name, pivot=p_arr, parent=parent))
        return idx

    def set_ops(self, part: int, clip_name: str, ops: list[AnimOp]) -> None:
        if not (0 <= part < len(self.parts)):
            raise ValueError(f"Invalid part index {part}")
        self.ops[(part, clip_name)] = list(ops)

    def validate(self) -> None:
        """Validate hierarchy constraints."""
        n = len(self.parts)
        if n == 0:
            raise ValueError("Rig has 0 parts")
        if n > params.MAX_PARTS:
            raise ValueError(f"Rig has {n} parts > {params.MAX_PARTS}")
        if self.parts[0].parent != -1:
            raise ValueError("Part 0 must be root (parent -1)")
        for i in range(1, n):
            p = self.parts[i].parent
            if not (0 <= p < i):
                raise ValueError(f"Part {i} parent {p} invalid")
            # Trace chain to 0
            curr = p
            visited = {i}
            while curr != 0:
                if curr in visited or curr < 0 or curr >= n:
                    raise ValueError(f"Cycle or invalid chain in part {i}")
                visited.add(curr)
                curr = self.parts[curr].parent

    def world_matrices(self, clip_index: int, frame: int) -> np.ndarray:
        """Compute 16 4x4 world matrices for a given clip and frame."""
        clip_name, frame_count, looping = params.CLIPS[clip_index]
        if looping:
            t = frame / float(frame_count)
        else:
            t = (frame / float(frame_count - 1)) if frame_count > 1 else 0.0

        n = len(self.parts)
        world_mats = [np.eye(4, dtype=np.float64) for _ in range(params.MAX_PARTS)]

        for i in range(n):
            part = self.parts[i]
            ops_list = self.ops.get((i, clip_name), [])

            local = np.eye(4, dtype=np.float64)
            for op in ops_list:
                local = local @ op.matrix(t, part.pivot)

            if part.parent == -1:
                world_mats[i] = local
            else:
                world_mats[i] = world_mats[part.parent] @ local

        return np.stack(world_mats, axis=0)
