"""Runs what CI runs (.github/workflows/build.yml), locally: the lint checks,
the host tests, the config tool tests and the firmware builds.

    python tools/check.py                    everything
    python tools/check.py lint tools         only those steps
    python tools/check.py --fix lint         reformat (clang-format, ruff) instead of checking
    python tools/check.py firmware --boards pico pico_w
    python tools/check.py -v                 show every command's output, not only on failure

Steps:
    lint      clang-format and ruff, at the versions pinned in build.yml (through uvx
              if it's there, otherwise the ones on PATH)
    tests     the C++ host tests (tests/, GoogleTest), built in build-check/tests
    tools     the config tool tests (tests/tools, pytest)
    firmware  the firmware for each board, built in build-check/<board>

Tools it looks for besides PATH: cmake and ninja from the Pico VS Code extension
(~/.pico-sdk) for the firmware, and on Windows MSYS2's usr/bin (C:/msys64 or
D:/msys64, or --msys) for the host tests' compiler, make and GoogleTest. The
firmware's CMakeLists.txt finds the Pico SDK and the Arm toolchain itself.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
BUILD = ROOT / "build-check"
STEPS = ("lint", "tests", "tools", "firmware")
BOARDS = ("pico", "pico_w", "pico2", "pico2_w")  # As CI's matrix
CPP_FILES = ("src/*.cpp", "src/*.hpp", "src/*.h", "tests/*.cpp", "tests/*.hpp", "tests/*.h")  # As CI's clang-format


class Runner:
    def __init__(self, verbose):
        self.verbose = verbose
        self.results = []  # (name, ok, seconds)

    def run(self, name, commands, env=None):
        """Runs the commands in order, stopping at the first that fails."""
        print(f"--- {name}", flush=True)
        start = time.monotonic()
        ok = True
        for command in commands:
            if self.verbose:
                print("$ " + " ".join(str(c) for c in command), flush=True)
            # Found on the step's own PATH: Windows looks only in this process's
            program = shutil.which(str(command[0]), path=(env or os.environ).get("PATH"))
            if program is None:
                print(f"Not found: {command[0]}")
                ok = False
                break
            try:
                result = subprocess.run(
                    [program, *(str(c) for c in command[1:])],
                    check=False,
                    cwd=ROOT,
                    env=env,
                    stdout=None if self.verbose else subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    errors="replace",
                )
            except OSError as e:
                print(f"Couldn't run {program}: {e}")
                ok = False
                break
            if result.returncode != 0:
                if not self.verbose:
                    print("$ " + " ".join(str(c) for c in command))
                    print(result.stdout.rstrip())
                ok = False
                break
        seconds = time.monotonic() - start
        self.results.append((name, ok, seconds))
        print(f"    {'OK' if ok else 'FAILED'} ({seconds:.1f} s)", flush=True)
        return ok

    def skip(self, name, reason):
        print(f"--- {name}\n    SKIPPED: {reason}", flush=True)
        self.results.append((name, None, 0.0))


def pinned_version(name):
    """CLANG_FORMAT_VERSION / RUFF_VERSION from the workflow's env."""
    match = re.search(rf"^\s*{name}:\s*([\w.]+)", WORKFLOW.read_text(encoding="utf-8"), re.MULTILINE)
    return match.group(1) if match else None


def pinned_tool(tool, version_name):
    """The command for a lint tool at CI's version: uvx if it's there."""
    version = pinned_version(version_name)
    if shutil.which("uvx") and version:
        return ["uvx", f"{tool}@{version}"]
    if shutil.which(tool):
        print(f"    (uvx not found: {tool} from PATH, CI uses {version})")
        return [tool]
    return None


def with_path(*dirs):
    """The environment with these directories first on PATH."""
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([*(str(d) for d in dirs if d), env.get("PATH", "")])
    return env


def newest(pattern):
    """The last match of a glob under ~/.pico-sdk (versions sort by name), or None."""
    matches = sorted((Path.home() / ".pico-sdk").glob(pattern))
    return matches[-1] if matches else None


def firmware_env():
    """cmake and ninja, from PATH or the Pico VS Code extension."""
    dirs = []
    if not shutil.which("cmake"):
        dirs.append(newest("cmake/v*/bin"))
    if not shutil.which("ninja"):
        dirs.append(newest("ninja/v*"))
    return with_path(*dirs)


def host_env(msys):
    """A host compiler with GoogleTest: on Windows MSYS2's usr/bin first."""
    if os.name != "nt":
        return dict(os.environ)
    roots = [Path(msys)] if msys else [Path("C:/msys64"), Path("D:/msys64")]
    for root in roots:
        if (root / "usr" / "bin" / "c++.exe").exists():
            return with_path(root / "usr" / "bin")
    return None


def lint(runner, fix):
    clang_format = pinned_tool("clang-format", "CLANG_FORMAT_VERSION")
    ruff = pinned_tool("ruff", "RUFF_VERSION")
    files = subprocess.run(
        ["git", "ls-files", *CPP_FILES], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.split()
    if clang_format is None:
        runner.skip("clang-format", "neither uvx nor clang-format found")
    elif fix:
        runner.run("clang-format (fix)", [[*clang_format, "-i", "--style=file", *files]])
    else:
        runner.run("clang-format", [[*clang_format, "--dry-run", "--Werror", "--style=file", *files]])
    if ruff is None:
        runner.skip("ruff", "neither uvx nor ruff found")
    elif fix:
        runner.run("ruff (fix)", [[*ruff, "check", "--fix", "."], [*ruff, "format", "."]])
    else:
        runner.run("ruff", [[*ruff, "check", "."], [*ruff, "format", "--check", "."]])


def host_tests(runner, msys, jobs):
    env = host_env(msys)
    if env is None:
        runner.skip("host tests", "no MSYS2 found (C:/msys64, D:/msys64), pass --msys <its folder>")
        return
    build = BUILD / "tests"
    runner.run(
        "host tests",
        [
            ["cmake", "-S", "tests", "-B", build],
            ["cmake", "--build", build, "-j", jobs],
            ["ctest", "--test-dir", build, "--output-on-failure", "--timeout", "120"],
        ],
        env=env,
    )


def tool_tests(runner):
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    args = ["tests/tools", "-q", "-rs", "-p", "no:cacheprovider"]
    try:
        import pytest  # noqa: F401
        import serial  # noqa: F401

        command = [sys.executable, "-m", "pytest", *args]
    except ImportError:
        if not shutil.which("uvx"):
            runner.skip("config tool tests", "needs pytest and pyserial (pip install pytest pyserial), or uvx")
            return
        command = ["uvx", "--with", "pyserial", "pytest", *args]
    runner.run("config tool tests", [command], env=env)


def firmware(runner, boards, jobs):
    env = firmware_env()
    for board in boards:
        build = BUILD / board
        runner.run(
            f"firmware ({board})",
            [
                ["cmake", "-S", ".", "-B", build, "-G", "Ninja", f"-DPICO_BOARD={board}"],
                ["cmake", "--build", build, "-j", jobs],
            ],
            env=env,
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("steps", nargs="*", metavar="step", help=f"Steps to run: {', '.join(STEPS)} (default: all)")
    parser.add_argument(
        "--boards", nargs="+", default=list(BOARDS), choices=BOARDS, help="Boards for the firmware step"
    )
    parser.add_argument("--fix", action="store_true", help="lint: reformat the files instead of checking them")
    parser.add_argument("--msys", help="MSYS2 folder for the host tests on Windows (default: C:/msys64 or D:/msys64)")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4, help="Parallel build jobs")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show every command's output")
    args = parser.parse_args()
    steps = args.steps or STEPS
    unknown = [step for step in steps if step not in STEPS]
    if unknown:
        parser.error(f"unknown step {', '.join(unknown)}: pick from {', '.join(STEPS)}")

    runner = Runner(args.verbose)
    if "lint" in steps:
        lint(runner, args.fix)
    if "tests" in steps:
        host_tests(runner, args.msys, args.jobs)
    if "tools" in steps:
        tool_tests(runner)
    if "firmware" in steps:
        firmware(runner, args.boards, args.jobs)

    print("\nSummary:")
    for name, ok, seconds in runner.results:
        state = "skipped" if ok is None else "ok" if ok else "FAILED"
        print(f"  {state:8} {name} ({seconds:.1f} s)")
    failed = [name for name, ok, _ in runner.results if ok is False]
    skipped = [name for name, ok, _ in runner.results if ok is None]
    if failed:
        print(f"\n{len(failed)} failed: {', '.join(failed)}")
        return 1
    print("\nAll passed" + (f" ({len(skipped)} skipped)" if skipped else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
