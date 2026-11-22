#!/usr/bin/env python3
import sys
import math

def check_scene_ppm(path):
    try:
        with open(path, "rb") as f:
            header = f.readline()
            if header.strip() != b"P6":
                print(f"FAIL: Not a P6 PPM file (got {header!r})")
                return 1

            line = f.readline()
            while line.startswith(b"#"):
                line = f.readline()

            dims = line.strip().split()
            while len(dims) < 2:
                line = f.readline()
                if not line:
                    break
                if not line.startswith(b"#"):
                    dims.extend(line.strip().split())

            if len(dims) < 2:
                print("FAIL: Could not parse dimensions")
                return 1

            w, h = int(dims[0]), int(dims[1])
            if w <= 0 or h <= 0:
                print(f"FAIL: Invalid dimensions {w}x{h}")
                return 1

            line = f.readline()
            while line.startswith(b"#"):
                line = f.readline()

            max_val = int(line.strip())
            if max_val != 255:
                print(f"FAIL: Expected max value 255, got {max_val}")
                return 1

            pixel_data = f.read()
            expected_len = w * h * 3
            if len(pixel_data) != expected_len:
                print(f"FAIL: Expected {expected_len} bytes, got {len(pixel_data)}")
                return 1

            lumas = []
            for i in range(0, expected_len, 3):
                r = pixel_data[i]
                g = pixel_data[i+1]
                b = pixel_data[i+2]
                luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
                lumas.append(luma)

            mean = sum(lumas) / len(lumas)
            variance = sum((x - mean) ** 2 for x in lumas) / len(lumas)
            std = math.sqrt(variance)

            if not (8.0 <= mean <= 247.0):
                print(f"FAIL: Mean luminance {mean:.2f} not in [8, 247]")
                return 1

            if std < 6.0:
                print(f"FAIL: Luminance std {std:.2f} < 6.0")
                return 1

            print(f"SCENE OK {w}x{h} mean={mean:.2f} std={std:.2f}")
            return 0

    except Exception as e:
        print(f"FAIL: {e}")
        return 1

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: check_scene_ppm.py <file.ppm>")
        sys.exit(1)
    sys.exit(check_scene_ppm(sys.argv[1]))
