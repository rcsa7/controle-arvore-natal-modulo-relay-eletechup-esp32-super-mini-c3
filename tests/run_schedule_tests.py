"""Compile and execute the actual firmware scheduler on the local computer."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", help="Path to g++ or clang++")
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("g++") or shutil.which("clang++")
    bundled = Path.home() / ".platformio" / "packages" / "toolchain-gccmingw32" / "bin" / "g++.exe"
    if not compiler and bundled.is_file():
        compiler = str(bundled)
    if not compiler:
        parser.error("C++ compiler missing. Install g++ or use --compiler.")
    environment = os.environ.copy()
    environment["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + environment.get("PATH", "")
    source = Path(__file__).resolve().with_name("schedule_test.cpp")
    with tempfile.TemporaryDirectory(prefix="natal-schedule-") as directory:
        executable = Path(directory) / ("schedule_test.exe" if os.name == "nt" else "schedule_test")
        subprocess.run(
            [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
             str(source), "-o", str(executable)],
            check=True, env=environment,
        )
        subprocess.run([str(executable)], check=True, env=environment)


if __name__ == "__main__":
    main()
