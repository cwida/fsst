"""Run with the paths to the built fsst and/or fsst12 executables."""
import pathlib
import subprocess
import sys
import tempfile


def roundtrip(executable, data, directory):
    source = directory / "input"
    encoded = directory / "encoded"
    decoded = directory / "decoded"
    source.write_bytes(data)
    subprocess.run([executable, str(source), str(encoded)], check=True, timeout=30)
    subprocess.run([executable, "-d", str(encoded), str(decoded)], check=True, timeout=30)
    assert decoded.read_bytes() == data


for executable in sys.argv[1:]:
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        for data in (b"", b"a", b"hello FSST\n" * 100, bytes(range(256)) * 32,
                     b"block boundary\n" * 280000):
            roundtrip(str(pathlib.Path(executable).resolve()), data, directory)
