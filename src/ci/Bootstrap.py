"""GitHub Actions 自举环境适配器；核心编译器保持项目和 CI 无关。"""

import argparse
import io
import json
import os
from pathlib import Path
import pwd
import re
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import urllib.parse
import zipfile


def artifact_suffix():
    return os.environ["GITHUB_RUN_ATTEMPT"]

def artifact_name(name):
    return os.environ.get("POCOCC_CI_ARTIFACT_PREFIX", "") + name + "-" + artifact_suffix()


def positive_setting(name, fallback):
    value = int(os.environ.get(name, str(fallback)))
    if value <= 0:
        raise RuntimeError(f"{name} must be positive")
    return value


class GithubRedirect(urllib.request.HTTPRedirectHandler):
    """跨域跳转下载 artifact 时不转发 GitHub 凭据。"""

    def redirect_request(self, request, response, code, message, headers, new_url):
        redirected = super().redirect_request(request, response, code, message, headers, new_url)
        if redirected is not None and urllib.parse.urlsplit(new_url).netloc != urllib.parse.urlsplit(request.full_url).netloc:
            redirected.remove_header("Authorization")
        return redirected


def api(path):
    request = urllib.request.Request(
        "https://api.github.com/" + path,
        headers={
            "Authorization": "Bearer " + os.environ["GH_TOKEN"],
            "Accept": "application/vnd.github+json",
            "User-Agent": "pococc-bootstrap",
        },
    )
    with urllib.request.build_opener(GithubRedirect()).open(request, timeout=30) as response:
        return response.read()


def artifacts():
    repository = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    return json.loads(api(f"repos/{repository}/actions/runs/{run_id}/artifacts?per_page=100"))["artifacts"]


def wait_artifact(name, filename, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            for artifact in artifacts():
                if artifact["name"] == name and not artifact["expired"]:
                    repository = os.environ["GITHUB_REPOSITORY"]
                    archive = api(f"repos/{repository}/actions/artifacts/{artifact['id']}/zip")
                    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
                        return bundle.read(filename)
        except (urllib.error.URLError, OSError):
            pass
        time.sleep(positive_setting("POCOCC_CI_POLL_INTERVAL", 3))
    raise RuntimeError(f"Timed out waiting for artifact {name}/{filename}")


def free_port():
    with socket.socket() as endpoint:
        endpoint.bind(("127.0.0.1", 0))
        return endpoint.getsockname()[1]


def wait_port(port, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError(f"Port {port} did not become ready")


def start(state, name, command):
    state.mkdir(parents=True, exist_ok=True)
    with (state / f"{name}.log").open("a") as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=stream, start_new_session=True)
    processes_path = state / "processes.json"
    processes = json.loads(processes_path.read_text()) if processes_path.exists() else []
    processes = [entry for entry in processes if entry["name"] != name]
    processes.append({"name": name, "pid": process.pid})
    processes_path.write_text(json.dumps(processes))
    return process


def stop(state):
    process_file = state / "processes.json"
    if process_file.exists():
        for process in reversed(json.loads(process_file.read_text())):
            try:
                os.killpg(process["pid"], signal.SIGTERM)
            except ProcessLookupError:
                pass
    # sshd 由 root 启动，其 pid 单独记录且仍需 sudo 清理。
    sshd_pid = state / "sshd.pid"
    if sshd_pid.exists():
        pid = sshd_pid.read_text().strip()
        if pid.isdigit():
            subprocess.run(["sudo", "-n", "kill", pid], check=False, capture_output=True)


def seed_binary(name):
    return str(Path(os.environ.get("POCOCC_CI_BINARY_DIR", "seed"), name).resolve())


def worker(options):
    state = options.state.resolve()
    state.mkdir(parents=True, exist_ok=True)
    public_key = wait_artifact(
        artifact_name("coordinator-key"), "coordinator.pub",
        timeout=positive_setting("POCOCC_CI_RENDEZVOUS_TIMEOUT", 600)
    ).decode().strip()
    if not re.fullmatch(r"ssh-ed25519 [A-Za-z0-9+/=]+(?: [^\n]+)?", public_key):
        raise RuntimeError("Invalid coordinator public key")
    identity = pwd.getpwuid(os.getuid()).pw_name
    compile_port = free_port()
    ssh_port = free_port()
    authorized = state / "authorized_keys"
    authorized.write_text(
        f'restrict,port-forwarding,permitopen="127.0.0.1:{compile_port}" ' + public_key + "\n"
    )
    authorized.chmod(0o600)
    host_key = state / "host"
    subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(host_key)], check=True)
    configuration = state / "sshd_config"
    configuration.write_text(
        f"Port {ssh_port}\nListenAddress 127.0.0.1\nHostKey {host_key}\n"
        f"PidFile {state / 'sshd.pid'}\nAuthorizedKeysFile {authorized}\n"
        f"AllowUsers {identity}\nPasswordAuthentication no\nKbdInteractiveAuthentication no\n"
        "PubkeyAuthentication yes\nPermitRootLogin no\nUsePAM yes\nStrictModes yes\n"
        f"AllowTcpForwarding local\nPermitOpen 127.0.0.1:{compile_port}\n"
        "PermitTTY no\nX11Forwarding no\nAllowAgentForwarding no\n"
        "ForceCommand /usr/bin/false\nLogLevel VERBOSE\n"
    )
    (state / "sshd.log").touch(mode=0o600)
    subprocess.run(
        ["sudo", "-n", "/usr/sbin/sshd", "-f", str(configuration), "-E", str(state / "sshd.log")],
        check=True,
    )
    wait_port(ssh_port, 10)
    start(
        state, "worker",
        [seed_binary("pococc"), "worker", "--listen", "127.0.0.1", "--allow", "127.0.0.1",
         "--port", str(compile_port), "--jobs", str(positive_setting("POCOCC_CI_WORKER_SLOTS", 2)),
         "--compiler", os.environ["CI_COMPILER"]],
    )
    wait_port(compile_port, 10)
    hostname = None
    for attempt in range(3):
        log = state / "tunnel.log"
        offset = log.stat().st_size if log.exists() else 0
        tunnel = start(
            state, "tunnel",
            [seed_binary("cloudflared"), "tunnel", "--no-autoupdate", "--protocol", "http2",
             "--url", f"ssh://127.0.0.1:{ssh_port}"],
        )
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            text = log.read_text()[offset:]
            matches = [
                name for name in re.findall(r"https://([a-z0-9-]+\.trycloudflare\.com)", text)
                if name != "api.trycloudflare.com"
            ]
            if matches and "Registered tunnel connection" in text:
                hostname = matches[0]
                break
            if tunnel.poll() is not None:
                break
            time.sleep(1)
        if hostname is not None:
            break
        if tunnel.poll() is None:
            os.killpg(tunnel.pid, signal.SIGTERM)
            tunnel.wait(timeout=5)
        print(f"Quick Tunnel attempt {attempt + 1} failed; retrying", flush=True)
        time.sleep(2)
    if hostname is None:
        raise RuntimeError("Quick Tunnel did not become ready: " + text[-2000:])
    compiler_version = subprocess.check_output([os.environ["CI_COMPILER"], "--version"], text=True)
    endpoint = {
        "worker": options.worker,
        "hostname": hostname,
        "username": identity,
        "host_key": host_key.with_suffix(".pub").read_text().strip(),
        "compiler_version": compiler_version,
        "slots": positive_setting("POCOCC_CI_WORKER_SLOTS", 2),
        "compile_port": compile_port,
        "run_id": os.environ["GITHUB_RUN_ID"],
    }
    (state / "endpoint.json").write_text(json.dumps(endpoint, indent=2))
    print(f"worker={options.worker} endpoint=ready")


def connect(options):
    state = options.state.resolve()
    state.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([os.environ["CI_COMPILER"], "--version"], text=True)
    hosts = []
    for number in range(1, options.workers + 1):
        endpoint = json.loads(wait_artifact(
            artifact_name(f"worker-endpoint-{number}"), "endpoint.json",
            timeout=positive_setting("POCOCC_CI_RENDEZVOUS_TIMEOUT", 600)
        ))
        if endpoint["run_id"] != os.environ["GITHUB_RUN_ID"] or endpoint["worker"] != number:
            raise RuntimeError("Worker endpoint belongs to a different run or worker")
        if endpoint["compiler_version"] != version:
            raise RuntimeError(f"Compiler version differs on worker {number}")
        if not re.fullmatch(r"[a-z0-9-]+\.trycloudflare\.com", endpoint["hostname"]):
            raise RuntimeError("Invalid tunnel hostname")
        if not re.fullmatch(r"[a-z_][a-z0-9_-]*", endpoint["username"]):
            raise RuntimeError("Invalid worker SSH username")
        if not re.fullmatch(r"ssh-ed25519 [A-Za-z0-9+/=]+(?: [^\n]+)?", endpoint["host_key"]):
            raise RuntimeError("Invalid worker host key")
        if not isinstance(endpoint["compile_port"], int) or not 1 <= endpoint["compile_port"] <= 65535:
            raise RuntimeError("Invalid worker compile port")
        ssh_port = free_port()
        compiler_port = free_port()
        proxy = start(
            state, f"proxy-{number}",
            [seed_binary("cloudflared"), "access", "tcp", "--hostname", endpoint["hostname"],
             "--url", f"127.0.0.1:{ssh_port}"],
        )
        wait_port(ssh_port, 10)
        known_hosts = state / f"known-hosts-{number}"
        known_hosts.write_text(f"[127.0.0.1]:{ssh_port} {endpoint['host_key']}\n")
        private_key = Path(os.environ["RUNNER_TEMP"]) / "pococc-auth/id"
        command = [
            "ssh", "-N", "-T", "-p", str(ssh_port), "-i", str(private_key),
            "-o", "BatchMode=yes", "-o", "IdentitiesOnly=yes",
            "-o", "StrictHostKeyChecking=yes", "-o", f"UserKnownHostsFile={known_hosts}",
            "-o", "ExitOnForwardFailure=yes", "-o", "ConnectTimeout=10",
            "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3",
            "-L", f"127.0.0.1:{compiler_port}:127.0.0.1:{endpoint['compile_port']}",
            endpoint["username"] + "@127.0.0.1",
        ]
        deadline = time.monotonic() + 90
        connected = False
        while time.monotonic() < deadline:
            tunnel = start(state, f"ssh-{number}", command)
            try:
                wait_port(compiler_port, 12)
                connected = tunnel.poll() is None
                if connected:
                    break
            except RuntimeError:
                pass
            if tunnel.poll() is None:
                os.killpg(tunnel.pid, signal.SIGTERM)
                tunnel.wait(timeout=5)
            time.sleep(2)
        if not connected or proxy.poll() is not None:
            raise RuntimeError(f"Cannot authenticate tunnel to worker {number}")
        hosts.append(f"127.0.0.1:{compiler_port}/{endpoint['slots']},lzo")
        print(f"worker={number} authenticated_tunnel=ready")
    with Path(os.environ["GITHUB_ENV"]).open("a") as stream:
        stream.write("POCOCC_CI_HOSTS=" + " ".join(hosts) + "\n")


def hold(options):
    deadline = time.monotonic() + positive_setting("POCOCC_CI_HOLD_TIMEOUT", 900)
    marker = artifact_name("coordinator-finished")
    while time.monotonic() < deadline:
        processes = json.loads((options.state / "processes.json").read_text())
        for process in processes:
            try:
                os.kill(process["pid"], 0)
            except ProcessLookupError as error:
                raise RuntimeError(f"Worker service stopped: {process['name']}") from error
        try:
            if any(artifact["name"] == marker for artifact in artifacts()):
                print("Coordinator finished; stopping worker")
                return
        except urllib.error.URLError:
            pass
        time.sleep(positive_setting("POCOCC_CI_POLL_INTERVAL", 3))
    raise RuntimeError("Coordinator did not publish its completion marker")


def verify_build(options):
    records = [line.split("\t") for line in (options.build / "bootstrap.tsv").read_text().splitlines()]
    commands = json.loads((options.build / "compile_commands.json").read_text())
    if len(records) != len(commands):
        raise RuntimeError(f"Expected {len(commands)} compile calls, found {len(records)}")
    if not all(len(row) == 7 and row[1] == "distcc" and row[2] == "0" and row[6] == "strict" for row in records):
        raise RuntimeError("Self-bootstrap used local compilation, fallback or a failed compile")
    report = {
        "expected_compiles": len(commands),
        "distcc_submissions": len(records),
        "failures": 0,
        "strict": True,
        "run_id": os.environ.get("GITHUB_RUN_ID", "local"),
    }
    (options.build / "bootstrap.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


def verify_worker(options):
    log = (options.state / "worker.log").read_text()
    completed = [
        line for line in log.splitlines()
        if "COMPILE_OK exit:0" in line and ("/src/" in line or "/tests/" in line)
    ]
    report = {
        "worker": json.loads((options.state / "endpoint.json").read_text())["worker"],
        "self_compiles": len(completed),
        "run_id": os.environ.get("GITHUB_RUN_ID", "local"),
    }
    (options.state / "report.json").write_text(json.dumps(report, indent=2))
    if not completed:
        raise RuntimeError("Worker never compiled pococc source files")
    print(json.dumps(report))


def verify_evidence(options):
    if os.environ["COORDINATOR_RESULT"] != "success" or os.environ["WORKER_RESULT"] != "success":
        raise RuntimeError("Coordinator or worker jobs failed")
    reports = [json.loads(path.read_text()) for path in options.directory.rglob("report.json")]
    builds = [json.loads(path.read_text()) for path in options.directory.rglob("bootstrap.json")]
    if len(reports) != 2 or len(builds) != 1:
        raise RuntimeError("Missing bootstrap evidence")
    if any(report["run_id"] != os.environ["GITHUB_RUN_ID"] for report in reports + builds):
        raise RuntimeError("Bootstrap evidence belongs to a different run")
    actual = sum(report["self_compiles"] for report in reports)
    # 单元测试本身在 tests/ 下，worker 计数也包括该编译单元。
    if actual != builds[0]["distcc_submissions"]:
        raise RuntimeError(f"Workers compiled {actual} objects, expected {builds[0]['distcc_submissions']}")
    if sorted(report["worker"] for report in reports) != [1, 2] or any(report["self_compiles"] < 1 for report in reports):
        raise RuntimeError("Both independent worker jobs must participate")
    summary = {
        "bootstrap": builds[0], "workers": reports, "remote_objects": actual
    }
    print(json.dumps(summary, indent=2))
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as stream:
        stream.write(
            f"## Distributed self-bootstrap passed\n\nStage 0 compiled stage 1 through **{actual} remote objects** "
            "on **2 independent GitHub-hosted worker runners**. Strict mode prohibited local fallback. "
            "Stage 1 passed unit and integration tests.\n\n"
        )
        for report in reports:
            stream.write(f"- Worker {report['worker']}: {report['self_compiles']} remote objects\n")


def main():
    parser = argparse.ArgumentParser()
    subcommands = parser.add_subparsers(dest="command", required=True)
    worker_parser = subcommands.add_parser("worker")
    worker_parser.add_argument("--worker", type=int, required=True)
    worker_parser.add_argument("--state", type=Path, required=True)
    connect_parser = subcommands.add_parser("connect")
    connect_parser.add_argument("--workers", type=int, required=True)
    connect_parser.add_argument("--state", type=Path, required=True)
    for name in ("hold", "stop", "verify-worker"):
        subcommands.add_parser(name).add_argument("--state", type=Path, required=True)
    subcommands.add_parser("verify-build").add_argument("--build", type=Path, required=True)
    subcommands.add_parser("verify-evidence").add_argument("--directory", type=Path, required=True)
    options = parser.parse_args()
    handlers = {
        "worker": worker, "connect": connect, "hold": hold,
        "stop": lambda args: stop(args.state), "verify-worker": verify_worker,
        "verify-build": verify_build, "verify-evidence": verify_evidence,
    }
    try:
        handlers[options.command](options)
    except (RuntimeError, OSError, subprocess.SubprocessError, KeyError, ValueError) as error:
        print(f"Bootstrap error: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
