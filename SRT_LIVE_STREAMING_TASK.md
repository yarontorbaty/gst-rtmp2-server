# Task: Enable Live SRT Streaming from RTMP Server

## Problem
The current `rtmp2serversrc → flvdemux → mpegtsmux → srtsink` pipeline doesn't work for **live streaming** because:

1. **FLV is container-oriented**: `flvdemux` buffers data and waits for EOS (end-of-stream) before emitting pads
2. **No live mode**: The current `gst/gstrtmp2serversrc.c` writes raw FLV bytes meant for files, not live playback
3. **Testing confirms**: `ffplay srt://localhost:9000` shows `aq=0KB vq=0KB`—no data flows until stream ends

## Solution: Direct Message-to-Buffer Pipeline

Instead of writing FLV, emit **parsed RTMP messages directly as GStreamer buffers** on separate video/audio pads.

### Architecture Changes Needed

#### 1. Update `gst/gstrtmp2serversrc.c`
- **Current**: Single src pad pushing raw FLV bytes via `flv_parser`
- **Target**: 
  - `video_%u` pad: emit H.264 NAL units with PTS/DTS from RTMP message type 9
  - `audio_%u` pad: emit AAC frames with PTS from RTMP message type 8
  - Remove FLV serialization; use RTMP message payload directly

#### 2. Modify `gst/rtmp2client.c` Message Handling
- **Current**: `on_client_chunk_received()` → creates FLV tags → queues for mux
- **Target**:
  - Parse RTMP message type (8=audio, 9=video, 18=metadata)
  - Extract codec headers (AVC sequence header, AAC config) for caps negotiation
  - Push raw codec data as `GstBuffer` with proper timestamps on respective pads
  - Signal pad-added for dynamic linking

#### 3. Caps Negotiation
- **Video pad caps**:
  ```
  video/x-h264, stream-format=avc, alignment=au, 
  codec_data=(buffer from AVC sequence header message)
  ```
- **Audio pad caps**:
  ```
  audio/mpeg, mpegversion=4, stream-format=raw,
  codec_data=(buffer from AAC sequence header message)
  ```

### Recommended GStreamer Pipeline (after changes)

```bash
gst-launch-1.0 \
  rtmp2serversrc port=1935 name=src \
    src.video_0 ! queue ! h264parse ! video/x-h264,stream-format=byte-stream ! queue ! mux. \
    src.audio_0 ! queue ! aacparse ! audio/mpeg,stream-format=adts ! queue ! mux. \
  mpegtsmux name=mux ! srtsink uri="srt://:9000?mode=listener"
```

Key difference: `src.video_0` / `src.audio_0` are **sometimes pads** (like `flvdemux` emits), not a single `src.` pushing FLV.

### Testing Commands

#### Start SRT Server
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

gst-launch-1.0 -v \
  rtmp2serversrc port=1935 name=src \
    src.video_0 ! queue ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au ! queue ! mux. \
    src.audio_0 ! queue ! aacparse ! audio/mpeg,stream-format=adts ! queue ! mux. \
  mpegtsmux name=mux ! srtsink uri="srt://:9000?mode=listener"
```

#### Push RTMP Stream
```bash
ffmpeg -re -f lavfi -i testsrc=duration=30:rate=30 \
  -f lavfi -i sine=duration=30 \
  -c:v libx264 -preset ultrafast -g 30 \
  -c:a aac \
  -f flv rtmp://localhost:1935/live/test
```

#### Verify SRT Playback (should show immediate playback)
```bash
ffplay srt://localhost:9000
```

### Success Criteria
1. `ffplay` connects and shows video/audio immediately (no buffering wait)
2. Audio/video queues populate: `aq=XXkB vq=XXkB` in ffplay stats
3. Latency < 200ms from RTMP push to SRT playback
4. No EOS required—stream stays live until ffmpeg/server killed

### Files to Modify
1. `gst/gstrtmp2serversrc.c` - change from single-pad FLV writer to multi-pad message emitter
2. `gst/gstrtmp2serversrc.h` - add video/audio pad templates
3. `gst/rtmp2client.c` - route parsed messages to pads instead of FLV queue
4. `gst/rtmp2flv.c/.h` - keep for backward compat (file writing), but bypass for live

### Additional Notes
- Keep the existing FLV file-writing mode as a property: `rtmp2serversrc mode=file` vs `mode=live` (default)
- Reference how `flvdemux` handles dynamic pad creation for structure
- Ensure proper ref-counting on `GstBuffer` to avoid leaks
- Handle client disconnects gracefully (send EOS on pads when client drops)

## Current State
- Parser fix merged to `main` (commit `9c7dd27`)
- All 60 video frames + 88 audio packets captured correctly in file mode
- SRT pipeline tested but stalls due to FLV buffering issue

## Next Steps
1. Implement sometimes pads in `gstrtmp2serversrc.c`
2. Parse RTMP video/audio messages and emit as buffers
3. Test with live SRT playback
4. Document usage in README

