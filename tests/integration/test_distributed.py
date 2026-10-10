"""使用真实 Clang、Ninja 和两个 distccd 验证编译器入口契约。"""

import argparse
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time


def checked(command, environment, cwd):
    result = subprocess.run(command, env=environment, cwd=cwd, text=True, capture_output=True, timeout=60)
    if result.returncode != 0:
        raise AssertionError(f"Command failed: {command}\n{result.stdout}\n{result.stderr}")
    return result


def available_port():
    with socket.socket() as endpoint:
        endpoint.bind(("127.0.0.1", 0))
        return endpoint.getsockname()[1]


def wait_worker(process, port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"Worker exited with {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("Worker did not become ready")


def events(path):
    if not path.exists():
        return []
    result = [line.split("\t") for line in path.read_text().splitlines()]
    assert all(len(row) == 7 for row in result), "Journal records must remain intact under concurrent writes"
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pococc", required=True)
    parser.add_argument("--launcher", required=True)
    options = parser.parse_args()
    pococc = str(Path(options.pococc).resolve())
    launcher = str(Path(options.launcher).resolve())
    for executable in ("clang++", "distcc", "distccd", "cmake", "ninja", "ccache"):
        if shutil.which(executable) is None:
            raise AssertionError(f"Required integration tool not found: {executable}")
    compiler = shutil.which("clang++")
    environment = {
        key: value for key, value in os.environ.items()
        if not key.startswith(("POCOCC_", "DISTCC_", "CCACHE_"))
    }
    workers = []
    streams = []
    with tempfile.TemporaryDirectory(prefix="pococc integration ") as directory:
        root = Path(directory)
        ports = [available_port(), available_port()]
        while ports[1] == ports[0]:
            ports[1] = available_port()
        try:
            for index, port in enumerate(ports):
                stream = (root / f"worker-{index}.log").open("w")
                streams.append(stream)
                worker = subprocess.Popen(
                    [pococc, "worker", "--listen", "127.0.0.1", "--allow", "127.0.0.1",
                     "--port", str(port), "--jobs", "2", "--compiler", compiler],
                    env=environment, cwd=root, stdout=stream, stderr=stream, start_new_session=True,
                )
                workers.append(worker)
                wait_worker(worker, port)
            hosts = " ".join(f"127.0.0.1:{port}/2,lzo" for port in ports)
            probe = checked([pococc, "doctor", "--hosts", hosts, "--compiler", compiler], environment, root)
            assert probe.stdout.count("probe=passed") == 2, probe.stdout

            project = root / "sample project"
            project.mkdir()
            build = root / "sample build"
            journal = root / "build.tsv"
            (project / "gen.cmake").write_text('file(WRITE "${OUT}" "#pragma once\\nconstexpr int MAGIC = 5;\\n")\n')
            sources = []
            for index in range(8):
                source = f"unit-{index}.cpp"
                sources.append(source)
                (project / source).write_text(
                    f'#include "generated.hpp"\nint unit_{index}() {{ return {index} + MAGIC; }}\n'
                )
            (project / "pch.hpp").write_text("#pragma once\n#include <vector>\n")
            (project / "pch.cpp").write_text("int pch_answer() { return std::vector<int>{40, 2}[0] + 2; }\n")
            (project / "main.cpp").write_text(
                "#include <iostream>\nint pch_answer();\n" +
                "".join(f"int unit_{index}();\n" for index in range(8)) +
                "int main() { std::cout << pch_answer()" +
                "".join(f" + unit_{index}()" for index in range(8)) + ' << "\\n"; }\n'
            )
            (project / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\nproject(sample LANGUAGES CXX)\n'
                'set(CMAKE_CXX_STANDARD 20)\nset(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)\n'
                'add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated.hpp"\n'
                ' COMMAND "${CMAKE_COMMAND}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/generated.hpp"\n'
                ' -P "${CMAKE_CURRENT_SOURCE_DIR}/gen.cmake" VERBATIM)\n'
                'add_library(pch STATIC pch.cpp)\ntarget_precompile_headers(pch PRIVATE pch.hpp)\n'
                'add_executable(sample main.cpp ' + " ".join(sources) + ' "${CMAKE_CURRENT_BINARY_DIR}/generated.hpp")\n'
                'target_include_directories(sample PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")\n'
                'target_link_libraries(sample PRIVATE pch)\n'
                'target_compile_options(sample PRIVATE -Wall -Wextra -Wpedantic -Werror)\n'
            )
            checked(
                ["cmake", "-S", str(project), "-B", str(build), "-G", "Ninja",
                 f"-DCMAKE_CXX_COMPILER={compiler}", f"-DCMAKE_CXX_COMPILER_LAUNCHER={launcher}"],
                environment, root,
            )
            checked(
                [pococc, "run", "--hosts", hosts, "--journal", str(journal),
                 "--", "cmake", "--build", str(build), "-j8"],
                environment, root,
            )
            assert checked([str(build / "sample")], environment, root).stdout.strip() == "110"
            records = events(journal)
            assert sum(row[1] == "distcc" for row in records) == 9, records
            assert sum(row[1] == "local" for row in records) == 2, records
            assert all(row[6] == "strict" and row[2] == "0" for row in records), records
            assert "distcc_submissions=9" in checked([pococc, "stats", str(journal)], environment, root).stdout
            print("PASS: CMake/Ninja, generated header, forced response files, PCH, 9 remote submissions")

            completed_compiles = 0
            for index in range(2):
                streams[index].flush()
                text = (root / f"worker-{index}.log").read_text()
                assert "COMPILE_OK exit:0" in text, text
                completed_compiles += text.count("COMPILE_OK exit:0")
            assert completed_compiles == 11, completed_compiles
            print("PASS: worker logs confirm 9 build compilations plus 2 strict probes completed remotely")

            compile_environment = dict(
                environment, POCOCC_HOSTS=hosts, POCOCC_JOURNAL=str(root / "direct.tsv"),
                POCOCC_STRICT="1",
            )
            (root / "inner.rsp").write_text("-D'VALUE=41' -std=c++20")
            (root / "outer.rsp").write_text("@inner.rsp -c 'source $(touch SENTINEL).cpp' -o 'odd object.o'")
            (root / "source $(touch SENTINEL).cpp").write_text("int value() { return VALUE + 1; }\n")
            checked([launcher, compiler, "@outer.rsp"], compile_environment, root)
            assert (root / "odd object.o").exists() and not (root / "SENTINEL").exists()
            print("PASS: nested GNU response files, paths with spaces, no shell evaluation")

            (root / "bad.cpp").write_text("int broken( { return 0; }\n")
            failed = subprocess.run(
                [launcher, compiler, "-c", "bad.cpp", "-o", "bad.o"],
                env=compile_environment, cwd=root, text=True, capture_output=True, timeout=15,
            )
            assert failed.returncode != 0 and "error:" in failed.stderr and not (root / "bad.o").exists()
            assert events(root / "direct.tsv")[-1][2] != "0"
            print("PASS: compiler errors and exit status survive the remote backend")

            dead_port = available_port()
            offline = dict(compile_environment, POCOCC_HOSTS=f"127.0.0.1:{dead_port}/1")
            (root / "good.cpp").write_text("int value() { return 42; }\n")
            rejected = subprocess.run(
                [launcher, compiler, "-c", "good.cpp", "-o", "strict.o"],
                env=offline, cwd=root, text=True, capture_output=True, timeout=15,
            )
            assert rejected.returncode != 0 and not (root / "strict.o").exists()
            fallback = dict(offline, POCOCC_STRICT="0")
            checked([launcher, compiler, "-c", "good.cpp", "-o", "fallback.o"], fallback, root)
            assert (root / "fallback.o").exists()
            print("PASS: strict worker failure cannot silently compile locally; explicit fallback works")

            alias = root / "clang++-recursive"
            alias.symlink_to(launcher)
            recursive = subprocess.run(
                [launcher, str(alias), "-c", "good.cpp", "-o", "recursive.o"],
                env=compile_environment, cwd=root, text=True, capture_output=True, timeout=5,
            )
            assert recursive.returncode != 0 and "resolves to pococc itself" in recursive.stderr
            malformed = subprocess.run(
                [launcher, compiler, "-c", "good.cpp", "-o", "malformed.o"],
                env=dict(compile_environment, POCOCC_STRICT="invalid"), cwd=root,
                text=True, capture_output=True, timeout=5,
            )
            assert malformed.returncode != 0 and "POCOCC_STRICT" in malformed.stderr
            assert checked([pococc, "--version"], environment, root).stdout.strip() == "pococc 0.1.0"
            print("PASS: recursive executable aliases and malformed configuration fail explicitly")

            cache_journal = root / "cache.tsv"
            cache_environment = dict(
                compile_environment, CCACHE_DIR=str(root / "ccache"), CCACHE_CONFIGPATH="/dev/null",
                CCACHE_PREFIX=launcher, POCOCC_JOURNAL=str(cache_journal),
            )
            for _ in range(2):
                checked(["ccache", compiler, "-c", "good.cpp", "-o", "cached.o"], cache_environment, root)
            assert len(events(cache_journal)) == 1, events(cache_journal)
            print("PASS: ccache hit bypasses pococc and the remote backend")

            local_driver = root / "local-driver"
            intervals = root / "intervals.tsv"
            local_driver.write_text(
                "#!/usr/bin/python3\nimport os,time\nfrom pathlib import Path\n"
                "start=time.monotonic_ns()\ntime.sleep(0.15)\n"
                "with Path(" + repr(str(intervals)) + ").open('a') as stream:\n"
                " stream.write(str(start)+'\\t'+str(time.monotonic_ns())+'\\n')\n"
            )
            local_driver.chmod(0o755)
            local_environment = dict(
                environment, POCOCC_BACKEND="local", POCOCC_LOCAL_JOBS="1",
                POCOCC_STATE_DIR=str(root / "local-slots"),
            )
            local_jobs = [
                subprocess.Popen(
                    [launcher, str(local_driver), "-c", "good.cpp"],
                    env=local_environment, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                )
                for _ in range(4)
            ]
            try:
                for job in local_jobs:
                    output, diagnostics = job.communicate(timeout=5)
                    assert job.returncode == 0, (output, diagnostics)
            finally:
                for job in local_jobs:
                    if job.poll() is None:
                        job.kill()
                        job.communicate()
            spans = sorted(tuple(map(int, row.split("\t"))) for row in intervals.read_text().splitlines())
            assert len(spans) == 4 and all(spans[i][1] <= spans[i + 1][0] for i in range(3)), spans
            print("PASS: local compiler concurrency respects its own slot limit")

            child_pid = root / "child.pid"
            sleeper = subprocess.Popen(
                [pococc, "run", "--backend", "local", "--", "python3", "-c",
                 "import os,time,pathlib; pathlib.Path(" + repr(str(child_pid)) +
                 ").write_text(str(os.getpid())); time.sleep(60)"],
                env=environment, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            try:
                deadline = time.monotonic() + 5
                while not child_pid.exists() and time.monotonic() < deadline:
                    time.sleep(0.02)
                assert child_pid.exists()
                sleeper.terminate()
                sleeper.communicate(timeout=5)
                assert sleeper.returncode == 143, sleeper.returncode
                try:
                    os.kill(int(child_pid.read_text()), 0)
                except ProcessLookupError:
                    pass
                else:
                    raise AssertionError("Build child survived cancellation")
            finally:
                if sleeper.poll() is None:
                    sleeper.kill()
                    sleeper.communicate()
            print("PASS: build cancellation terminates its process group")
        finally:
            for worker in workers:
                if worker.poll() is None:
                    os.killpg(worker.pid, signal.SIGTERM)
            for worker in workers:
                try:
                    worker.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(worker.pid, signal.SIGKILL)
                    worker.wait(timeout=5)
            for stream in streams:
                stream.close()


if __name__ == "__main__":
    main()
