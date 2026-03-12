#!/usr/bin/env python3
import argparse
import base64
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse


class State:
    def __init__(self) -> None:
        self.power_state = "On"
        self.sel_entries = [
            {
                "@odata.id": "/redfish/v1/Managers/1/LogServices/SEL/Entries/1",
                "Id": "1",
                "Created": "2026-03-12T00:00:00Z",
                "Message": "Mock SEL entry",
                "Severity": "OK",
            }
        ]


def _read_json(handler: BaseHTTPRequestHandler):
    length = int(handler.headers.get("Content-Length") or "0")
    if length <= 0:
        return None
    raw = handler.rfile.read(length)
    try:
        return json.loads(raw.decode("utf-8"))
    except Exception:
        return None


def _write_json(handler: BaseHTTPRequestHandler, status: int, payload: object):
    body = json.dumps(payload, indent=2).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def _write_empty(handler: BaseHTTPRequestHandler, status: int):
    handler.send_response(status)
    handler.send_header("Content-Length", "0")
    handler.end_headers()


def _require_basic_auth(handler: BaseHTTPRequestHandler, user: str, password: str) -> bool:
    auth = handler.headers.get("Authorization") or ""
    if not auth.startswith("Basic "):
        handler.send_response(401)
        handler.send_header("WWW-Authenticate", 'Basic realm="bmcli-mock"')
        handler.send_header("Content-Length", "0")
        handler.end_headers()
        return False
    b64 = auth[len("Basic ") :].strip()
    try:
        decoded = base64.b64decode(b64).decode("utf-8")
    except Exception:
        _write_empty(handler, 401)
        return False
    if ":" not in decoded:
        _write_empty(handler, 401)
        return False
    u, p = decoded.split(":", 1)
    if u != user or p != password:
        _write_empty(handler, 401)
        return False
    return True


class Handler(BaseHTTPRequestHandler):
    server_version = "bmcli-redfish-mock/0.1"

    def do_GET(self):
        if not _require_basic_auth(self, self.server.expected_user, self.server.expected_pass):
            return
        path = urlparse(self.path).path
        st = self.server.state

        if path == "/redfish/v1":
            _write_json(
                self,
                200,
                {
                    "RedfishVersion": "1.14.0",
                    "Systems": {"@odata.id": "/redfish/v1/Systems"},
                    "Managers": {"@odata.id": "/redfish/v1/Managers"},
                    "Chassis": {"@odata.id": "/redfish/v1/Chassis"},
                },
            )
            return

        if path == "/redfish/v1/Systems/1":
            _write_json(
                self,
                200,
                {
                    "Id": "1",
                    "Name": "Mock System",
                    "PowerState": st.power_state,
                    "Status": {"State": "Enabled", "Health": "OK"},
                },
            )
            return

        if path == "/redfish/v1/Managers/1/LogServices/SEL/Entries":
            _write_json(
                self,
                200,
                {
                    "Members@odata.count": len(st.sel_entries),
                    "Members": st.sel_entries,
                },
            )
            return

        if path == "/redfish/v1/Chassis/1/Thermal":
            _write_json(
                self,
                200,
                {
                    "Temperatures": [
                        {"Name": "CPU0", "ReadingCelsius": 42, "Status": {"Health": "OK"}}
                    ],
                    "Fans": [
                        {"Name": "FAN0", "Reading": 8000, "Status": {"Health": "OK"}}
                    ],
                },
            )
            return

        _write_json(self, 404, {"error": {"code": "NotFound", "message": "Unknown path", "path": path}})

    def do_POST(self):
        if not _require_basic_auth(self, self.server.expected_user, self.server.expected_pass):
            return
        path = urlparse(self.path).path
        st = self.server.state
        body = _read_json(self) or {}

        if path == "/redfish/v1/Systems/1/Actions/ComputerSystem.Reset":
            reset_type = body.get("ResetType", "")
            if reset_type in ("On",):
                st.power_state = "On"
            elif reset_type in ("ForceOff", "GracefulShutdown"):
                st.power_state = "Off"
            elif reset_type in ("PowerCycle", "ForceRestart"):
                st.power_state = "On"
            else:
                _write_json(self, 400, {"error": {"code": "BadRequest", "message": "Invalid ResetType"}})
                return
            _write_empty(self, 204)
            return

        if path == "/redfish/v1/Managers/1/LogServices/SEL/Actions/LogService.ClearLog":
            st.sel_entries = []
            _write_empty(self, 204)
            return

        _write_json(self, 404, {"error": {"code": "NotFound", "message": "Unknown path", "path": path}})

    def log_message(self, fmt: str, *args) -> None:
        # Keep test output clean; write server logs to stderr.
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password", default="admin")
    args = ap.parse_args()

    httpd = ThreadingHTTPServer((args.listen, args.port), Handler)
    httpd.state = State()
    httpd.expected_user = args.user
    httpd.expected_pass = args.password

    sys.stderr.write(f"[mock] listening on http://{args.listen}:{args.port}\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

