"""Exercise verification failures, evidence, partial outcomes, and owned-tree cleanup."""

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import verify


class RunnerTests(unittest.TestCase):
    """Drive real harmless child processes rather than mocking their exit behavior."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def step(self, code: str, name: str = "probe", tests: bool = False) -> verify.Step:
        return verify.Step(name, (sys.executable, "-c", code), tests)

    def test_nonzero_exit_and_stderr_are_preserved(self):
        result = verify.run_step(self.step('import sys; print("failure evidence", file=sys.stderr); sys.exit(7)'), self.root, 5)
        self.assertEqual(result["returncode"], 7)
        self.assertEqual(result["status"], "failed")
        self.assertIn("failure evidence", Path(result["log"]).read_text(encoding="utf-8"))

    def test_missing_command_fails_with_evidence(self):
        result = verify.run_step(verify.Step("missing", (str(self.root / "nonexistent-tool"),)), self.root, 5)
        self.assertEqual(result["status"], "failed")
        self.assertIn("Could not start", Path(result["log"]).read_text(encoding="utf-8"))

    def test_empty_skipped_and_unobserved_tests_cannot_pass(self):
        for text in ("No tests were found!!!", "Running 0 tests from 0 suites.",
                     "Ran 0 tests in 0.00s", "Running 3 tests\n[  SKIPPED ] privacy",
                     "Running 3 tests\nYOU HAVE 1 DISABLED TEST",
                     "Running 3 tests\nYOU HAVE 1 DISABLED TESTS", "tool exited without test evidence"):
            with self.subTest(text=text):
                result = verify.run_step(self.step(f"print({text!r})", tests=True), self.root, 5)
                self.assertEqual(result["status"], "failed")

    def test_real_unittest_skip_cannot_pass(self):
        code = ('import unittest\nclass Acceptance(unittest.TestCase):\n'
                '    @unittest.skip("unavailable proof")\n'
                '    def test_behavior(self):\n        self.fail("never executed")\n'
                'unittest.main()\n')
        result = verify.run_step(self.step(code, tests=True), self.root, 5)
        self.assertEqual(result["returncode"], 0, "unittest itself accepts skipped tests")
        self.assertEqual(result["status"], "failed", "the acceptance gate must reject the missing proof")

    def test_test_children_cannot_inherit_partial_selection(self):
        overrides = {"GTEST_FILTER": "OneSuite.OneTest", "TESTBRIDGE_TEST_ONLY": "OtherSuite.OneTest",
                     "GTEST_TOTAL_SHARDS": "2", "GTEST_SHARD_INDEX": "0"}
        code = ('import os\n'
                f'assert all(name not in os.environ for name in {list(overrides)!r}), "partial selection inherited"\n'
                'print("Running 1 test")\n')
        with patch.dict(verify.os.environ, overrides):
            result = verify.run_step(self.step(code, tests=True), self.root, 5)
        self.assertEqual(result["status"], "passed")
        self.assertEqual(set(result["test_selection_removed"]), set(overrides))

    def test_observed_nonempty_suites_can_pass(self):
        for text in ("1: [==========] Running 37 tests from 5 test suites.", "Ran 1 test in 0.001s\nOK"):
            result = verify.run_step(self.step(f"print({text!r})", tests=True), self.root, 5)
            self.assertEqual(result["status"], "passed")

    def test_fail_fast_leaves_remaining_stage_unrun(self):
        marker = self.root / "should-not-exist"
        steps = [self.step("raise SystemExit(9)", "fail"),
                 self.step(f"from pathlib import Path; Path({str(marker)!r}).touch()", "later")]
        with redirect_stdout(io.StringIO()):
            results = verify.run_plan(steps, self.root, 5)
        self.assertEqual([r["status"] for r in results], ["failed", "not_run"])
        self.assertFalse(marker.exists())

    def test_timeout_stops_owned_descendant(self):
        heartbeat = self.root / "heartbeat"
        child = self.root / "child.py"
        parent = self.root / "parent.py"
        child.write_text('from pathlib import Path\nimport sys,time\np=Path(sys.argv[1])\nn=0\n'
                         'while True:\n    n+=1\n    p.write_text(str(n))\n    time.sleep(0.02)\n', encoding="utf-8")
        parent.write_text('import subprocess,sys,time\nsubprocess.Popen([sys.executable,sys.argv[1],sys.argv[2]])\n'
                          'time.sleep(60)\n', encoding="utf-8")
        result = verify.run_step(verify.Step("timeout", (sys.executable, str(parent), str(child), str(heartbeat))), self.root, 1)
        self.assertEqual(result["status"], "timed_out")
        self.assertTrue(heartbeat.exists(), "the child must actually start before testing cleanup")
        count = heartbeat.read_text(encoding="utf-8")
        time.sleep(0.15)
        self.assertEqual(heartbeat.read_text(encoding="utf-8"), count, "owned child continued after timeout")

    def test_interrupt_stops_owned_process_and_is_recorded(self):
        # Patch only the interrupt event; launch and cleanup still use a real process.
        original_wait = subprocess.Popen.wait
        interrupted = False

        def interrupt_once(process, *args, **kwargs):
            nonlocal interrupted
            if not interrupted:
                interrupted = True
                raise KeyboardInterrupt
            return original_wait(process, *args, **kwargs)

        with patch.object(subprocess.Popen, "wait", interrupt_once):
            result = verify.run_step(self.step("import time; time.sleep(60)"), self.root, 5)
        self.assertEqual(result["status"], "interrupted")
        self.assertIn("interrupted", Path(result["log"]).read_text(encoding="utf-8"))

    def test_full_selects_debug_release_and_asan_but_omission_is_explicit(self):
        for windows in (False, True):
            commands = "\n".join(" ".join(s.command) for s in verify.plan("full", windows=windows))
            for preset in ("debug", "release", "asan"):
                self.assertIn(preset, commands)
            partial = "\n".join(" ".join(s.command) for s in verify.plan("full", "unsupported toolchain", windows))
            self.assertIn("debug", partial)
            self.assertIn("release", partial)
            self.assertNotIn("asan", partial)
        with self.assertRaises(ValueError):
            verify.plan("quick", "skip")
        with self.assertRaises(ValueError):
            verify.plan("full", " ")

    def test_cli_dry_run_has_no_side_effects_or_pass_claim(self):
        with patch.object(verify, "ROOT", self.root), redirect_stdout(io.StringIO()) as output:
            self.assertEqual(verify.main(["workflow", "--dry-run"]), 0)
        self.assertEqual(json.loads(output.getvalue())["status"], "dry_run")
        self.assertEqual(list(self.root.iterdir()), [])

    def run_main(self, argv, step, changed=False):
        before = {"revision": "synthetic", "status": "", "fingerprint": "before"}
        after = dict(before, fingerprint="after") if changed else before
        with patch.object(verify, "ROOT", self.root), patch.object(verify, "plan", return_value=[step]), \
                patch.object(verify, "git_identity", side_effect=[before, after]), redirect_stdout(io.StringIO()):
            code = verify.main(argv)
        evidence = list((self.root / ".cache/verification").glob("*/summary.json"))
        self.assertEqual(len(evidence), 1)
        return code, json.loads(evidence[0].read_text(encoding="utf-8"))

    def test_cli_failure_retains_summary_and_returns_nonzero(self):
        code, summary = self.run_main(["workflow"], self.step('print("proof"); raise SystemExit(4)'))
        self.assertEqual(code, 1)
        self.assertEqual(summary["status"], "failed")
        self.assertEqual(summary["steps"][0]["returncode"], 4)
        self.assertIn("proof", Path(summary["steps"][0]["log"]).read_text(encoding="utf-8"))

    def test_cli_asan_omission_returns_partial_even_when_commands_pass(self):
        code, summary = self.run_main(["full", "--skip-asan", "not supported here"], self.step('print("done")'))
        self.assertEqual(code, 2)
        self.assertEqual(summary["status"], "partial")
        self.assertEqual(summary["skip_asan"], "not supported here")

    def test_changed_tree_cannot_receive_a_passing_verdict(self):
        code, summary = self.run_main(["workflow"], self.step('print("done")'), changed=True)
        self.assertEqual(code, 2)
        self.assertEqual(summary["status"], "incomplete")


if __name__ == "__main__":
    unittest.main()
