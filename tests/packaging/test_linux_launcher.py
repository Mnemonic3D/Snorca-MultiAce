import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class LinuxLauncherTest(unittest.TestCase):
    def test_generated_launchers_preserve_spaces_and_arguments(self):
        root = Path(__file__).resolve().parents[2]
        for name in ("pack_linux.sh", "gitlab_pack_linux.sh"):
            with self.subTest(script=name), tempfile.TemporaryDirectory() as tmp:
                package = Path(tmp) / "package with spaces"
                (package / "bin").mkdir(parents=True)
                stub = package / "bin" / "FOrcaSlicer"
                stub.write_text('#!/bin/bash\nprintf "%s\\n" "$LD_LIBRARY_PATH" "$@"\n')
                stub.chmod(0o755)
                script = (root / name).read_text()
                start = script.index('    cat > "$PACKAGE_DIR/${APP_CMD}" <<EOF')
                end = script.index("\nEOF", start) + len("\nEOF")
                subprocess.run(
                    ["bash", "-c", 'PACKAGE_DIR=$1; APP_CMD=FOrcaSlicer\n' + script[start:end],
                     "test", str(package)], check=True
                )
                env = os.environ.copy()
                env["XDG_SESSION_TYPE"] = "x11"
                result = subprocess.run(
                    ["bash", str(package / "FOrcaSlicer"), "--datadir", "profile with spaces"],
                    env=env, capture_output=True, text=True, check=True
                )
                lines = result.stdout.splitlines()
                self.assertTrue(lines[0].startswith(str(package / "bin") + ":"))
                self.assertEqual(lines[1:], ["--datadir", "profile with spaces"])


if __name__ == "__main__":
    unittest.main()
