#!/usr/bin/env python3
"""Hidden HTTP supervisor: bounded probes, owned-child restart, backoff."""
import argparse
import http.client
import json
import os
import subprocess
import sys
import time
from pathlib import Path

from runtime_support import atomic_json, log_event, restart_delay, singleton

HERE = Path(__file__).resolve().parent
SERVER = HERE / "usage_collector.py"
STATE_DIR = Path.home() / ".ai-usage-dashboard"
CREATE_NO_WINDOW = 0x08000000


def read_token():
    token = os.environ.get("AI_DASH_DEVICE_TOKEN", "")
    if token:
        return token
    try:
        return (HERE / "token.local").read_text(encoding="utf-8").strip()
    except OSError:
        return ""


def health_check(host, port, token, expected_pid, timeout=5):
    """Check identity and dashboard responsiveness; stale sources are OK."""
    for route in ("/healthz", "/v1/dashboard"):
        conn = http.client.HTTPConnection(host, port, timeout=timeout)
        try:
            conn.request("GET", route, headers={"Authorization": "Bearer " + token})
            response = conn.getresponse()
            raw = response.read(65537)
            if len(raw) > 65536:
                return False
            if route == "/v1/dashboard" and response.status == 503:
                return True  # source error, not a stuck HTTP process
            if response.status != 200:
                return False
            body = json.loads(raw)
            if route == "/healthz":
                if body.get("service") != "ai-usage-collector" or body.get("pid") != expected_pid:
                    return False
            elif body.get("schema_version") != 1 or not isinstance(body.get("providers"), list):
                return False
        except (OSError, ValueError, http.client.HTTPException, AttributeError):
            return False
        finally:
            conn.close()
    return True


def supervise(args):
    state = Path(args.state_dir)
    lock = singleton(state / "collector_server_daemon.lock")
    if lock is None:
        return 0
    status_file = state / "collector_server_status.json"
    log_file = state / "collector_server_daemon.log"
    status = {"pid": os.getpid(), "running": True, "port": args.port,
              "restart_count": 0, "consecutive_failures": 0}
    def publish():
        status["heartbeat_at"] = int(time.time())
        try:
            atomic_json(status_file, status)
        except OSError as exc:
            log_event(log_file, "status_write_failed " + type(exc).__name__)

    host = "127.0.0.1" if args.host == "0.0.0.0" else args.host
    proc = None
    restart_failures = 0
    try:
        while True:
            token = read_token()
            if not token:
                status["error"] = "missing_device_token"
                publish()
                log_event(log_file, status["error"])
                time.sleep(60)
                continue
            started = time.monotonic()
            try:
                # Popen retains the actual process handle; never kill by port.
                proc = subprocess.Popen(
                    [sys.executable, str(SERVER), "--host", args.host, "--port", str(args.port)],
                    cwd=str(HERE), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    creationflags=CREATE_NO_WINDOW,
                )
                status.update(child_pid=proc.pid, child_started_at=time.time(),
                              error="starting", last_healthy_at=None, consecutive_failures=0)
                publish()
                time.sleep(args.startup_grace)
                while proc.poll() is None:
                    healthy = health_check(host, args.port, token, proc.pid, args.probe_timeout)
                    if healthy:
                        status.update(last_healthy_at=int(time.time()), consecutive_failures=0, error=None)
                        if time.monotonic() - started > 300:
                            restart_failures = 0
                    else:
                        status["consecutive_failures"] += 1
                        status["error"] = "http_probe_failed"
                    publish()
                    if status["consecutive_failures"] >= args.failure_threshold:
                        log_event(log_file, f"unhealthy owned child={proc.pid}; restarting")
                        proc.kill()
                        proc.wait(timeout=5)
                        break
                    time.sleep(args.check_seconds)
                status["error"] = "child_exit_" + str(proc.poll())
            except (OSError, subprocess.SubprocessError) as exc:
                status["error"] = "child_failed_" + type(exc).__name__
            finally:
                if proc is not None and proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)
            restart_failures += 1
            delay = restart_delay(restart_failures)
            status["restart_count"] += 1
            status["next_restart_at"] = int(time.time() + delay)
            publish()
            log_event(log_file, f"{status['error']}; retry_seconds={delay}")
            time.sleep(delay)
    finally:
        if proc is not None and proc.poll() is None:
            proc.kill()
        lock.close()


def main():
    parser = argparse.ArgumentParser(description="Collector HTTP supervisor")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8770)
    parser.add_argument("--state-dir", default=str(STATE_DIR))
    parser.add_argument("--check-seconds", type=float, default=30)
    parser.add_argument("--probe-timeout", type=float, default=5)
    parser.add_argument("--startup-grace", type=float, default=5)
    parser.add_argument("--failure-threshold", type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or min(args.check_seconds, args.probe_timeout, args.startup_grace, args.failure_threshold) <= 0:
        parser.error("invalid port or nonpositive probe timing")
    return supervise(args)


if __name__ == "__main__":
    raise SystemExit(main())
