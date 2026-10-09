#!/usr/bin/env python3
"""Generate game/assets/ui/icons.tga, the rail's icon sprite sheet.

    tools/design/icons.py game/assets/ui/icons.tga

Six 24x24 line icons in a row (dashboard, plots, market, research, finance,
log), white on transparent, drawn procedurally with a tiny rasterizer so no
image library is needed. RmlUi's built-in loader reads uncompressed TGA; the
stylesheet maps each cell through a @spritesheet. Strokes are 2px, drawn at
4x supersampling for smooth edges, in the same geometry as the bench's SVGs.
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

SIZE = 24
SCALE = 4
STROKE = 2.0
ICONS = ["dashboard", "plots", "market", "research", "finance", "log"]


def segment_distance(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> float:
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length2))
    cx, cy = ax + t * dx, ay + t * dy
    return math.hypot(px - cx, py - cy)


class Canvas:
    """One icon cell at SCALE x supersampling; coverage in [0, 1]."""

    def __init__(self) -> None:
        self.n = SIZE * SCALE
        self.cov = [[0.0] * self.n for _ in range(self.n)]

    def line(self, ax: float, ay: float, bx: float, by: float, width: float = STROKE) -> None:
        half = width / 2.0
        x0, x1 = int((min(ax, bx) - half - 1) * SCALE), int((max(ax, bx) + half + 1) * SCALE)
        y0, y1 = int((min(ay, by) - half - 1) * SCALE), int((max(ay, by) + half + 1) * SCALE)
        for y in range(max(0, y0), min(self.n, y1 + 1)):
            for x in range(max(0, x0), min(self.n, x1 + 1)):
                d = segment_distance((x + 0.5) / SCALE, (y + 0.5) / SCALE, ax, ay, bx, by)
                if d <= half:
                    self.cov[y][x] = 1.0

    def rect(self, x: float, y: float, w: float, h: float, width: float = STROKE) -> None:
        self.line(x, y, x + w, y, width)
        self.line(x + w, y, x + w, y + h, width)
        self.line(x + w, y + h, x, y + h, width)
        self.line(x, y + h, x, y, width)

    def polyline(self, points: list[tuple[float, float]], width: float = STROKE) -> None:
        for (ax, ay), (bx, by) in zip(points, points[1:]):
            self.line(ax, ay, bx, by, width)

    def downsample(self) -> list[list[float]]:
        out = [[0.0] * SIZE for _ in range(SIZE)]
        for y in range(SIZE):
            for x in range(SIZE):
                total = 0.0
                for sy in range(SCALE):
                    for sx in range(SCALE):
                        total += self.cov[y * SCALE + sy][x * SCALE + sx]
                out[y][x] = total / (SCALE * SCALE)
        return out


def draw(name: str) -> Canvas:
    c = Canvas()
    if name == "dashboard":  # four tiles
        c.rect(4, 4, 6.5, 6.5)
        c.rect(13.5, 4, 6.5, 6.5)
        c.rect(4, 13.5, 6.5, 6.5)
        c.rect(13.5, 13.5, 6.5, 6.5)
    elif name == "plots":  # folded map
        c.polyline([(4, 6), (9, 4), (15, 6), (20, 4), (20, 18), (15, 20), (9, 18), (4, 20), (4, 6)])
        c.line(9, 4, 9, 18)
        c.line(15, 6, 15, 20)
    elif name == "market":  # rising line with baseline
        c.polyline([(4, 17), (9, 11), (13, 14), (20, 6)])
        c.line(4, 20, 20, 20)
    elif name == "research":  # flask
        c.line(9, 4, 15, 4)
        c.line(10, 4, 10, 10)
        c.line(14, 4, 14, 10)
        c.polyline([(10, 10), (5, 19), (19, 19), (14, 10)])
        c.line(8, 15, 16, 15)
    elif name == "finance":  # bank card
        c.rect(4, 6, 16, 12)
        c.line(4, 10, 20, 10, 2.5)
        c.line(7, 15, 11, 15)
    elif name == "log":  # three lines of text
        c.line(5, 7, 19, 7)
        c.line(5, 12, 19, 12)
        c.line(5, 17, 14, 17)
    return c


def write_tga(path: Path, sheets: list[list[list[float]]]) -> None:
    width, height = SIZE * len(sheets), SIZE
    # Uncompressed true-colour, 32-bit, top-left origin (descriptor bit 5).
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 0x28)
    rows = bytearray()
    for y in range(height):
        for cell in sheets:
            for x in range(SIZE):
                alpha = int(round(cell[y][x] * 255))
                rows += bytes((255, 255, 255, alpha))  # BGRA, white strokes
    path.write_bytes(header + bytes(rows))


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    write_tga(Path(argv[1]), [draw(name).downsample() for name in ICONS])
    print(f"{len(ICONS)} icons written to {argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
