#!/usr/bin/env python3
"""Independent reference model for AntLang 1.0.

This file intentionally does not share code with the C implementation.  The
test harness uses it as an oracle for parsing, execution and rendering.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import re
from typing import List, Set, Tuple

DIRS = "ESWN"
DX = {"E": 1, "S": 0, "W": -1, "N": 0}
DY = {"E": 0, "S": 1, "W": 0, "N": -1}
RIGHT = {"E": "S", "S": "W", "W": "N", "N": "E"}
LEFT = {v: k for k, v in RIGHT.items()}
MAX_VIEW_CELLS = 4_000_000


class ParseError(Exception):
    pass


@dataclass
class Program:
    width: int
    height: int
    cells: List[str]                 # rows, each a string of B/W
    ax: int
    ay: int
    direction: str
    has_out: bool
    out: bool
    has_loop: bool
    loop: int


@dataclass
class Result:
    ax: int
    ay: int
    direction: str
    steps: int
    halt: str
    trace: List[str] = field(default_factory=list)
    black: Set[Tuple[int, int]] = field(default_factory=set)


def _strip_comment(line: str) -> str:
    cut = len(line)
    for marker in ("#", "//"):
        at = line.find(marker)
        if at != -1:
            cut = min(cut, at)
    return line[:cut]


def _payload(line: str) -> Optional[Tuple[str, str]]:
    m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=(.*)$", line)
    if not m:
        return None
    return m.group(1).upper(), m.group(2).strip()


def _split_row(text: str, line_no: int) -> str:
    text = text.strip()
    if text.endswith(";"):
        text = text[:-1].rstrip()
    if not text:
        raise ParseError(f"line {line_no}: empty BOARD row")
    cells = []
    for token in text.split():
        if token == ";":
            raise ParseError(f"line {line_no}: ';' is only allowed at the end")
        for ch in token.upper():
            if ch not in "BW":
                raise ParseError(f"line {line_no}: invalid cell {ch!r}")
            cells.append(ch)
    if not cells:
        raise ParseError(f"line {line_no}: empty BOARD row")
    return "".join(cells)


def parse(text: str) -> Program:
    width = height = None
    ax = ay = None
    direction = None
    has_out = out = False
    has_loop = False
    loop = 0
    seen = set()
    rows: List[str] = []
    in_board = False
    saw_board = False
    board_line = 0

    for line_no, raw in enumerate(text.splitlines(), 1):
        line = _strip_comment(raw).strip()
        if not line:
            continue
        head = _payload(line)
        if head is not None:
            name, value = head
            in_board = False
            if name == "BOXES":
                if name in seen:
                    raise ParseError(f"line {line_no}: duplicate BOXES")
                seen.add(name)
                m = re.fullmatch(r"\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*;?", value)
                if not m:
                    raise ParseError(f"line {line_no}: bad BOXES")
                width, height = int(m.group(1)), int(m.group(2))
            elif name == "ANT":
                if name in seen:
                    raise ParseError(f"line {line_no}: duplicate ANT")
                seen.add(name)
                m = re.fullmatch(
                    r"\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*([ENWSenws])\s*\)\s*;?",
                    value,
                )
                if not m:
                    raise ParseError(f"line {line_no}: bad ANT")
                ax, ay = int(m.group(1)), int(m.group(2))
                direction = m.group(3).upper()
            elif name == "OUT":
                if name in seen:
                    raise ParseError(f"line {line_no}: duplicate OUT")
                seen.add(name)
                m = re.fullmatch(r"(true|false)\s*;?", value, re.IGNORECASE)
                if not m:
                    raise ParseError(f"line {line_no}: bad OUT")
                has_out = True
                out = m.group(1).lower() == "true"
            elif name == "LOOP":
                if name in seen:
                    raise ParseError(f"line {line_no}: duplicate LOOP")
                seen.add(name)
                m = re.fullmatch(r"(\d+)\s*;?", value)
                if not m:
                    raise ParseError(f"line {line_no}: bad LOOP")
                has_loop = True
                loop = int(m.group(1))
            elif name == "BOARD":
                if saw_board:
                    raise ParseError(f"line {line_no}: duplicate BOARD")
                saw_board = True
                board_line = line_no
                in_board = True
                if value:
                    rows.append(_split_row(value, line_no))
            else:
                raise ParseError(f"line {line_no}: unknown directive {name}")
        elif in_board:
            rows.append(_split_row(line, line_no))
        else:
            raise ParseError(f"line {line_no}: expected a directive")

    if width is None or height is None:
        raise ParseError("missing BOXES")
    if ax is None or ay is None or direction is None:
        raise ParseError("missing ANT")
    if not saw_board:
        raise ParseError("missing BOARD")
    if not seen.intersection({"OUT", "LOOP"}):
        raise ParseError("needs OUT or LOOP")
    if has_out and not out and not has_loop:
        raise ParseError("OUT=false requires LOOP")
    if width < 1 or height < 1:
        raise ParseError("BOXES must be positive")
    if len(rows) != height:
        raise ParseError(f"BOARD has {len(rows)} rows, expected {height}")
    for i, row in enumerate(rows, 1):
        if len(row) != width:
            raise ParseError(
                f"BOARD row {i} has {len(row)} cells, expected {width}"
            )
    if has_out and out and not (1 <= ax <= width and 1 <= ay <= height):
        raise ParseError("initial ant is outside the board with OUT=true")
    return Program(
        width=width,
        height=height,
        cells=rows,
        ax=ax,
        ay=ay,
        direction=direction,
        has_out=has_out,
        out=out,
        has_loop=has_loop,
        loop=loop,
    )


def parse_file(path: str) -> Program:
    with open(path, "r", encoding="utf-8-sig") as f:
        return parse(f.read())


def simulate(program: Program, trace: bool = False) -> Result:
    black = {
        (x + 1, y + 1)
        for y, row in enumerate(program.cells)
        for x, cell in enumerate(row)
        if cell == "B"
    }
    ax, ay = program.ax, program.ay
    direction = program.direction
    steps = 0
    trace_lines: List[str] = []

    while True:
        if program.has_loop and steps >= program.loop:
            halt = "LOOP"
            break
        is_black = (ax, ay) in black
        nd = LEFT[direction] if is_black else RIGHT[direction]
        if is_black:
            black.remove((ax, ay))
        else:
            black.add((ax, ay))
        nx, ny = ax + DX[nd], ay + DY[nd]
        steps += 1
        if trace:
            trace_lines.append(
                f"STEP {steps}: ({ax},{ay}) "
                f"{'B' if is_black else 'W'}, {direction} -> {nd}, "
                f"{'W' if is_black else 'B'}, ({nx},{ny},{nd})"
            )
        ax, ay, direction = nx, ny, nd
        if program.out and not (1 <= ax <= program.width and
                                1 <= ay <= program.height):
            halt = "OUT"
            break

    return Result(
        ax=ax,
        ay=ay,
        direction=direction,
        steps=steps,
        halt=halt,
        trace=trace_lines,
        black=black,
    )


def render(
    program: Program,
    result: Result,
    view: str = "board",
    show_board: bool = True,
    trace: bool = False,
) -> str:
    lines: List[str] = []
    if trace:
        lines.extend(result.trace)

    x0, y0, x1, y1 = 1, 1, program.width, program.height
    if show_board:
        if view == "bbox":
            if result.black:
                xs = [p[0] for p in result.black]
                ys = [p[1] for p in result.black]
                x0, x1 = min(x0, min(xs)), max(x1, max(xs))
                y0, y1 = min(y0, min(ys)), max(y1, max(ys))
            x0, x1 = min(x0, result.ax), max(x1, result.ax)
            y0, y1 = min(y0, result.ay), max(y1, result.ay)
        elif view != "board":
            raise ValueError(f"unknown view {view!r}")
        vw, vh = x1 - x0 + 1, y1 - y0 + 1
        if vw > MAX_VIEW_CELLS or vh > MAX_VIEW_CELLS or \
                vw * vh > MAX_VIEW_CELLS:
            raise ValueError("view is too large")
        lines.append(f"BOXES = ({program.width}, {program.height})")
        if view == "bbox":
            lines.append(f"VIEW = ({x0}, {y0}, {vw}, {vh})")
        for y in range(y0, y1 + 1):
            row = []
            for x in range(x0, x1 + 1):
                row.append("B" if (x, y) in result.black else "W")
            lines.append(" ".join(row))

    lines.append(f"ANT = ({result.ax}, {result.ay}, {result.direction})")
    lines.append(f"STEPS = {result.steps}")
    lines.append(f"HALT = {result.halt}")
    return "\n".join(lines) + "\n"


def run_file(path: str, view: str = "board", show_board: bool = True,
             trace: bool = False) -> str:
    program = parse_file(path)
    result = simulate(program, trace=trace)
    return render(program, result, view=view, show_board=show_board,
                  trace=trace)


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="AntLang reference interpreter")
    ap.add_argument("file")
    ap.add_argument("--view", choices=("board", "bbox"), default="board")
    ap.add_argument("--no-board", action="store_true")
    ap.add_argument("--trace", action="store_true")
    args = ap.parse_args()
    print(run_file(args.file, args.view, not args.no_board, args.trace), end="")
