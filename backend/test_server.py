#!/usr/bin/env python3
"""Small no-network checks for the Passport-to-Feishu receiver."""

import array
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location("inspiration_server", Path(__file__).with_name("server.py"))
SERVER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(SERVER)


class PassportAdpcmTests(unittest.TestCase):
    def test_decodes_firmware_packet_layout(self):
        # Predictor=0 and two IMA nibbles: 7 then 8.  Firmware writes low nibble first.
        packet = struct.pack("<hBBI", 0, 0, 0, 3) + bytes([0x87])
        pcm = array.array("h")
        pcm.frombytes(SERVER.decode_adpcm_packet(packet))
        self.assertEqual(list(pcm), [0, 11, 9])

    def test_rejects_wrong_packet_size(self):
        packet = struct.pack("<hBBI", 0, 0, 0, 3)
        with self.assertRaises(ValueError):
            SERVER.decode_adpcm_packet(packet)

    def test_creates_8khz_mono_wav(self):
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory) / "session"
            inbox = Path(directory) / "inbox"
            session.mkdir()
            inbox.mkdir()
            (session / "000001.adpcm").write_bytes(struct.pack("<hBBI", 0, 0, 0, 3) + bytes([0x87]))
            original_inbox = SERVER.INBOX
            SERVER.INBOX = inbox
            try:
                wav_path = SERVER.passport_to_wav(session, "test")
            finally:
                SERVER.INBOX = original_inbox
            with SERVER.wave.open(str(wav_path), "rb") as wav:
                self.assertEqual((wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes()),
                                 (8000, 1, 2, 3))

    def test_complete_reconstructs_then_enters_shared_pipeline_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            session = root / "session"
            inbox = root / "inbox"
            session.mkdir()
            inbox.mkdir()
            packet = struct.pack("<hBBI", 0, 0, 0, 3) + bytes([0x87])
            (session / "000002.adpcm").write_bytes(packet)
            (session / "000001.adpcm").write_bytes(packet)
            original_inbox, original_pipeline = SERVER.INBOX, SERVER.pipeline
            processed = []
            SERVER.INBOX = inbox
            SERVER.pipeline = lambda wav: processed.append(wav)
            try:
                SERVER.complete_passport(session, "memo-42")
            finally:
                SERVER.INBOX, SERVER.pipeline = original_inbox, original_pipeline
            self.assertEqual([path.name for path in processed], ["passport-memo-42.wav"])
            manifest = __import__("json").loads((session / "manifest.json").read_text())
            self.assertEqual(manifest["chunks"], ["000001.adpcm", "000002.adpcm"])
            with SERVER.wave.open(str(processed[0]), "rb") as wav:
                self.assertEqual(wav.getnframes(), 6)


if __name__ == "__main__":
    unittest.main()
