#!/usr/bin/env python3
"""Shared local backend for the Cardputer and AI Passport recorders.

Both devices land in the same pipeline: WAV -> GLM ASR -> GLM formatter ->
``lark-cli`` message to the configured Feishu chat.  Passport chunks are
durably acknowledged first; the final request reconstructs them to WAV and
then runs that very same pipeline in the background.
"""

from __future__ import annotations

import array
import json
import os
import shutil
import struct
import subprocess
import threading
import time
import urllib.error
import urllib.request
import uuid
import wave
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse


ROOT = Path(os.environ.get("AI_REC_ROOT", "~/ai-rec-backend")).expanduser().resolve()
INBOX = ROOT / "inbox"
PROCESSED = ROOT / "processed"
PASSPORT = ROOT / "passport"
PORT = int(os.environ.get("AI_REC_PORT", "8787"))
CHAT_ID = os.environ.get("AI_REC_CHAT_ID", "")
AGENT_WEBHOOK = os.environ.get("AI_REC_AGENT_WEBHOOK", "").strip()
AGENT_TOKEN = os.environ.get("AI_REC_AGENT_TOKEN", "").strip()
AGENT_TIMEOUT = int(os.environ.get("AI_REC_AGENT_TIMEOUT", "30"))
AGENT_RETRIES = max(1, int(os.environ.get("AI_REC_AGENT_RETRIES", "3")))
SERVICE_NAME = os.environ.get("AI_REC_SERVICE_NAME", "AI Passport Receiver")
MAX_CHUNK_BYTES = 768 * 1024
MAX_LEGACY_BYTES = 32 * 1024 * 1024


def receiver_identity() -> str:
    configured = os.environ.get("AI_REC_RECEIVER_ID", "").strip()
    identity_file = ROOT / "receiver.id"
    if configured:
        return configured
    try:
        identity = identity_file.read_text(encoding="ascii").strip()
        if identity: return identity
    except FileNotFoundError:
        pass
    identity = uuid.uuid4().hex
    identity_file.write_text(identity + "\n", encoding="ascii")
    return identity


def advertise_service() -> subprocess.Popen | None:
    identity = receiver_identity()
    txt = f"id={identity}"
    dns_sd = shutil.which("dns-sd")
    if dns_sd:
        return subprocess.Popen([dns_sd, "-R", SERVICE_NAME, "_aipassport._tcp", "local", str(PORT), txt, "proto=1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    avahi = shutil.which("avahi-publish-service")
    if avahi:
        return subprocess.Popen([avahi, SERVICE_NAME, "_aipassport._tcp", str(PORT), txt, "proto=1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log("mDNS unavailable; Passport can still use a manually configured endpoint")
    return None


def ensure_dirs() -> None:
    for directory in (INBOX, PROCESSED, PASSPORT):
        directory.mkdir(parents=True, exist_ok=True)


def log(message: str) -> None:
    line = f"[{time.strftime('%m-%d %H:%M:%S')}] {message}"
    print(line, flush=True)
    with (ROOT / "server.log").open("a", encoding="utf-8") as file:
        file.write(line + "\n")


def safe_name(value: str, fallback: str) -> str:
    name = Path(value).name.replace("..", "").strip()
    return name or fallback


def read_body(handler: BaseHTTPRequestHandler, max_bytes: int) -> bytes:
    try:
        length = int(handler.headers.get("Content-Length", "0"))
    except ValueError as error:
        raise ValueError("invalid Content-Length") from error
    if length <= 0 or length > max_bytes:
        raise ValueError("invalid Content-Length")
    payload = handler.rfile.read(length)
    if len(payload) != length:
        raise ValueError("incomplete request body")
    return payload


# ---------- Existing Cardputer processing pipeline ----------
def zhipu(path: str, payload: dict | None = None, files: dict | None = None) -> dict:
    token = os.environ.get("ANTHROPIC_AUTH_TOKEN", "")
    url = "https://open.bigmodel.cn/api/paas/v4/" + path
    if files:
        boundary = uuid.uuid4().hex
        name, content = files["name"], files["data"]
        body = (
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nglm-asr\r\n"
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}\"\r\n"
            "Content-Type: audio/wav\r\n\r\n"
        ).encode() + content + f"\r\n--{boundary}--\r\n".encode()
        request = urllib.request.Request(url, data=body, method="POST")
        request.add_header("Authorization", f"Bearer {token}")
        request.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    else:
        request = urllib.request.Request(url, data=json.dumps(payload).encode(), method="POST")
        request.add_header("Authorization", f"Bearer {token}")
        request.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(request, timeout=300) as response:
            return json.loads(response.read())
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", "replace")[:300]
        raise RuntimeError(f"bigmodel HTTP {error.code}: {body}") from error


def repair_wav(path: Path) -> bool:
    """Repair the conventional 44-byte PCM header if an interrupted device left it stale."""
    data = path.read_bytes()
    if len(data) <= 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return False
    try:
        with wave.open(str(path), "rb") as source:
            if source.getnframes() > 0:
                return False
    except wave.Error:
        pass
    actual = len(data) - 44
    header = bytearray(data[:44])
    declared_data = struct.unpack("<I", data[40:44])[0]
    if declared_data == actual and struct.unpack("<I", data[4:8])[0] == 36 + actual:
        return False
    header[4:8] = struct.pack("<I", 36 + actual)
    header[40:44] = struct.pack("<I", actual)
    path.write_bytes(bytes(header) + data[44:])
    log(f"wav header repaired: {path.name} data_len {declared_data}->{actual}")
    return True


def split_wav(path: Path, max_sec: int = 28) -> list[Path]:
    with wave.open(str(path), "rb") as source:
        rate = source.getframerate()
        pcm = source.readframes(source.getnframes())
    if len(pcm) <= max_sec * rate * 2:
        return [path]
    samples = array.array("h")
    samples.frombytes(pcm)
    if samples.itemsize != 2:
        raise RuntimeError("expected 16-bit PCM")
    window, target, total = max(1, rate // 10), max_sec * rate, len(samples)
    segments: list[array.array] = []
    position = 0
    while position < total:
        if total - position <= target:
            segments.append(samples[position:])
            break
        boundary = position + target
        low, high = max(position + rate, boundary - rate), min(total - window, boundary + rate)
        best, energy = boundary, None
        for cursor in range(low, high + 1, window):
            current = max(abs(value) for value in samples[cursor:cursor + window])
            if energy is None or current < energy:
                best, energy = cursor, current
        segments.append(samples[position:best])
        position = best
    paths: list[Path] = []
    for index, segment in enumerate(segments):
        part = INBOX / f"{path.name}.part{index}.wav"
        with wave.open(str(part), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(rate)
            output.writeframes(segment.tobytes())
        paths.append(part)
    log(f"split {path.name}: {len(paths)} parts ({len(pcm) // (rate * 2)}s)")
    return paths


def transcribe_long(wav_path: Path) -> str:
    repair_wav(wav_path)
    pieces = split_wav(wav_path)
    text: list[str] = []
    try:
        for piece in pieces:
            payload = piece.read_bytes()
            result = zhipu("audio/transcriptions", files={"name": piece.name, "data": payload})
            recognized = (result.get("text") or "").strip()
            log(f"  asr part {piece.name} ({len(payload) // 1024}KB): {recognized[:60]}")
            if recognized:
                text.append(recognized)
    finally:
        for piece in pieces:
            if piece != wav_path:
                piece.unlink(missing_ok=True)
    return " ".join(text)


TEMPLATES = {"meeting": ("📋", "会议纪要"), "idea": ("💡", "灵感卡片"), "memo": ("🎙️", "速记")}
NAMES = os.environ.get("AI_REC_NAMES", "").strip()
NAMES_HINT = f"录音所属团队的人名表：{NAMES}。转写稿里的谐音人名一律修正为正确人名。\n" if NAMES else ""
LLM_SYS = (
    "你是录音整理助手。先判断这条录音的意图类型，再按对应模板整理成飞书 markdown。"
    "只输出一个严格 JSON 对象，不要 markdown 围栏，格式：\n"
    '{"type":"meeting|idea|memo","title":"主题(10字内)","body":"正文markdown"}\n\n'
    "类型判断（录音开头明说的暗号优先，其次按内容性质）：\n"
    '- 说了「会议」「开会」「碰一下」「讨论一下」，或内容是与他人讨论工作事项 → "meeting"\n'
    '- 说了「灵感」「点子」「想法」「记个创意」，或内容是一个创意/念头 → "idea"\n'
    '- 都不像 → "memo"\n\n'
    "各类型 body 的结构要求（用 ## 小节标题）：\n"
    "- meeting: ## 要点（3-6条，保留人名/数字/结论）；## 结论；## 待办（每条格式「事项 — 负责人/期限」，没提就写「无」）\n"
    "- idea: ## 一句话（这个点子是什么）；## 想法本身（展开，保留原话里生动的表达）；## 为什么有价值；## 下一步（最小验证动作，没想好写「待想」）\n"
    "- memo: ## 要点（3-6条）；## 待办（没提就写「无」）\n\n"
    + NAMES_HINT + "口语、重复、语气词精简，但绝不虚构内容。只输出 JSON。"
)


def parse_intent(raw: str) -> tuple[str, str, str]:
    start, end = raw.find("{"), raw.rfind("}")
    if 0 <= start < end:
        try:
            result = json.loads(raw[start:end + 1])
            kind = result.get("type", "memo")
            kind = kind if kind in TEMPLATES else "memo"
            title, body = (result.get("title") or "").strip(), (result.get("body") or "").strip()
            if body:
                return kind, title, body
        except (json.JSONDecodeError, TypeError):
            pass
    return "memo", "", raw.strip()


def process_text(text: str) -> tuple[str, str, str]:
    base = os.environ.get("ANTHROPIC_BASE_URL", "").rstrip("/")
    token = os.environ.get("ANTHROPIC_AUTH_TOKEN", "")
    if not base or not token:
        raise RuntimeError("ANTHROPIC_BASE_URL and ANTHROPIC_AUTH_TOKEN must be set")
    body = {"model": os.environ.get("ANTHROPIC_MODEL", "glm-5.3-flash"), "max_tokens": 2000,
            "system": LLM_SYS, "messages": [{"role": "user", "content": text}]}
    request = urllib.request.Request(base + "/v1/messages", data=json.dumps(body).encode(), method="POST")
    request.add_header("x-api-key", token)
    request.add_header("Authorization", f"Bearer {token}")
    request.add_header("anthropic-version", "2023-06-01")
    request.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request, timeout=180) as response:
        result = json.loads(response.read())
    return parse_intent("".join(item.get("text", "") for item in result.get("content", [])))


def send_lark(markdown: str) -> bool:
    if not CHAT_ID:
        log("lark send skipped: AI_REC_CHAT_ID not set")
        return False
    result = subprocess.run(["lark-cli", "im", "+messages-send", "--chat-id", CHAT_ID, "--markdown", markdown],
                            capture_output=True, text=True, timeout=60, check=False)
    ok = '"ok": true' in result.stdout
    log(f"lark send ok={ok} {result.stdout[:200]}{result.stderr[:200]}")
    return ok


def send_agent_event(event: dict) -> bool:
    """Deliver one finalized recording event to the configured Feishu Agent."""
    if not AGENT_WEBHOOK:
        log("agent send skipped: AI_REC_AGENT_WEBHOOK not set")
        return False
    body = json.dumps(event, ensure_ascii=False).encode("utf-8")
    recording_id = str(event.get("recording_id", "unknown"))
    for attempt in range(1, AGENT_RETRIES + 1):
        request = urllib.request.Request(AGENT_WEBHOOK, data=body, method="POST")
        request.add_header("Content-Type", "application/json; charset=utf-8")
        request.add_header("Accept", "application/json")
        request.add_header("X-Idempotency-Key", recording_id)
        if AGENT_TOKEN:
            request.add_header("Authorization", f"Bearer {AGENT_TOKEN}")
        try:
            with urllib.request.urlopen(request, timeout=AGENT_TIMEOUT) as response:
                response.read(1024)
                status = response.status
            if 200 <= status < 300:
                log(f"agent send ok recording={recording_id} status={status}")
                return True
            log(f"agent send failed recording={recording_id} status={status} attempt={attempt}")
        except urllib.error.HTTPError as error:
            log(f"agent send HTTP {error.code} recording={recording_id} attempt={attempt}")
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            log(f"agent send network error recording={recording_id} attempt={attempt}: {error}")
        if attempt < AGENT_RETRIES:
            time.sleep(min(2 ** (attempt - 1), 8))
    return False


def pipeline(wav_path: Path) -> None:
    tag = wav_path.name
    try:
        log(f"ASR start: {tag} ({wav_path.stat().st_size // 1024}KB)")
        started = time.time()
        transcript = transcribe_long(wav_path)
        log(f"ASR done {time.time() - started:.1f}s: {transcript[:80]}")
        if transcript.strip():
            kind, title, summary = process_text(transcript)
        else:
            kind, title, summary = "memo", "", "**转写为空**（可能是静音或麦克风问题）"
        emoji, label = TEMPLATES[kind]
        header = f"{emoji} **{label} · {title}** · {tag}" if title else f"{emoji} **{label}** · {tag}"
        markdown = (f"{header}\n\n{summary}\n\n<details>原始转写：{transcript[:500]}</details>\n"
                    f"<font color='grey'>时长约 {wav_duration_seconds(wav_path)}s · 音频: {wav_path}</font>")
        event = {
            "schema": 1,
            "event": "recording.ready",
            "recording_id": Path(tag).stem,
            "source": "ai-passport" if tag.startswith("passport-") else "cardputer",
            "audio_path": str(wav_path),
            "duration_seconds": wav_duration_seconds(wav_path),
            "transcript": transcript,
            "type": kind,
            "title": title,
            "body": summary,
            "markdown": markdown,
            "received_at": int(time.time()),
        }
        agent_ok = send_agent_event(event)
        lark_ok = send_lark(markdown)
        if (AGENT_WEBHOOK or CHAT_ID) and not (agent_ok or lark_ok):
            raise RuntimeError("all configured Feishu destinations failed; recording kept locally")
        if not AGENT_WEBHOOK and not CHAT_ID:
            log(f"pipeline kept: no Feishu destination configured for {tag}")
            return
        (PROCESSED / f"{tag}.md").write_text(
            f"# {tag}\n类型: {kind} ({label})\n标题: {title}\n\n{summary}\n\n## 转写原文\n{transcript}\n",
            encoding="utf-8")
        wav_path.rename(wav_path.with_suffix(wav_path.suffix + ".done"))
        log(f"pipeline done: {tag}")
    except Exception as error:
        message = str(error)
        if "no audio segment" in message or '"1210"' in message:
            log(f"pipeline {tag}: no speech detected (1210), file kept")
            try:
                send_lark(f"🎙️ AI 录音卡：{tag} 里没有检测到语音（这段可能是静音），文件已保留")
            except Exception:
                pass
            return
        log(f"pipeline ERROR {tag}: {message}")
        try:
            send_lark(f"⚠️ AI 录音卡处理失败：{tag}\n`{message[:200]}`")
        except Exception:
            pass


# ---------- AI Passport IMA-ADPCM reconstruction ----------
STEP_TABLE = (7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
              45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
              230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
              963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
              3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
              11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
              32767)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def decode_adpcm_packet(packet: bytes) -> bytes:
    """Decode Passport packet: int16 predictor, uint8 index, reserved, uint32 samples, nibbles."""
    if len(packet) < 8:
        raise ValueError("ADPCM packet is shorter than its 8-byte header")
    predictor, index, _reserved, sample_count = struct.unpack_from("<hBBI", packet)
    if not 0 <= index < len(STEP_TABLE):
        raise ValueError("ADPCM packet has an invalid step index")
    # The header already carries the first predictor sample; only the remaining
    # samples consume nibbles.  This mirrors inspiration_adpcm_encoded_size().
    if sample_count == 0 or len(packet) != 8 + sample_count // 2:
        raise ValueError("ADPCM packet is truncated")
    samples = array.array("h", [predictor])
    remaining = sample_count - 1
    for byte in packet[8:]:
        for nibble in (byte & 0x0F, byte >> 4):
            if remaining <= 0:
                return samples.tobytes()
            step = STEP_TABLE[index]
            delta = step >> 3
            if nibble & 1:
                delta += step >> 2
            if nibble & 2:
                delta += step >> 1
            if nibble & 4:
                delta += step
            predictor += -delta if nibble & 8 else delta
            predictor = max(-32768, min(32767, predictor))
            index = max(0, min(88, index + INDEX_TABLE[nibble]))
            samples.append(predictor)
            remaining -= 1
    if remaining:
        raise ValueError("ADPCM packet ended before the stated sample count")
    return samples.tobytes()


def decode_adpcm_stream(payload: bytes) -> bytes:
    """Decode a stored chunk containing concatenated ADPCM packets."""
    pcm = bytearray()
    offset = 0
    while offset < len(payload):
        if len(payload) - offset < 8:
            raise ValueError("ADPCM stream has a truncated packet header")
        sample_count = struct.unpack_from("<I", payload, offset + 4)[0]
        packet_bytes = 8 + sample_count // 2
        if sample_count == 0 or offset + packet_bytes > len(payload):
            raise ValueError("ADPCM stream has a truncated packet")
        pcm.extend(decode_adpcm_packet(payload[offset:offset + packet_bytes]))
        offset += packet_bytes
    return bytes(pcm)


def wav_duration_seconds(path: Path) -> int:
    with wave.open(str(path), "rb") as source:
        return source.getnframes() // max(1, source.getframerate())


def passport_to_wav(session_dir: Path, recording_id: str) -> Path:
    chunks = sorted(session_dir.glob("*.adpcm"))
    if not chunks:
        raise ValueError("session has no audio chunks")
    target = INBOX / f"passport-{recording_id}.wav"
    temp = target.with_suffix(".wav.part")
    with wave.open(str(temp), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(8000)
        for chunk in chunks:
            output.writeframes(decode_adpcm_stream(chunk.read_bytes()))
    temp.replace(target)
    return target


def complete_passport(session_dir: Path, recording_id: str) -> None:
    try:
        wav_path = passport_to_wav(session_dir, recording_id)
        manifest = {"schema": 1, "recording_id": recording_id, "device": "ai-passport",
                    "codec": "ima-adpcm-8khz-mono", "audio_path": str(wav_path),
                    "chunks": [path.name for path in sorted(session_dir.glob("*.adpcm"))],
                    "received_at": int(time.time())}
        (session_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        pipeline(wav_path)
    except Exception as error:
        log(f"passport completion ERROR {recording_id}: {error}")


class Handler(BaseHTTPRequestHandler):
    server_version = "InspirationRecorder/1.0"

    def log_message(self, format: str, *args: object) -> None:
        log(format % args)

    def reply_json(self, status: HTTPStatus, value: dict) -> None:
        payload = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        if urlparse(self.path).path == "/health":
            self.reply_json(HTTPStatus.OK, {"ok": True, "service": "ai-rec-feishu-backend"})
        else:
            self.reply_json(HTTPStatus.NOT_FOUND, {"ok": False})

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
            log(f"request failed: {error}")
            self.reply_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"ok": False, "error": "server failure"})

    def receive_legacy(self, parsed) -> None:
        query = parse_qs(parsed.query)
        name = safe_name(unquote(query.get("name", [""])[0]), f"cardputer-{int(time.time())}.wav")
        if not name.endswith(".wav"):
            name += ".wav"
        target = INBOX / name
        target.write_bytes(read_body(self, MAX_LEGACY_BYTES))
        log(f"legacy upload: {name} from {self.client_address[0]}")
        threading.Thread(target=pipeline, args=(target,), daemon=True).start()
        self.reply_json(HTTPStatus.OK, {"ok": True, "recording_id": target.stem})

    def receive_passport(self, parsed) -> None:
        parts = parsed.path.strip("/").split("/")
        if len(parts) < 5 or parts[:3] != ["v1", "passport", "sessions"]:
            raise ValueError("invalid passport route")
        session = safe_name(parts[3], "")
        if not session:
            raise ValueError("invalid session")
        session_dir = PASSPORT / session
        session_dir.mkdir(parents=True, exist_ok=True)
        if parts[4] == "chunks" and len(parts) == 6:
            sequence = safe_name(parts[5], "")
            if not sequence.isdigit():
                raise ValueError("chunk sequence must be numeric")
            (session_dir / f"{int(sequence):06d}.adpcm").write_bytes(read_body(self, MAX_CHUNK_BYTES))
            self.reply_json(HTTPStatus.OK, {"ok": True, "sequence": int(sequence)})
            return
        if parts[4] == "complete" and len(parts) == 5:
            if not list(session_dir.glob("*.adpcm")):
                raise ValueError("session has no audio chunks")
            threading.Thread(target=complete_passport, args=(session_dir, session), daemon=True).start()
            self.reply_json(HTTPStatus.OK, {"ok": True, "recording_id": session})
            return
        raise ValueError("invalid passport route")


if __name__ == "__main__":
    ensure_dirs()
    mdns_process = advertise_service()
    log(f"shared recorder backend starting on :{PORT}, chat={CHAT_ID or '(not configured)'}")
    try:
        ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
    finally:
        if mdns_process: mdns_process.terminate()
