#!/usr/bin/env python3
import sys

def parse_ppm_p6(path):
    with open(path, 'rb') as f:
        magic = f.readline().strip()
        if magic != b'P6':
            print(f"Error: expected P6 magic, got {magic}")
            sys.exit(1)
        
        # Read header tokens, skipping comments
        tokens = []
        while len(tokens) < 3:
            line = f.readline()
            if not line:
                break
            line = line.split(b'#')[0].strip()
            if not line:
                continue
            tokens.extend(line.split())
        
        if len(tokens) < 3:
            print("Error: invalid PPM header")
            sys.exit(1)
        
        w = int(tokens[0])
        h = int(tokens[1])
        maxval = int(tokens[2])
        if maxval != 255:
            print(f"Error: maxval must be 255, got {maxval}")
            sys.exit(1)
        
        data = f.read()
        expected_len = w * h * 3
        if len(data) != expected_len:
            print(f"Error: data size mismatch: got {len(data)}, expected {expected_len} ({w}x{h}x3)")
            sys.exit(1)
        
        return w, h, data

def main():
    if len(sys.argv) < 2:
        print("Usage: check_ppm.py <path.ppm>")
        sys.exit(1)
    
    path = sys.argv[1]
    w, h, data = parse_ppm_p6(path)
    
    if w <= 0 or h <= 0:
        print(f"Error: invalid dimensions {w}x{h}")
        sys.exit(1)
    
    # Require not all pixels identical
    first_pixel = data[:3]
    all_same = True
    for i in range(3, len(data), 3):
        if data[i:i+3] != first_pixel:
            all_same = False
            break
    if all_same:
        print("Error: all pixels in PPM are identical")
        sys.exit(1)
    
    # Mean luminance of rightmost 5% vs leftmost 5% columns
    # Rec. 709 luminance or standard luminance: Y = 0.2126*R + 0.7152*G + 0.0722*B
    col_5pct = max(1, int(w * 0.05))
    left_cols = range(0, col_5pct)
    right_cols = range(w - col_5pct, w)
    
    left_lum_sum = 0.0
    left_count = 0
    right_lum_sum = 0.0
    right_count = 0
    
    for y in range(h):
        row_offset = y * w * 3
        for x in left_cols:
            idx = row_offset + x * 3
            r = data[idx]
            g = data[idx + 1]
            b = data[idx + 2]
            y_lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
            left_lum_sum += y_lum
            left_count += 1
        
        for x in right_cols:
            idx = row_offset + x * 3
            r = data[idx]
            g = data[idx + 1]
            b = data[idx + 2]
            y_lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
            right_lum_sum += y_lum
            right_count += 1
    
    left_mean = left_lum_sum / max(1, left_count)
    right_mean = right_lum_sum / max(1, right_count)
    diff = right_mean - left_mean
    
    if diff < 40.0:
        print(f"Error: right mean ({right_mean:.2f}) does not exceed left mean ({left_mean:.2f}) by >= 40 (diff={diff:.2f})")
        sys.exit(1)
    
    print(f"PPM OK {w}x{h}")
    sys.exit(0)

if __name__ == '__main__':
    main()
