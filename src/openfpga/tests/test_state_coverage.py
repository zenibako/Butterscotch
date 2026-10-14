"""Exercise the coverage CLI's frame validation and save/load protocol."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / "tools/state-coverage.sh"

# Stand in for the desktop process: the CLI must validate its inputs and
# observe successful restoration, even when two fresh runs draw equal pixels.
GAME = r'''#!/usr/bin/env python3
from pathlib import Path
import os
import sys
if os.getenv("BS_PLAYBACK"):
    Path(os.environ["BS_PLAYBACK"]).read_text()
if os.getenv("BS_STATE_PROBE"):
    print("State probe\t1\t0\tfixture\tallowed\tok\t8\t", file=sys.stderr)
if os.getenv("BS_STATE_SAVE_PATH"):
    previous = Path("save-attempt")
    if not (os.getenv("TEST_REFUSE_SECOND_SAVE") == "1" and previous.exists()):
        Path(os.environ["BS_STATE_SAVE_PATH"]).write_bytes(b"snapshot")
    else:
        print("Save state save: refused", file=sys.stderr)
    previous.touch()
if os.getenv("BS_STATE_LOAD_PATH"):
    refused = os.getenv("TEST_REFUSE_LOAD") == "1"
    print("Save state load: " + ("refused" if refused else "ok") + " (8 bytes)", file=sys.stderr)
if os.getenv("BS_DUMP_PATH"):
    Path(os.environ["BS_DUMP_PATH"]).write_bytes(b"P6\n1 1\n255\n\0\0\0")
'''


class StateCoverageTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        port = self.root / "port"
        (port / "tools").mkdir(parents=True)
        (port / "games/undertale").mkdir(parents=True)
        (port / "games/undertale/data.win").touch()
        self.tool = port / "tools/state-coverage.sh"
        shutil.copyfile(TOOL, self.tool)
        binary = port / "undertale_pc"
        binary.write_text(GAME)
        binary.chmod(0o755)

    def run_tool(self, *args, environment=None):
        env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
        env.update(environment or {})
        return subprocess.run(
            ["bash", str(self.tool), "-x", "60", *args], cwd=self.root,
            env=env,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=15,
        )

    def test_rejects_comparison_before_restore_or_saved_frame(self):
        for pair in ["10:20", "10:40", "60:50", "60:60"]:
            with self.subTest(pair=pair):
                result = self.run_tool(pair)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertNotIn("identical", result.stdout)

    def test_rejects_malformed_frame_pairs(self):
        for pair in ["10", "10:50:60", "-1:60", "abc:60", "10:abc", "0:60", "2147483648:2147483649"]:
            with self.subTest(pair=pair):
                result = self.run_tool("--", pair)
                self.assertEqual(result.returncode, 2, result.stdout)

    def test_resolves_relative_recording_from_callers_directory(self):
        (self.root / "my recording.json").write_text("{}")
        result = self.run_tool("-p", "my recording.json")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("fixture", result.stdout)

    def test_failed_load_cannot_pass_equal_frame_comparison(self):
        result = self.run_tool("-e", "TEST_REFUSE_LOAD=1", "10:60")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertNotIn("identical", result.stdout)
        self.assertIn("load", result.stdout)

    def test_recordings_can_be_probed_but_not_round_trip_compared(self):
        (self.root / "recording.json").write_text("{}")
        result = self.run_tool("-p", "recording.json", "10:60")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("playback cursor", result.stdout)
        self.assertNotIn("identical", result.stdout)

    def test_recordings_from_extra_environment_cannot_be_compared(self):
        recording = self.root / "recording.json"
        recording.write_text("{}")
        result = self.run_tool("-e", f"BS_PLAYBACK={recording}", "10:60")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("playback cursor", result.stdout)

    def test_recordings_from_inherited_environment_cannot_be_compared(self):
        recording = self.root / "recording.json"
        recording.write_text("{}")
        result = self.run_tool("10:60", environment={"BS_PLAYBACK": str(recording)})
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("playback cursor", result.stdout)

    def test_successful_load_compares_later_frame(self):
        result = self.run_tool("10:60")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("save 10 -> compare 60: identical", result.stdout)

    def test_failed_load_cannot_leave_state_for_next_pair(self):
        result = self.run_tool("-e", "TEST_REFUSE_LOAD=1", "-e", "TEST_REFUSE_SECOND_SAVE=1",
                               "10:60", "20:70")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("save 20 -> compare 70: no state", result.stdout)


if __name__ == "__main__":
    unittest.main()
