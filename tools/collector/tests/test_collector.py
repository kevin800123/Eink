import http.client
import json
import os
import sys
import tempfile
import threading
import time
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import usage_collector as collector
import collector_server_daemon as supervisor
import runtime_support
import claude_daemon_watchdog as watchdog


class FreshnessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.now = time.time()

    def window(self, used=35, expires=None):
        return {"used_percent": used, "resets_at": expires or self.now + 5000}

    def claude(self, age=0, **extra):
        payload = {"captured_at": self.now-age, "five_hour": self.window(), "seven_day": self.window(61)}
        payload.update(extra)
        path = self.root / 'claude.json'
        path.write_text(json.dumps(payload), encoding='utf-8')
        return collector.build_claude_provider(path)

    def test_fresh_and_real_zero_are_preserved(self):
        result = self.claude(five_hour=self.window(0))
        self.assertEqual(result['status'], 'ok')
        self.assertEqual(result['usage_percent'], 0)
        self.assertFalse(result['stale'])
        self.assertEqual(result['windows']['weekly']['used_percent'], 61)

    def test_stale_snapshot_is_not_ok(self):
        result = self.claude(age=3601)
        self.assertEqual(result['error_code'], 'source_stale')
        self.assertNotIn('usage_percent', result)
        self.assertNotIn('windows', result)  # old parser cannot turn absent week into 0

    def test_expired_window_is_never_zero(self):
        result = self.claude(five_hour=self.window(85, self.now-1))
        self.assertEqual(result['error_code'], 'awaiting_new_window')
        self.assertNotIn('usage_percent', result)

    def test_missing_week_or_reset_is_unavailable(self):
        for values in ({'seven_day': None}, {'five_hour': {'used_percent': 1}}):
            self.assertEqual(self.claude(**values)['error_code'], 'incomplete_windows')

    def test_bad_timestamp_is_unavailable(self):
        for stamp in (None, True, float('nan'), self.now+1000):
            self.assertEqual(self.claude(captured_at=stamp)['error_code'], 'invalid_observation_time')

    def test_bad_numbers_do_not_crash_or_clamp_to_real_usage(self):
        for number in (True, float('nan'), float('inf'), -1, 101, '45'):
            self.assertIsNone(collector.window_state(self.window(number), self.now))

    def record(self, stamp, used):
        return {'timestamp': datetime.fromtimestamp(stamp, timezone.utc).isoformat(),
                'payload': {'rate_limits': {'primary': self.window(used), 'secondary': self.window(70)}}}

    def test_codex_uses_event_time_not_file_mtime(self):
        a, b = self.root/'a.jsonl', self.root/'b.jsonl'
        a.write_text(json.dumps(self.record(self.now-1, 45))+'\n', encoding='utf-8')
        b.write_text(json.dumps(self.record(self.now-4000, 3))+'\n', encoding='utf-8')
        os.utime(b, (self.now+20, self.now+20))
        result = collector.build_codex_provider(self.root)
        self.assertEqual(result['usage_percent'], 45)
        self.assertLess(result['age_seconds'], 10)

    def test_null_record_does_not_refresh_old_timestamp(self):
        path = self.root/'session.jsonl'
        path.write_text(json.dumps(self.record(self.now-4000, 45))+'\n'+
                        json.dumps({'timestamp': self.now, 'rate_limits': {'primary': None}})+'\n', encoding='utf-8')
        result = collector.build_codex_provider(self.root)
        self.assertEqual(result['error_code'], 'source_stale')

    def test_reverse_reader_crosses_block_and_partial_tail(self):
        path = self.root/'session.jsonl'
        path.write_text(json.dumps(self.record(self.now, 45))+'\n'+('x'*300000)+'\n{"rate_limits":', encoding='utf-8')
        self.assertEqual(collector.build_codex_provider(self.root)['usage_percent'], 45)


class HTTPTests(unittest.TestCase):
    def setUp(self):
        collector.Handler.token = 'test-token'
        collector.Handler.cache = None
        collector.Handler.cache_at = 0
        self.server = collector.ThreadingHTTPServer(('127.0.0.1', 0), collector.Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)

    def request(self, route, token='test-token'):
        conn = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=2)
        try:
            conn.request('GET', route, headers={'Authorization': 'Bearer '+token})
            response = conn.getresponse()
            return response.status, json.loads(response.read())
        finally:
            conn.close()

    def test_health_auth_and_identity(self):
        self.assertEqual(self.request('/healthz', '')[0], 401)
        code, body = self.request('/healthz')
        self.assertEqual((code, body['pid']), (200, os.getpid()))
        self.assertEqual(self.request('/missing')[0], 404)

    def test_source_error_does_not_break_health(self):
        with patch.object(collector, 'build_payload', side_effect=OSError('fixture')):
            self.assertEqual(self.request('/v1/dashboard')[0], 503)
            self.assertTrue(supervisor.health_check('127.0.0.1', self.server.server_port, 'test-token', os.getpid()))

    def test_stale_providers_do_not_trigger_restart(self):
        body = {'schema_version': 1, 'providers': [{'id': 'claude', 'status': 'unavailable'}]}
        with patch.object(collector, 'build_payload', return_value=body):
            self.assertTrue(supervisor.health_check('127.0.0.1', self.server.server_port, 'test-token', os.getpid()))
            self.assertFalse(supervisor.health_check('127.0.0.1', self.server.server_port, 'test-token', os.getpid()+1))


class RuntimeTests(unittest.TestCase):
    def test_backoff_is_bounded(self):
        self.assertEqual([runtime_support.restart_delay(n) for n in (1, 2, 3, 20)], [5, 10, 20, 300])

    def test_atomic_write_retries_permission_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'status.json'
            replace = os.replace
            attempts = []
            def flaky(src, dst):
                attempts.append(1)
                if len(attempts) == 1:
                    raise PermissionError('fixture')
                replace(src, dst)
            with patch.object(runtime_support.os, 'replace', side_effect=flaky):
                runtime_support.atomic_json(path, {'ok': True})
            self.assertEqual(json.loads(path.read_text()), {'ok': True})
            self.assertEqual(list(Path(tmp).glob('*.tmp')), [])

    def test_dead_daemon_restarts_even_with_fresh_status_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'status.json'
            path.write_text('{"pid": 1234, "started_at": 1}')
            with patch.object(watchdog, 'STATUS_FILE', path), patch.object(watchdog, 'pid_is_running', return_value=False):
                self.assertTrue(watchdog.stop_stale_daemon())

    def test_live_fresh_daemon_is_not_stopped(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'status.json'
            path.write_text('{"pid": 1234, "started_at": 1}')
            with patch.object(watchdog, 'STATUS_FILE', path), patch.object(watchdog, 'pid_is_running', return_value=True), patch.object(watchdog.subprocess, 'run') as stop:
                self.assertFalse(watchdog.stop_stale_daemon())
                stop.assert_not_called()


if __name__ == '__main__':
    unittest.main()
