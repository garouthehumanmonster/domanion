"""rng.py - Deterministic 64-bit SplitMix PRNG."""

import struct

M64 = 0xFFFFFFFFFFFFFFFF


class Rng:
    """SplitMix64 deterministic pseudo-random number generator."""

    def __init__(self, seed: int = 0):
        self.state = int(seed) & M64

    def next_u64(self) -> int:
        self.state = (self.state + 0x9E3779B97F4A7C15) & M64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        return z ^ (z >> 31)

    def uniform(self) -> float:
        """Uniform float in [0.0, 1.0)."""
        return (self.next_u64() >> 11) / 9007199254740992.0

    def randint(self, lo: int, hi: int) -> int:
        """Inclusive integer in [lo, hi]."""
        if hi < lo:
            raise ValueError("hi must be >= lo")
        count = hi - lo + 1
        return lo + int(self.next_u64() % count)

    def shuffle(self, lst: list) -> None:
        """In-place Fisher-Yates shuffle from the end."""
        for i in range(len(lst) - 1, 0, -1):
            j = self.randint(0, i)
            lst[i], lst[j] = lst[j], lst[i]
