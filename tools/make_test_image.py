#!/usr/bin/env python3
"""Generate a random 128x160 24-bit BMP for testing the display.

Usage: python3 tools/make_test_image.py [output.bmp] [--seed N]

No third-party packages needed. Each run produces a different image: a random
colour gradient background with random circles and stripes on top, plus a
white border so you can check the image lines up with the screen edges.
"""
import math
import random
import struct
import sys

W, H = 128, 160


def main():
    args = sys.argv[1:]
    seed = None
    if "--seed" in args:
        i = args.index("--seed")
        seed = int(args[i + 1])
        del args[i:i + 2]
    out = args[0] if args else "test.bmp"
    rng = random.Random(seed)

    # Background: blend between four random corner colours.
    corners = [[rng.randrange(256) for _ in range(3)] for _ in range(4)]
    img = [[None] * W for _ in range(H)]
    for y in range(H):
        v = y / (H - 1)
        for x in range(W):
            u = x / (W - 1)
            img[y][x] = [
                int((1 - u) * (1 - v) * corners[0][c] + u * (1 - v) * corners[1][c]
                    + (1 - u) * v * corners[2][c] + u * v * corners[3][c])
                for c in range(3)
            ]

    # Diagonal stripes in a random colour, semi-transparent.
    sc = [rng.randrange(256) for _ in range(3)]
    period, angle = rng.randint(10, 30), rng.uniform(0, math.pi)
    ca, sa = math.cos(angle), math.sin(angle)
    for y in range(H):
        for x in range(W):
            if int((x * ca + y * sa) / period) % 2 == 0:
                img[y][x] = [(img[y][x][c] + sc[c]) // 2 for c in range(3)]

    # Random filled circles.
    for _ in range(rng.randint(5, 12)):
        cx, cy, r = rng.randrange(W), rng.randrange(H), rng.randint(6, 30)
        col = [rng.randrange(256) for _ in range(3)]
        for y in range(max(0, cy - r), min(H, cy + r + 1)):
            for x in range(max(0, cx - r), min(W, cx + r + 1)):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    img[y][x] = col

    # 1-pixel white border.
    for x in range(W):
        img[0][x] = img[H - 1][x] = [255, 255, 255]
    for y in range(H):
        img[y][0] = img[y][W - 1] = [255, 255, 255]

    # Write 24-bit bottom-up BMP (rows stored as B,G,R, padded to 4 bytes).
    row_size = (W * 3 + 3) & ~3
    pixel_bytes = row_size * H
    with open(out, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", 54 + pixel_bytes, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, pixel_bytes,
                            2835, 2835, 0, 0))
        pad = b"\0" * (row_size - W * 3)
        for y in range(H - 1, -1, -1):
            f.write(b"".join(bytes((p[2], p[1], p[0])) for p in img[y]) + pad)
    print(f"Wrote {out} ({W}x{H}, 24-bit)")


if __name__ == "__main__":
    main()
