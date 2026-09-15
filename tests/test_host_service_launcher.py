"""CPU-only service-launch tests: fake systemd; no model or service is started."""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform.startswith("linux") and shutil.which("bash"), "WSL/Linux bash fixture")
class HostServiceLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="dark-factory-launch-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.log = self.root / "calls.jsonl"
        self.launcher = Path(__file__).resolve().parents[1] / "scripts/model/start_host_service.sh"
        fake = self.root / "fake.py"
        fake.write_text("#!" + sys.executable + "\n" + r'''
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
with open(os.environ["DF_TEST_LOG"], "a") as out:
    out.write(json.dumps({"tool": name, "args": sys.argv[1:]}) + "\n")
if name == "systemctl":
    if "show-environment" in sys.argv:
        sys.exit(int(os.environ.get("DF_TEST_MANAGER_EXIT", "0")))
    if "show" in sys.argv:
        print(os.environ.get("DF_TEST_LOAD_STATE", "not-found"))
        sys.exit(0)
    sys.exit("Unexpected systemctl command")
if name == "systemd-run":
    sys.exit(int(os.environ.get("DF_TEST_RUN_EXIT", "0")))
sys.exit("Unexpected fake tool")
''', encoding="utf-8")
        fake.chmod(0o755)
        for name in ("systemctl", "systemd-run"):
            (self.root / name).symlink_to(fake)
        self.env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ["PATH"],
                        DF_TEST_LOG=str(self.log), DF_MODEL_PYTHON=sys.executable,
                        DF_BASE_MODEL="/private/base model", PYTHONPATH="/private/custom:path")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            self.port = probe.getsockname()[1]

    def run_helper(self, *args):
        return subprocess.run(["bash", str(self.launcher), *args], env=self.env,
                              capture_output=True, text=True, timeout=10)

    def calls(self, tool):
        rows = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return [row for row in rows if row["tool"] == tool]

    def assert_no_start(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.calls("systemd-run"), [])

    def test_starts_with_quoted_arguments_environment_and_bounded_restart(self):
        adapter = "/private/adapter with spaces/$(never-run);literal"
        args = ["--port", str(self.port), "--adapter", adapter, "--linear-patch-embedding"]
        result = self.run_helper(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        command = self.calls("systemd-run")[0]["args"]
        self.assertEqual(command[-len(args):], args)
        for required in ("--user", "--unit=dark-factory-panel-model.service", "--collect",
                         "--expand-environment=no", "--property=Type=exec", "--property=Restart=on-failure",
                         "--property=RestartSec=5", "--property=StartLimitIntervalSec=120",
                         "--property=StartLimitBurst=3", "--setenv=DF_MODEL_PYTHON=" + sys.executable,
                         "--setenv=DF_BASE_MODEL=/private/base model",
                         "--setenv=PYTHONPATH=/private/custom:path"):
            self.assertIn(required, command)
        self.assertIn("--working-directory=" + str(self.launcher.parents[2]), command)
        self.assertIn("STARTING", result.stdout)
        self.assertIn("This does not mean ready", result.stdout)

    def test_existing_unit_is_not_replaced_or_stopped(self):
        self.env["DF_TEST_LOAD_STATE"] = "loaded"
        result = self.run_helper("--port", str(self.port))
        self.assert_no_start(result)
        self.assertIn("already exists", result.stderr)
        self.assertTrue(all("stop" not in row["args"] for row in self.calls("systemctl")))

    def test_unknown_unit_inspection_refuses_start(self):
        self.env["DF_TEST_LOAD_STATE"] = ""
        self.assert_no_start(self.run_helper("--port", str(self.port)))

    def test_unavailable_user_manager_refuses_start(self):
        self.env["DF_TEST_MANAGER_EXIT"] = "1"
        self.assert_no_start(self.run_helper("--port", str(self.port)))

    def test_occupied_loopback_port_is_not_reused(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            result = self.run_helper("--port", str(listener.getsockname()[1]))
            self.assert_no_start(result)
            self.assertIn("Cannot reserve loopback port", result.stderr)

    def test_invalid_or_missing_port_refuses_start(self):
        for value in ("0", "65536", "bad"):
            with self.subTest(value=value):
                self.assert_no_start(self.run_helper("--port", value))
        self.assert_no_start(self.run_helper("--port"))

    def test_equals_form_and_last_port_are_forwarded(self):
        args = ["--port", "1", "--port=" + str(self.port)]
        result = self.run_helper(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls("systemd-run")[0]["args"][-len(args):], args)

    def test_missing_python_refuses_start(self):
        self.env["DF_MODEL_PYTHON"] = str(self.root / "no-python")
        self.assert_no_start(self.run_helper("--port", str(self.port)))

    def test_systemd_failure_is_not_reported_as_starting(self):
        self.env["DF_TEST_RUN_EXIT"] = "1"
        result = self.run_helper("--port", str(self.port))
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("STARTING", result.stdout)

    def test_help_does_not_contact_manager_or_start_host(self):
        result = self.run_helper("--help")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(self.calls("systemctl"), [])
        self.assertEqual(self.calls("systemd-run"), [])


if __name__ == "__main__":
    unittest.main()
