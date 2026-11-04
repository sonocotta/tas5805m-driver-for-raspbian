"""Compile functions from tas58xx.c on the host, with test stubs, and run them.

The tests in this directory take the real function bodies out of the driver
source (so a change to the driver is what gets tested) and build them into a
small C program together with stubs for the kernel APIs they call.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "tas58xx.c").read_text(encoding="utf-8")


def function(name, source=SOURCE):
    """Return the full text of the static function `name`."""
    match = re.search(r"^static [^\n;]*\b" + re.escape(name) + r"\(", source, re.M)
    if match is None:
        raise AssertionError(f"Missing function: {name}")
    brace = source.index("{", match.end())
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():i + 1] + "\n"
    raise AssertionError(f"Unbalanced braces in {name}")


def compiler():
    cc = shutil.which("cc") or shutil.which("gcc")
    if cc is None:
        raise RuntimeError("a C compiler (cc or gcc) is needed for the host tests")
    return cc


class Program:
    """A C program built once in a temporary directory."""

    def __init__(self, code, name="harness"):
        self._dir = tempfile.TemporaryDirectory(prefix="tas58xx-test-")
        src = Path(self._dir.name) / f"{name}.c"
        src.write_text(code, encoding="utf-8")
        self.path = Path(self._dir.name) / name
        result = subprocess.run(
            [compiler(), "-std=gnu11", "-Wall", "-Wno-unused-function",
             "-Wno-unused-variable", "-Wno-unused-but-set-variable",
             "-Werror=implicit-function-declaration", "-g", "-O1",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             "-o", str(self.path), str(src)],
            capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError("harness did not compile:\n" + result.stderr)

    def run(self, *args, stdin=None):
        result = subprocess.run([str(self.path), *map(str, args)], input=stdin,
                                capture_output=True, text=True, timeout=60)
        return result

    def close(self):
        self._dir.cleanup()
