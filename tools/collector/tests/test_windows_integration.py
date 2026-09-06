"""Real Windows subprocess tests in a temp state directory and unused port."""
import http.client
import json
import os
import socket
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
FIXTURE = Path(__file__).with_name('http_probe_fixture.py')
FLAGS = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
POWERSHELL = str(Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe')


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def until(predicate, timeout=35):
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, ValueError, KeyError, http.client.HTTPException):
            pass
        time.sleep(.2)
    raise AssertionError('timed out waiting for test condition')


@unittest.skipUnless(os.name == 'nt', 'Windows subprocess ownership test')
class WindowsIntegration(unittest.TestCase):
    def test_stale_daemon_stop_requires_matching_start_time(self):
        sys.path.insert(0, str(HERE))
        import claude_daemon_watchdog as watchdog
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fake = root/'claude_refresh_daemon.py'
            shutil.copyfile(FIXTURE, fake)
            shutil.copyfile(HERE/'process_control.ps1', root/'process_control.ps1')
            started = time.time()
            proc = subprocess.Popen([sys.executable, str(fake), '--port', str(free_port())],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=FLAGS)
            status = root/'status.json'
            try:
                with patch.object(watchdog, 'HERE', root), patch.object(watchdog, 'STATUS_FILE', status):
                    for stamp, expected in ((started-100, False), (started, True)):
                        status.write_text(json.dumps({'pid': proc.pid, 'started_at': stamp}))
                        os.utime(status, (time.time()-400, time.time()-400))
                        self.assertEqual(watchdog.stop_stale_daemon(), expected)
                        if not expected:
                            self.assertIsNone(proc.poll())
                    proc.wait(timeout=5)
                print('stale daemon: mismatched start time refused; verified fixture stopped')
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.wait(timeout=5)

    def test_hung_and_crashed_server_recover(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = Path(tmp)
            port = free_port()
            env = os.environ.copy()
            env['AI_DASH_DEVICE_TOKEN'] = 'fixture-only-token'
            env['COLLECTOR_TEST_HANG_MARKER'] = str(state/'hung-once')
            code = 'import sys; from pathlib import Path; sys.path.insert(0,sys.argv.pop(1)); import collector_server_daemon as d; d.SERVER=Path(sys.argv.pop(1)); d.main()'
            proc = subprocess.Popen([sys.executable, '-B', '-c', code, str(HERE), str(FIXTURE),
                '--host', '127.0.0.1', '--port', str(port), '--state-dir', tmp,
                '--check-seconds', '.3', '--probe-timeout', '.5', '--startup-grace', '.5', '--failure-threshold', '2'],
                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=FLAGS)
            status_path = state/'collector_server_status.json'
            def status():
                return json.loads(status_path.read_text())
            try:
                first = until(lambda: status().get('child_pid'))
                recovered = until(lambda: status() if status().get('child_pid') != first and status().get('last_healthy_at') else None)
                self.assertGreaterEqual(recovered['restart_count'], 1)
                self.assertTrue((state/'hung-once').exists())
                second = recovered['child_pid']
                conn = http.client.HTTPConnection('127.0.0.1', port, timeout=2)
                try:
                    conn.request('GET', '/crash')
                    conn.getresponse()
                except (OSError, http.client.HTTPException):
                    pass
                finally:
                    conn.close()
                final = until(lambda: status() if status().get('child_pid') != second and status().get('error') is None and status().get('last_healthy_at') else None)
                self.assertGreaterEqual(final['restart_count'], 2)
                print(f'owned HTTP child recovery: {first} -> {second} -> {final["child_pid"]}')
            finally:
                proc.kill()
                proc.wait(timeout=5)
                if status_path.exists():
                    current = status()
                    if current.get('child_pid'):
                        result = subprocess.run([POWERSHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                            str(HERE/'process_control.ps1'), '-StopScript', str(FIXTURE),
                            '-ExpectedPid', str(current['child_pid']), '-ExpectedStartEpoch', str(current['child_started_at'])],
                            capture_output=True, timeout=20, creationflags=FLAGS)
                        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))

    def test_foreign_port_survives_install_and_remove(self):
        port = free_port()
        proc = subprocess.Popen([sys.executable, str(FIXTURE), '--port', str(port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=FLAGS)
        def ready():
            conn = http.client.HTTPConnection('127.0.0.1', port, timeout=1)
            try:
                conn.request('GET', '/healthz')
                return json.loads(conn.getresponse().read())['pid'] == proc.pid
            finally:
                conn.close()
        try:
            until(ready)
            for option in ([], ['-Remove']):
                result = subprocess.run([POWERSHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(HERE/'setup_server_autostart.ps1'), '-Port', str(port)] + option,
                    env=dict(os.environ, AI_DASH_DEVICE_TOKEN='fixture-only-token'),
                    capture_output=True, timeout=20, creationflags=FLAGS)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'owned by another service', result.stderr)
                self.assertIsNone(proc.poll())
                self.assertTrue(ready())
            print(f'foreign port {port}: PID {proc.pid} survived install/remove')
        finally:
            proc.kill()
            proc.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
