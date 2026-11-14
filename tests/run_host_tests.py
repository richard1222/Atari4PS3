import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CXX = os.environ.get("CXX", "g++")
with tempfile.TemporaryDirectory(prefix="atari4ps3-tests-") as work:
    for name, sources in (
        ("audio", [ROOT / "tests/test_audio.cpp"]),
        ("watchdog", [ROOT / "src/common/watchdog.cxx", ROOT / "tests/test_watchdog.cpp"]),
    ):
        exe = Path(work) / (name + (".exe" if os.name == "nt" else ""))
        args = [CXX, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT / "src"),
                "-I", str(ROOT / "src/common"), *map(str, sources), "-o", str(exe)]
        subprocess.run(args, check=True, timeout=60)
        subprocess.run([str(exe)], check=True, timeout=30)
