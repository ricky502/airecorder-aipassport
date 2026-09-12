#!/usr/bin/env python3
"""Local inbox shared by Cardputer and FoloToy AI Passport.

No API keys live here.  The service durably receives audio first, then sends a
small JSON event to an optional agent webhook.  This keeps device transport
separate from transcription, Feishu, and Obsidian policy.
"""

from __future__ import annotations

import json
import os
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse
from urllib.request import Request, urlopen


ROOT = Path(os.environ.get("AI_REC_ROOT", "./data")).expanduser().resolve()
PORT = int(os.environ.get("AI_REC_PORT", "8787"))
AGENT_WEBHOOK = os.environ.get("AI_REC_AGENT_WEBHOOK", "")
MAX_CHUNK_BYTES = 768 * 1024
MAX_LEGACY_BYTES = 32 * 1024 * 1024


def safe_name(value: str, fallback: str) -> str:
    name = Path(value).name.replace("..", "").strip()
    return name or fallback


def ensure_dirs() -> None:
    for directory in (ROOT / "legacy", ROOT / "passport", ROOT / "events"):
        directory.mkdir(parents=True, exist_ok=True)


def read_body(handler: BaseHTTPRequestHandler, max_bytes: int) -> bytes:
    length = int(handler.headers.get("Content-Length", "0"))
    if length <= 0 or length > max_bytes:
        raise ValueError("invalid Content-Length")
    remaining = length
    parts: list[bytes] = []
    while remaining:
        chunk = handler.rfile.read(min(65536, remaining))
        if not chunk:
            raise ValueError("incomplete request body")
        parts.append(chunk)
        remaining -= len(chunk)
    return b"".join(parts)


def agent_event(event: dict) -> None:
    """Persist every event and optionally forward it to one shared Agent."""
    event_id = f"{event['recording_id']}-{int(time.time() * 1000)}"
    (ROOT / "events" / f"{event_id}.json").write_text(
        json.dumps(event, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    if not AGENT_WEBHOOK:
        return
    request = Request(
        AGENT_WEBHOOK,
        data=json.dumps(event, ensure_ascii=False).encode("utf-8"),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urlopen(request, timeout=20) as response:
        if response.status // 100 != 2:
            raise RuntimeError(f"agent webhook returned {response.status}")


def async_agent_event(event: dict) -> None:
    def run() -> None:
        try:
            agent_event(event)
        except Exception as error:  # preserve received audio even if the agent is down
            print(f"agent event failed: {error}", flush=True)

    threading.Thread(target=run, daemon=True).start()


class Handler(BaseHTTPRequestHandler):
    server_version = "InspirationInbox/0.1"

    def log_message(self, format: str, *args: object) -> None:
        print(f"[{time.strftime('%H:%M:%S')}] {format % args}", flush=True)

    def reply_json(self, status: HTTPStatus, value: dict) -> None:
        payload = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        if urlparse(self.path).path != "/health":
            self.reply_json(HTTPStatus.NOT_FOUND, {"ok": False})
            return
        self.reply_json(HTTPStatus.OK, {"ok": True, "service": "inspiration-inbox"})

    def do_POST(self) -> None:
        try:
            parsed = urlparse(self.path)
            if parsed.path == "/upload":
                self.receive_legacy(parsed)
            elif parsed.path.startswith("/v1/passport/sessions/"):
                self.receive_passport(parsed)
            else:
                self.reply_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "unknown route"})
        except ValueError as error:
            self.reply_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(error)})
        except Exception as error:
            print(f"request failed: {error}", flush=True)
            self.reply_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"ok": False, "error": "server failure"})

    def receive_legacy(self, parsed) -> None:
        query = parse_qs(parsed.query)
        name = safe_name(unquote(query.get("name", [""])[0]), f"cardputer-{int(time.time())}.wav")
        if not name.endswith(".wav"):
            name += ".wav"
        payload = read_body(self, MAX_LEGACY_BYTES)
        target = ROOT / "legacy" / name
        target.write_bytes(payload)
        event = {
            "schema": 1,
            "recording_id": target.stem,
            "device": "cardputer",
            "codec": "wav",
            "audio_path": str(target),
            "received_at": int(time.time()),
        }
        async_agent_event(event)
        self.reply_json(HTTPStatus.OK, {"ok": True, "recording_id": target.stem})

    def receive_passport(self, parsed) -> None:
        parts = parsed.path.strip("/").split("/")
        # v1/passport/sessions/{session}/chunks/{sequence}
        if len(parts) < 5 or parts[:3] != ["v1", "passport", "sessions"]:
            raise ValueError("invalid passport route")
        session = safe_name(parts[3], "")
        if not session:
            raise ValueError("invalid session")
        session_dir = ROOT / "passport" / session
        session_dir.mkdir(parents=True, exist_ok=True)
        if parts[4] == "chunks" and len(parts) == 6:
            sequence = safe_name(parts[5], "")
            if not sequence.isdigit():
                raise ValueError("chunk sequence must be numeric")
            payload = read_body(self, MAX_CHUNK_BYTES)
            target = session_dir / f"{int(sequence):06d}.adpcm"
            target.write_bytes(payload)
            self.reply_json(HTTPStatus.OK, {"ok": True, "sequence": int(sequence)})
            return
        if parts[4] == "complete" and len(parts) == 5:
            chunks = sorted(path.name for path in session_dir.glob("*.adpcm"))
            if not chunks:
                raise ValueError("session has no audio chunks")
            manifest = {
                "schema": 1,
                "recording_id": session,
                "device": "ai-passport",
                "codec": "ima-adpcm-16khz-mono",
                "audio_directory": str(session_dir),
                "chunks": chunks,
                "received_at": int(time.time()),
            }
            (session_dir / "manifest.json").write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            async_agent_event(manifest)
            self.reply_json(HTTPStatus.OK, {"ok": True, "recording_id": session})
            return
        raise ValueError("invalid passport route")


if __name__ == "__main__":
    ensure_dirs()
    print(f"inspiration inbox listening on :{PORT}, root={ROOT}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
