#!/usr/bin/env python3
"""Test harness for antc.

By default it checks the interpreter against the independent Python model in
reference.py.  Pass --native to also build every case with the host C compiler
and compare the generated executable, and --fuzz N for randomized programs.
"""

from __future__ import annotations

import argparse
import difflib
import os
from pathlib import Path
import random
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import reference  # noqa: E402

EXE = ".exe" if os.name == "nt" else ""
DEFAULT_ANTC = ROOT / "build" / ("antc" + EXE)
WORK = ROOT / "build" / "test_work"


def find_antc() -> Path:
    env = os.environ.get("ANTC")
    if env:
        return Path(env)
    for candidate in (ROOT / "build" / ("antc" + EXE),
                      ROOT / ("antc" + EXE)):
        if candidate.exists():
            return candidate
    return DEFAULT_ANTC


def norm(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def run(cmd, cwd=ROOT, input_text=None):
    return subprocess.run(
        [str(c) for c in cmd],
        cwd=str(cwd),
        input=input_text,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def diff_text(name: str, actual: str, expected: str) -> str:
    return "".join(
        difflib.unified_diff(
            expected.splitlines(keepends=True),
            actual.splitlines(keepends=True),
            fromfile=name + " (expected)",
            tofile=name + " (actual)",
        )
    )


class Failure(Exception):
    pass


class Runner:
    def __init__(self, antc: Path, native: bool, cc: str | None,
                 fuzz: int, native_fuzz: int):
        self.antc = antc
        self.native = native
        self.cc = cc
        self.fuzz = fuzz
        self.native_fuzz = native_fuzz
        self.passed = 0
        self.failed = 0
        self.failures = []
        self.native_dir = ROOT / "build" / "test_native"

    def ok(self, name: str):
        self.passed += 1
        print(f"PASS  {name}")

    def fail(self, name: str, detail: str):
        self.failed += 1
        self.failures.append((name, detail))
        print(f"FAIL  {name}")
        if detail:
            for line in detail.rstrip().splitlines()[:40]:
                print("      " + line)

    def check(self, name: str, fn):
        try:
            fn()
        except Failure as exc:
            self.fail(name, str(exc))
        except Exception as exc:  # noqa: BLE001 - test harness
            self.fail(name, f"{type(exc).__name__}: {exc}")
        else:
            self.ok(name)

    def expect(self, name: str, args, expected: str):
        self.expect_cmd(name, [self.antc] + list(args), expected)

    def expect_cmd(self, name: str, cmd, expected: str):
        cp = run(cmd)
        if cp.returncode != 0:
            raise Failure(
                f"exit {cp.returncode}\nstdout:\n{cp.stdout}\n"
                f"stderr:\n{cp.stderr}"
            )
        actual = norm(cp.stdout)
        if actual != expected:
            raise Failure(diff_text(name, actual, expected))

    def test_case(self, case: Path):
        program = reference.parse_file(str(case))
        result = reference.simulate(program)
        self.expect(case.name + " board", ["run", case],
                    reference.render(program, result))
        self.expect(case.name + " bbox", ["run", case, "--view", "bbox"],
                    reference.render(program, result, view="bbox"))
        self.expect(case.name + " no-board",
                    ["run", case, "--no-board"],
                    reference.render(program, result, show_board=False))
        if result.steps <= 300:
            traced = reference.simulate(program, trace=True)
            self.expect(case.name + " trace", ["run", case, "--trace"],
                        reference.render(program, traced, trace=True))
        if self.native:
            self.test_native(case, program)

    def build_native(self, case: Path) -> Path:
        self.native_dir.mkdir(parents=True, exist_ok=True)
        exe = self.native_dir / (case.stem + EXE)
        args = [self.antc, "build", case, "-o", exe]
        if self.cc:
            args += ["--cc", self.cc]
        cp = run(args)
        if cp.returncode != 0:
            raise Failure(
                f"native build failed (exit {cp.returncode})\n"
                f"{cp.stdout}\n{cp.stderr}"
            )
        if not exe.exists():
            raise Failure(f"native build did not create {exe}")
        return exe

    def test_native(self, case: Path, program: reference.Program):
        exe = self.build_native(case)
        result = reference.simulate(program)
        self.expect_cmd(case.name + " native board", [exe],
                        reference.render(program, result))
        self.expect_cmd(case.name + " native bbox", [exe, "--view", "bbox"],
                        reference.render(program, result, view="bbox"))
        if result.steps <= 300:
            traced = reference.simulate(program, trace=True)
            self.expect_cmd(case.name + " native trace", [exe, "--trace"],
                            reference.render(program, traced, trace=True))

    def test_invalid(self, case: Path, needle: str):
        cp = run([self.antc, "check", case])
        if cp.returncode != 1:
            raise Failure(
                f"expected exit 1, got {cp.returncode}\n"
                f"stdout:\n{cp.stdout}\nstderr:\n{cp.stderr}"
            )
        err = norm(cp.stderr)
        if "error:" not in err:
            raise Failure(f"no 'error:' in stderr:\n{err}")
        if needle.lower() not in err.lower():
            raise Failure(f"stderr does not contain {needle!r}:\n{err}")

    def test_cli(self):
        cp = run([self.antc, "--version"])
        if cp.returncode != 0 or norm(cp.stdout) != "antc 1.0.0\n":
            raise Failure(f"bad --version: {cp.stdout!r} / {cp.stderr!r}")
        cp = run([self.antc, "--help"])
        if cp.returncode != 0 or "Usage:" not in cp.stdout:
            raise Failure(f"bad --help: {cp.stdout!r}")
        cp = run([self.antc, "run", "tests/cases/basic_out.ant", "--view", "bogus"])
        if cp.returncode != 2:
            raise Failure(f"bad --view should exit 2, got {cp.returncode}")

        # The shorthand must be identical to 'run'.
        a = run([self.antc, "run", "test.ant"])
        b = run([self.antc, "test.ant"])
        if a.returncode != 0 or b.returncode != 0 or a.stdout != b.stdout:
            raise Failure("shorthand 'antc FILE' differs from 'antc run FILE'")

        cfile = WORK / "emit_test.c"
        cp = run([self.antc, "emit", "test.ant", "-o", cfile])
        if cp.returncode != 0:
            raise Failure(f"emit failed: {cp.stderr}")
        text = cfile.read_text(encoding="utf-8")
        for marker in ("#define BOARD_W 6", "int main", "set_has"):
            if marker not in text:
                raise Failure(f"generated C is missing {marker!r}")

    def test_fuzz_one(self, seed: int, native: bool = False):
        rng = random.Random(seed)
        source = make_fuzz_source(rng)
        program = reference.parse(source)
        result = reference.simulate(program)
        WORK.mkdir(parents=True, exist_ok=True)
        case = WORK / f"fuzz_{seed}.ant"
        case.write_text(source, encoding="utf-8")
        self.expect(f"fuzz {seed} board", ["run", case],
                    reference.render(program, result))
        self.expect(f"fuzz {seed} bbox",
                    ["run", case, "--view", "bbox"],
                    reference.render(program, result, view="bbox"))
        if native:
            exe = self.build_native(case)
            self.expect_cmd(f"fuzz {seed} native", [exe],
                            reference.render(program, result))


def make_fuzz_source(rng: random.Random) -> str:
    width = rng.randint(1, 8)
    height = rng.randint(1, 8)
    out = rng.choice([True, False])
    if out:
        ax = rng.randint(1, width)
        ay = rng.randint(1, height)
    else:
        # Mostly inside, sometimes on the infinite plane outside BOXES.
        if rng.random() < 0.8:
            ax = rng.randint(1, width)
            ay = rng.randint(1, height)
        else:
            ax = rng.choice([0, width + 1, -rng.randint(1, 3)])
            ay = rng.choice([0, height + 1, -rng.randint(1, 3)])
    loop = rng.randint(0, 300)
    direction = rng.choice("ENWS")
    if rng.random() < 0.15:
        rows = [["B"] * width for _ in range(height)]
    elif rng.random() < 0.15:
        rows = [["W"] * width for _ in range(height)]
    else:
        rows = [
            [rng.choice("BW") for _ in range(width)]
            for _ in range(height)
        ]
    compact = rng.random() < 0.4
    lines = [
        f"BOXES = ({width}, {height});",
        f"ANT = ({ax}, {ay}, {direction});",
        f"OUT = {'true' if out else 'false'};",
        f"LOOP = {loop};",
    ]
    if rng.random() < 0.5:
        lines.insert(1, "# generated by the fuzz tester")
    lines.append("BOARD =")
    for row in rows:
        text = "".join(row) if compact else " ".join(row)
        if rng.random() < 0.3:
            text += " ;"
        lines.append(text)
    return "\n".join(lines) + "\n"


INVALID = {
    "missing_rule.ant": "at least one",
    "out_false_no_loop.ant": "never halt",
    "ant_outside.ant": "must be inside",
    "width.ant": "has 3 cells",
    "rows.ant": "has 1 row",
    "color.ant": "invalid cell",
    "direction.ant": "invalid direction",
    "duplicate.ant": "duplicate",
    "box_spelling.ant": "spelled BOXES",
    "unknown.ant": "unknown directive",
    "negative_loop.ant": "non-negative",
    "empty.ant": "empty",
    "junk_after.ant": "unexpected text",
    "loop_overflow.ant": "too large",
}


def main() -> int:
    ap = argparse.ArgumentParser(description="run the antc test suite")
    ap.add_argument("--antc", default=str(find_antc()))
    ap.add_argument("--native", action="store_true",
                    help="also build and check generated executables")
    ap.add_argument("--cc", default=os.environ.get("ANT_CC"),
                    help="compiler passed to 'antc build --cc'")
    ap.add_argument("--fuzz", type=int, default=40,
                    help="number of randomized interpreter cases")
    ap.add_argument("--native-fuzz", type=int, default=0,
                    help="number of randomized cases also built natively")
    args = ap.parse_args()

    antc = Path(args.antc)
    if not antc.exists():
        print(f"error: {antc} does not exist; run build.bat or make first",
              file=sys.stderr)
        return 1

    r = Runner(antc, args.native, args.cc, args.fuzz, args.native_fuzz)
    WORK.mkdir(parents=True, exist_ok=True)

    cases = sorted((ROOT / "tests" / "cases").glob("*.ant"))
    for case in cases:
        r.check(case.name, lambda case=case: r.test_case(case))

    for name, needle in INVALID.items():
        case = ROOT / "tests" / "invalid" / name
        r.check("invalid " + name,
                lambda case=case, needle=needle: r.test_invalid(case, needle))

    r.check("cli", r.test_cli)

    for seed in range(1, args.fuzz + 1):
        r.check(f"fuzz {seed}", lambda seed=seed: r.test_fuzz_one(seed))
    for seed in range(1, args.native_fuzz + 1):
        r.check(f"native fuzz {seed}",
                lambda seed=seed: r.test_fuzz_one(seed, native=True))

    print()
    print(f"{r.passed} passed, {r.failed} failed")
    if r.failed:
        print("\nfailures:")
        for name, _ in r.failures:
            print("  " + name)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
