import subprocess
import sys
from pathlib import Path


def run(binary, *arguments):
    return subprocess.run([str(binary), *arguments], capture_output=True, text=True, timeout=10)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: orbital_relief_options_test.py EXPERIMENTAL_KOSMOS")
    binary = Path(sys.argv[1]).resolve()
    valid = run(binary, "--orbital-relief-patches", "0", "--orbital-relief-meshes", "1500",
                "--orbital-relief-textures", "256", "--version")
    assert valid.returncode == 0 and "Kosmos" in valid.stdout, valid.stderr
    for flag, values in {
        "--orbital-relief-patches": ("-1", "1", "abc", "2147483648", ""),
        "--orbital-relief-meshes": ("-1", "1", "1x"),
        "--orbital-relief-textures": ("-1", "9", "2.5"),
    }.items():
        for value in values:
            result = run(binary, flag, value, "--version")
            assert result.returncode != 0 and flag in result.stderr, (flag, value, result)
        result = run(binary, flag)
        assert result.returncode != 0 and flag in result.stderr, (flag, result)
    print("orbital relief options ok")


if __name__ == "__main__":
    main()
