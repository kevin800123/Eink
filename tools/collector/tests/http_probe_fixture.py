"""Loopback-only fault fixture; never use the real provider files or token."""
import argparse
import json
import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Handler(BaseHTTPRequestHandler):
    hang_this_process = False
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == '/crash':
            os._exit(7)
        if self.path == '/v1/dashboard':
            marker = os.environ.get('COLLECTOR_TEST_HANG_MARKER')
            if marker and self.hang_this_process:
                Path(marker).touch()
                time.sleep(120)
            body = {'schema_version': 1, 'providers': [{'id': 'claude', 'status': 'unavailable'}]}
        else:
            body = {'service': 'ai-usage-collector', 'pid': os.getpid(), 'status': 'ok'}
        raw = json.dumps(body).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1', choices=['127.0.0.1'])
    parser.add_argument('--port', type=int, required=True)
    args = parser.parse_args()
    marker = os.environ.get('COLLECTOR_TEST_HANG_MARKER')
    Handler.hang_this_process = bool(marker and not Path(marker).exists())
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()
