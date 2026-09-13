# AI Passport Inspiration Recorder

[简体中文](README.zh_CN.md)

An ESP32-C3 firmware and local receiver that turn the FoloToy AI Passport into
an offline-first voice recorder for ideas. The device records with physical
buttons, retains clips until the receiver acknowledges them, and hands the
completed recording to an owner-configured AI or collaboration workflow.

## What it does

- **One-button capture:** `OK` starts, pauses, and resumes recording; `DOWN`
  stops and seals the current clip.
- **Offline-first storage:** 8 kHz mono IMA-ADPCM keeps approximately twelve
  minutes of clips in the verified free voice filesystem region.
- **Safe rolling transfer:** clips are sealed every 60 seconds. Wi-Fi opens
  only for pending uploads, and a clip is deleted only after an HTTP success
  response.
- **On-device library:** `UP` opens unuploaded clips; `OK` plays/stops,
  `UP/DOWN` selects clips or changes playback volume, and holding `OK` opens a
  vertically navigated delete confirmation.
- **Usable playback:** the player presents actual elapsed/total time, a real
  progress bar, and a separate volume percentage.
- **Living idle screen:** date/time, battery, a small recording waveform, and
  a 12-frame meditation illustration that follows the time of day.
- **Battery-aware behavior:** recording keeps the screen visible at low
  brightness; idle mode later dims, turns off the backlight, and finally uses
  light sleep.

## Design

```text
physical keys
    -> recorder state machine -> ADPCM clip files -> durable upload queue
                                                   -> short Wi-Fi window
                                                   -> owner-configured receiver
                                                          -> ASR / AI / chat workflow
```

The firmware deliberately contains no personal Wi-Fi credentials, private
receiver address, chat identifier, or AI token. Wi-Fi credentials and the
receiver URL are entered locally through the device provisioning page and are
stored only on that device. The included receiver reads its own credentials
from environment variables.

## Controls

| Context | `OK` | `UP` | `DOWN` |
| --- | --- | --- | --- |
| Home | record / pause / resume | open offline library | stop and send |
| Library | play / stop | previous clip | next clip |
| Playing | stop | volume + | volume - |
| Delete confirmation | confirm | move selection | move selection |

Hold `OK` in the library to open the delete confirmation. Hold `UP` to return
to the home screen. Hold `UP` on the home screen to open the local Wi-Fi and
receiver setup page.

## Receiver

`backend/server.py` accepts the Passport's ordered ADPCM chunks, reconstructs
them as WAV after session completion, and invokes a configurable processing
pipeline. It contains no credentials; provide service endpoints, keys, and
chat routing through environment variables in your own deployment. See
[backend/README.md](backend/README.md).

## Safety notes

- Flash only the application partition at `0x10000`; do not erase the whole
  device.
- The project avoids the factory identity and resource regions.
- Do not commit `.env` files, captured audio, receiver data, device logs, or
  provisioning screenshots.

## Build

Use ESP-IDF 5.5 or a compatible environment:

```bash
idf.py build
```

The repository is intentionally public-safe by default: a newly flashed card
can record offline immediately, but will not upload until configured by its
owner.
