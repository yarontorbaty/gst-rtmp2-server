# SRT Live Streaming Status & Next Steps

## What We've Accomplished

✅ **Parser V2 Fixed** - RTMP chunk parsing now correctly handles 60/60 video frames + 88 audio packets  
✅ **FLV Queue Thread-Safe** - Added mutex protection to prevent crashes  
✅ **File Writing Works** - `rtmp2serversrc ! filesink` correctly saves streams  
✅ **FLV Serialization Implemented** - Added proper FLV tag structure (header + codec byte + payload + prev_tag_size)  

## Current Blocker: SRT Pipeline Stalls

### Symptom
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! flvdemux ! h264parse ! mpegtsmux ! srtsink uri="srt://:9000?mode=listener"
ffplay srt://localhost:9000  # Shows aq=0KB vq=0KB (no data flows)
```

### Root Cause
FLV serialization produces malformed output—ffprobe reports "Video codec (0)" instead of recognizing H.264.

### Technical Issue
The serialization reconstructs the codec byte from `tag->video_codec` and `tag->video_keyframe`, but something in the data flow causes the codec byte to be `0x00` instead of `0x17` (H.264 keyframe).

## Three Paths Forward

### Option 1: Debug FLV Serialization (Incremental)
**Pros**: Keeps current architecture, minimal changes  
**Cons**: FLV is unnecessary complexity for live streaming  

**Steps**:
1. Add debug logging to `gstrtmp2serversrc.c` serialization to print actual tag values
2. Verify `tag->video_codec == 7` and `tag->video_keyframe == TRUE`
3. If values are correct, check byte order/endianness
4. If values are wrong, trace back through rtmp2client → rtmp2flv parser

**Estimated effort**: 1-2 hours debugging

---

### Option 2: Bypass FLV Layer (Cleaner Architecture)
**Pros**: Matches how native GStreamer sources work (rtspsrc, udpsrc), no intermediate format  
**Cons**: Requires more code changes  

**Architecture**:
```
Current:  RTMP messages → FLV tags → serialize FLV → flvdemux → codecs
Proposed: RTMP messages → extract payloads → emit on typed pads
```

**Implementation**:
1. Modify `gstrtmp2serversrc` to expose `video_%u` and `audio_%u` sometimes pads
2. Parse RTMP message types 8/9, extract codec data directly
3. Emit buffers with proper caps: `video/x-h264,stream-format=avc` and `audio/mpeg,mpegversion=4`
4. Pipeline becomes: `rtmp2serversrc.video_0 ! h264parse ! ...`

**Estimated effort**: 3-4 hours implementation + testing

**Reference**: See `SRT_LIVE_STREAMING_TASK.md` for detailed spec

---

### Option 3: Quick Workaround - Store Codec Byte
**Pros**: Minimal change to current code  
**Cons**: Band-aid solution, doesn't address architectural issue  

**Implementation**:
1. Add `guint8 codec_byte` field to `Rtmp2FlvTag` struct
2. Store the original codec byte in `rtmp2flv.c` line 129
3. Use stored byte directly in serialization instead of reconstructing

**Estimated effort**: 30 minutes

---

## Recommendation

**Go with Option 2** - Bypass FLV entirely. Here's why:

1. **Native GStreamer pattern** - All live sources (RTSP, UDP, WebRTC) emit typed pads, not intermediate container formats
2. **Lower latency** - No FLV serialization/deserialization overhead
3. **Simpler debugging** - Direct RTMP → codec mapping, no mysterious FLV corruption
4. **Future-proof** - Easier to add enhanced RTMP features (H.265, VP9, AV1) without FLV limitations
5. **Already specified** - `SRT_LIVE_STREAMING_TASK.md` has full implementation plan

The FLV approach was meant for file writing (which works!). Live streaming needs a different architecture.

## Commands to Resume Testing

Once Option 2 is implemented:

```bash
# Terminal 1 - Start SRT server
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

gst-launch-1.0 -v \
  rtmp2serversrc port=1935 name=src \
    src.video_0 ! queue ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au ! queue ! mux. \
    src.audio_0 ! queue ! aacparse ! audio/mpeg,stream-format=adts ! queue ! mux. \
  mpegtsmux name=mux ! srtsink uri="srt://:9000?mode=listener"

# Terminal 2 - Send RTMP stream
ffmpeg -re -f lavfi -i testsrc=duration=30:rate=30 \
  -f lavfi -i sine=duration=30 \
  -c:v libx264 -preset ultrafast -g 30 \
  -c:a aac \
  -f flv rtmp://localhost:1935/live/test

# Terminal 3 - Verify playback
ffplay srt://localhost:9000
```

**Success criteria**: `ffplay` shows `aq=XXkB vq=XXkB` (data flowing) and video plays with < 200ms latency.

##Files Modified So Far

- `gst/rtmp2chunk_v2.c` - Parser fixes + diagnostics ✅ Merged to main
- `gst/rtmp2flv.c/.h` - Added mutex ✅ Merged to main  
- `gst/rtmp2client.c` - Thread-safe tag queueing ✅ Merged to main
- `gst/gstrtmp2serversrc.c` - FLV serialization (needs Option 1 fix OR Option 2 replacement)

## Next Session Prompt

"Implement Option 2 from SRT_LIVE_STREAMING_STATUS.md: modify rtmp2serversrc to emit video/audio on separate sometimes pads instead of FLV stream. Use SRT_LIVE_STREAMING_TASK.md as the spec."

