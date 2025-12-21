# GStreamer Merge Request: RTMP Server Source Element

## Summary

This MR adds a new `rtmp2serversrc` element that implements an RTMP server capable of receiving live streams from encoders like FFmpeg, OBS, etc. This fills a gap in GStreamer's RTMP support - while `rtmp2src` and `rtmp2sink` exist for client-side operations, there was no server-side receive capability.

## Element: rtmp2serversrc

### Description

`rtmp2serversrc` is a source element that listens for incoming RTMP connections on a configurable port. When a client connects and starts publishing, the element outputs the received audio/video data.

### Use Cases

- **Ingest servers**: Receive streams from OBS, FFmpeg, or other RTMP encoders
- **Transcoding pipelines**: Receive RTMP → process → output to other formats
- **Recording**: Receive live streams and save to file
- **Re-streaming**: RTMP input → SRT/HLS/DASH output

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| host | string | "0.0.0.0" | Address to bind to |
| port | uint | 1935 | TCP port to listen on |
| application | string | "live" | RTMP application name to accept |
| stream-key | string | NULL | If set, only accept this stream key |
| timeout | uint | 30 | Client timeout in seconds |
| tls | boolean | FALSE | Enable TLS/RTMPS |
| certificate | string | NULL | PEM certificate file for TLS |
| private-key | string | NULL | PEM private key file for TLS |

### Pad Templates

**Always Pad (src)**:
- Outputs raw FLV data containing the full stream
- Caps: `video/x-flv`

**Sometimes Pads** (created when streams are detected):
- `video_%u`: Raw H.264 video data (`video/x-h264, stream-format=avc, alignment=au`)
- `audio_%u`: Raw AAC audio data (`audio/mpeg, mpegversion=4, stream-format=raw`)

### Example Pipelines

**Save RTMP stream to file:**
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv
```

**RTMP to SRT re-streaming:**
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! flvdemux name=demux \
  demux.video ! queue ! h264parse ! mpegtsmux name=mux ! srtsink uri="srt://:9000?mode=listener" \
  demux.audio ! queue ! aacparse ! mux.
```

**RTMP to HLS:**
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! flvdemux name=demux \
  demux.video ! queue ! h264parse ! mpegtsmux name=mux ! hlssink location=segment%05d.ts \
  demux.audio ! queue ! aacparse ! mux.
```

## Implementation Details

### RTMP Protocol Support

- Full RTMP handshake (C0/C1/C2/S0/S1/S2)
- Chunk stream parsing with all chunk types (0, 1, 2, 3)
- AMF0 command parsing (connect, createStream, publish, etc.)
- Proper handling of interleaved audio/video on different chunk streams
- Window acknowledgement and peer bandwidth messages

### Codecs Supported

- **Video**: H.264/AVC (codec ID 7)
- **Audio**: AAC (codec ID 10)

Additional codecs can be added by extending the FLV tag parsing.

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    rtmp2serversrc                       │
├─────────────────────────────────────────────────────────┤
│  ┌───────────┐    ┌──────────────┐    ┌─────────────┐  │
│  │  Server   │───▶│  Rtmp2Client │───▶│  FLV Parser │  │
│  │  Socket   │    │  (per conn)  │    │             │  │
│  └───────────┘    └──────────────┘    └──────┬──────┘  │
│                                              │         │
│                         ┌────────────────────┼─────┐   │
│                         ▼                    ▼     ▼   │
│                    [src pad]          [video_0] [audio_0]
└─────────────────────────────────────────────────────────┘
```

### Thread Model

- Main thread: GStreamer pipeline, state changes
- Event loop thread: Server socket, client accept
- Read thread (per client): RTMP chunk parsing
- Task thread: Dequeue FLV tags and push to pads

## Testing

Tested with:
- FFmpeg 7.x (`ffmpeg -re -f lavfi -i testsrc ... -f flv rtmp://localhost:1935/live/test`)
- Continuous audio+video streams (3+ minutes)
- Re-streaming to SRT with VLC playback

### Test Results

| Test | Result |
|------|--------|
| Video only (H.264) | ✅ Pass |
| Audio only (AAC) | ✅ Pass |
| Audio + Video interleaved | ✅ Pass |
| Save to FLV file | ✅ Pass |
| Re-stream to SRT | ✅ Pass |
| VLC playback | ✅ Pass |

## Files Added/Modified

### New Files
- `gst/rtmp2/gstrtmp2serversrc.c` - Main element implementation
- `gst/rtmp2/gstrtmp2serversrc.h` - Element header
- `gst/rtmp2/rtmp2client.c` - RTMP client handler
- `gst/rtmp2/rtmp2client.h` - Client header
- `gst/rtmp2/rtmp2chunk_v2.c` - Improved chunk parser
- `gst/rtmp2/rtmp2chunk_v2.h` - Chunk parser header
- `gst/rtmp2/rtmp2flv.c` - FLV tag handling
- `gst/rtmp2/rtmp2flv.h` - FLV header
- `gst/rtmp2/rtmp2handshake.c` - RTMP handshake
- `gst/rtmp2/rtmp2handshake.h` - Handshake header
- `gst/rtmp2/rtmp2enhanced.c` - Enhanced RTMP support
- `gst/rtmp2/rtmp2enhanced.h` - Enhanced RTMP header

### Modified Files
- `gst/rtmp2/meson.build` - Add new sources
- `gst/rtmp2/gstrtmp2.c` - Register new element

## Checklist

- [ ] Code follows GStreamer coding style
- [ ] All functions have gtk-doc documentation
- [ ] Unit tests added
- [ ] Integration tests pass
- [ ] Documentation updated
- [ ] Meson build integration complete

## Related Issues

- Addresses the need for RTMP server/ingest capability in GStreamer
- Complements existing rtmp2src/rtmp2sink elements

## License

LGPL-2.1-or-later (same as gst-plugins-bad)

