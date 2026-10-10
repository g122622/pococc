"""验证自举计数、跨节点证据及 artifact 重定向的凭据处理。"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import urllib.request
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[2] / "src/ci/Bootstrap.py"
SPEC = importlib.util.spec_from_file_location("bootstrap", SOURCE)
BOOTSTRAP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOOTSTRAP)


class BootstrapTests(unittest.TestCase):
    def test_redirect_strips_credentials_only_across_hosts(self):
        handler = BOOTSTRAP.GithubRedirect()
        request = urllib.request.Request(
            "https://api.github.com/artifact", headers={"Authorization": "Bearer test-value"}
        )
        cross = handler.redirect_request(request, None, 302, "", {}, "https://storage.example/artifact")
        self.assertIsNone(cross.get_header("Authorization"))
        same = handler.redirect_request(request, None, 302, "", {}, "https://api.github.com/download")
        self.assertEqual(same.get_header("Authorization"), "Bearer test-value")

    def test_build_requires_every_compile_to_be_strict_and_distributed(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "compile_commands.json").write_text(json.dumps([{}, {}]))
            journal = build / "bootstrap.tsv"
            journal.write_text(
                "1\tdistcc\t0\t100\ta.cpp\tplain-object-compile\tstrict\n"
                "2\tdistcc\t0\t100\tb.cpp\tplain-object-compile\tstrict\n"
            )
            options = argparse.Namespace(build=build)
            BOOTSTRAP.verify_build(options)
            self.assertEqual(json.loads((build / "bootstrap.json").read_text())["distcc_submissions"], 2)
            for record in (
                "2\tlocal\t0\t100\tb.cpp\tlocal-backend\tstrict\n",
                "2\tdistcc\t1\t100\tb.cpp\tplain-object-compile\tstrict\n",
                "2\tdistcc\t0\t100\tb.cpp\tplain-object-compile\tfallback-allowed\n",
            ):
                journal.write_text("1\tdistcc\t0\t100\ta.cpp\tplain-object-compile\tstrict\n" + record)
                with self.assertRaises(RuntimeError):
                    BOOTSTRAP.verify_build(options)

    def test_worker_counts_only_actual_project_compiles(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory)
            (state / "endpoint.json").write_text('{"worker": 1}')
            log = state / "worker.log"
            log.write_text(
                "COMPILE_OK exit:0 /tmp/pococc-probe/probe.cpp\n"
                "COMPILE_OK exit:0 /checkout/pococc/src/main.cpp\n"
                "COMPILE_OK exit:0 /checkout/pococc/tests/unit/test_invocation.cpp\n"
                "OTHER /checkout/pococc/src/main.cpp\n"
            )
            BOOTSTRAP.verify_worker(argparse.Namespace(state=state))
            self.assertEqual(json.loads((state / "report.json").read_text())["self_compiles"], 2)
            log.write_text("COMPILE_OK exit:0 /tmp/probe.cpp\n")
            with self.assertRaises(RuntimeError):
                BOOTSTRAP.verify_worker(argparse.Namespace(state=state))

    def test_evidence_requires_two_participating_workers_and_exact_total(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for worker in (1, 2):
                state = root / f"worker-{worker}"
                state.mkdir()
                (state / "report.json").write_text(json.dumps({
                    "worker": worker, "self_compiles": 1, "run_id": "test-run"
                }))
            build = root / "build"
            build.mkdir()
            (build / "bootstrap.json").write_text(json.dumps({
                "distcc_submissions": 2, "run_id": "test-run"
            }))
            environment = {
                "COORDINATOR_RESULT": "success", "WORKER_RESULT": "success",
                "GITHUB_RUN_ID": "test-run", "GITHUB_STEP_SUMMARY": str(root / "summary.md"),
            }
            with patch.dict(os.environ, environment):
                BOOTSTRAP.verify_evidence(argparse.Namespace(directory=root))
                (build / "bootstrap.json").write_text(json.dumps({
                    "distcc_submissions": 3, "run_id": "test-run"
                }))
                with self.assertRaises(RuntimeError):
                    BOOTSTRAP.verify_evidence(argparse.Namespace(directory=root))


if __name__ == "__main__":
    unittest.main()
