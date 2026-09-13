# AI Passport Inspiration Recorder: Product Blueprint

[简体中文](INSPIRATION_RECORDER_PRD.zh_CN.md)

## Definition of done

Physical controls capture an idea without a phone. The device stores audio
offline, transfers one-minute chunks only when a network is needed, and deletes
a chunk only after the receiver acknowledges it. The completed recording is
then handed to the owner's chosen AI or collaboration workflow. Interrupted
uploads resume after restart.

## Interaction contract

- Click `OK` to start, pause, or resume recording.
- Click `DOWN` to stop and seal the current clip.
- Click `UP` to open the unuploaded-clip library; hold `UP` to return home.
- Recording never relies on a touch-screen button.
- The top row shows date/time and a genuinely right-aligned battery level;
  Wi-Fi belongs in the lower status row.
- The centre card is reserved for a lightweight future app such as a mood card
  or a virtual pet.
- The lower row shows recording state, a compact live waveform, Wi-Fi state,
  and available cache time.

## Data, energy, and safety contract

- Audio uses 8 kHz, 16-bit mono IMA-ADPCM.
- Chunks are 60 seconds and are removed only after receiver acknowledgement.
- The offline target is about 12 minutes.
- Wi-Fi starts only while work is pending and stops when the queue is empty.
- The firmware never erases or writes `cardid@0x356000`, and does not overwrite
  factory resource regions.
- During recording or pause, the display stays visible at low brightness;
  regular idle handling later dims, turns off the backlight, then light-sleeps.

## Delivery status

| Area | Status |
| --- | --- |
| ADPCM, recorder state machine, 60-second chunks, voice filesystem | Complete |
| Restart recovery, acknowledgement-before-delete, background transfer | Complete and device-tested |
| Wi-Fi upload window and local provisioning | Complete |
| Receiver protocol and configurable AI/collaboration workflow | Complete |
| Recorder UI, clock, library, playback progress, and delete confirmation | Complete and device-tested |
| ESP-IDF build and application-partition flashing | Complete; preserves identity and recordings |

## Device acceptance checks

1. `OK` begins recording; the red indicator and compact waveform move.
2. `OK` pauses/resumes; `DOWN` stops and briefly shows the stop icon.
3. Offline recordings survive restart.
4. When configured and connected, the previous minute uploads while recording
   continues uninterrupted.
5. A receiver acknowledgement deletes the matching local chunk; failures do
   not delete it.
6. The configured workflow receives a structured idea result.
7. Restart and application updates preserve device identity, factory resources,
   and unacknowledged audio.
