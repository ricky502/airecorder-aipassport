<p align="right">
  <a href="README.zh_CN.md">Simplified Chinese</a> · <strong>English</strong>
</p>

# Inspiration Inbox

This dependency-free local service receives recordings from both supported
devices before any AI processing happens. It preserves audio when a later
transcription or Feishu step is unavailable.

## Start

```bash
python3 server.py
curl http://127.0.0.1:8787/health
```

Set `AI_REC_ROOT` to choose an audio directory and `AI_REC_PORT` to choose a
port. On startup the service advertises `_aipassport._tcp` with mDNS, so a
Passport on the same LAN can discover it without a fixed computer IP. The
receiver identity is stored in `AI_REC_ROOT/receiver.id` and is ignored by
Git. `AI_REC_SERVICE_NAME` changes the advertised name and
`AI_REC_RECEIVER_ID` can explicitly set the identity. `AI_REC_AGENT_WEBHOOK` is optional. When set, the service POSTs one JSON
recording event to that URL after it has durably stored the audio.

## Compatible routes

The existing Cardputer firmware keeps using its current route:

```text
POST /upload?name=<name>.wav
```

The Passport recorder uploads ordered IMA-ADPCM chunks, then finalizes a
session:

```text
POST /v1/passport/sessions/<session>/chunks/<sequence>
POST /v1/passport/sessions/<session>/complete
```

The session manifest is the stable hand-off to a single shared AI/Feishu
pipeline. It identifies the source device, codec, local audio location, and
chunk ordering. The agent adapter is deliberately separate from transport so
credentials never enter firmware or source control.
