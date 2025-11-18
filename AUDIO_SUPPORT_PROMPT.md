# Chat Prompt: Add Audio Support to V2 Parser

## Context

**Repository**: `/Users/yarontorbaty/gst-rtmp2-server`  
**Current Status**: V1.0.0 - Production ready with 100% video frame capture  
**Issue**: #4 - Audio support causes pipeline errors  
**Goal**: Enable audio+video streaming through the V2 parser

---

## What's Working ✅

### V2 Parser (Video Only)
- **Frame Capture**: 100% (60/60 frames at 30fps)
- **Parser**: SRS-based with FastBuffer and ensure() pattern
- **Output Formats**: FLV file, UDP, SRT streaming
- **Files**: `gst/rtmp2chunk_v2.c`, `gst/rtmp2chunk_v2.h`

### Test Command (Video Only - Working)
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

# Terminal 1
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=test.flv

# Terminal 2
ffmpeg -re -f lavfi -i testsrc=size=640x480:rate=30 -t 2 \
  -c:v libx264 -preset ultrafast -g 30 \
  -f flv rtmp://localhost:1935/live/test

# Result: 60/60 frames, valid 50KB FLV file
```

---

## What's Broken ❌

### Audio+Video Streaming

When FFmpeg sends both audio and video:
```bash
ffmpeg -re -f lavfi -i testsrc -f lavfi -i sine \
  -c:v libx264 -c:a aac \
  -f flv rtmp://localhost:1935/live/test
```

With pipeline:
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! flvdemux name=d \
  d.video ! queue ! h264parse ! mux. \
  d.audio ! queue ! aacparse ! mux. \
  mpegtsmux name=mux ! filesink location=test.ts
```

### Observed Errors

1. **Parser logs:**
   ```
   Message type 0 (length=X, csid=X) has no buffer, skipping
   Suspicious message length: 12575458 bytes (type=X, csid=X), skipping this stream
   ```

2. **Pipeline error:**
   ```
   ERROR: Internal data stream error
   Got EOS before any data
   ```

3. **Symptoms:**
   - Parser gets into infinite loop
   - Only ~12 frames captured instead of 60-90
   - FFmpeg sometimes can't connect
   - Pipeline stalls or exits early

---

## Root Cause Hypothesis

The V2 parser (`gst/rtmp2chunk_v2.c`) may have issues with:

1. **Audio message parsing** - Different message type (8 = AUDIO) vs (9 = VIDEO)
2. **Message interleaving** - Audio and video chunks interleaved on different chunk streams
3. **Chunk stream ID handling** - Audio uses different CSID than video
4. **Type 3 chunks** - Continuation chunks for audio messages
5. **Zero-length messages** - Some control messages have length=0

### Relevant Code

**V2 Parser**: `gst/rtmp2chunk_v2.c` lines 380-510  
**Message Processing**: `gst/rtmp2client.c` lines 1108-1195 (VIDEO/AUDIO handling)

---

## What Needs To Be Done

### Phase 1: Debug Audio Parsing

1. **Add audio debug logging** to V2 parser:
   - Log when audio messages (type=8) arrive
   - Log chunk stream IDs for audio vs video
   - Log message lengths and timestamps for audio

2. **Test with verbose debugging:**
   ```bash
   export GST_DEBUG=rtmp2chunk_v2:5,rtmp2client:5
   ```

3. **Reproduce the error** with minimal test:
   ```bash
   gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=audio_test.flv
   ffmpeg -re -f lavfi -i testsrc=duration=2 -f lavfi -i sine=duration=2 \
     -c:v libx264 -c:a aac -f flv rtmp://localhost:1935/live/test
   ```

### Phase 2: Fix Parser Issues

Likely fixes needed in `gst/rtmp2chunk_v2.c`:

1. **Handle audio messages (type=8)** same as video (type=9)
2. **Support multiple chunk streams** - video and audio use different CSIDs
3. **Fix continue loops** - Lines 423, 431, 438, 449, 457 may cause infinite loops
4. **Handle zero-length messages** properly - Don't allocate 1-byte dummy buffers
5. **Validate message types** before processing

### Phase 3: Test Audio+Video

1. **Verify both streams captured:**
   ```bash
   ffprobe test.ts 2>&1 | grep "Stream #"
   # Should show:
   #   Stream #0:0: Audio: aac
   #   Stream #0:1: Video: h264
   ```

2. **Count frames:**
   ```bash
   ffprobe -count_frames test.ts
   # Should match FFmpeg output (e.g., 60 video frames)
   ```

3. **Test SRT streaming:**
   ```bash
   ./demo_rtmp_to_srt.sh  # Should show video with audio
   ```

---

## Files To Modify

1. **`gst/rtmp2chunk_v2.c`** - Main parser logic
   - Fix audio message handling
   - Remove infinite loop possibilities
   - Better error recovery

2. **`gst/rtmp2client.c`** - Message processing
   - Ensure audio frames create FLV tags
   - Handle audio timestamps correctly

3. **Demo scripts** (after fix):
   - `demo_rtmp_to_srt.sh` - Re-add audio pipeline
   - `demo_rtmp_to_udp.sh` - Re-add audio pipeline

---

## Success Criteria

### Must Have
- ✅ 60/60 video frames captured (maintain existing performance)
- ✅ All audio frames captured
- ✅ No "suspicious message length" errors
- ✅ No "message type 0" errors
- ✅ No pipeline stalls or infinite loops
- ✅ Valid output with both streams in ffprobe

### Nice To Have
- ✅ Audio and video synchronized
- ✅ Demo scripts work with audio+video
- ✅ SRT streaming with audio

---

## Reference Materials

### SRS Audio Handling
Download SRS source for reference:
```bash
curl -sL https://raw.githubusercontent.com/ossrs/srs/develop/trunk/src/protocol/srs_protocol_rtmp_stack.cpp > /tmp/srs_rtmp.cpp
```

Look at how SRS handles audio vs video messages (same logic, different type).

### RTMP Message Types
- Type 8 = AUDIO (same structure as VIDEO)
- Type 9 = VIDEO (working)
- Type 20 = AMF0 COMMAND (working)

Both audio and video should be handled identically by the chunk parser.

---

## Build & Test Commands

### Build
```bash
cd /Users/yarontorbaty/gst-rtmp2-server/build

# Compile V2 parser
cc -c ../gst/rtmp2chunk_v2.c -o libgstrtmp2server.dylib.p/gst_rtmp2chunk_v2.c.o \
  -Ilibgstrtmp2server.dylib.p -I. -I.. \
  -I/opt/homebrew/Cellar/gstreamer/1.26.7_1/include/gstreamer-1.0 \
  -I/opt/homebrew/Cellar/glib/2.86.1/include \
  -I/Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk/usr/include/ffi \
  -I/opt/homebrew/Cellar/glib/2.86.1/include/glib-2.0 \
  -I/opt/homebrew/Cellar/glib/2.86.1/lib/glib-2.0/include \
  -I/opt/homebrew/opt/gettext/include \
  -I/opt/homebrew/Cellar/pcre2/10.47/include \
  -I/opt/homebrew/Cellar/orc/0.4.41/include/orc-0.4 \
  -fdiagnostics-color=always -Wall -Winvalid-pch -O2 -g -DHAVE_CONFIG_H

# Link
cc -Wl,-dead_strip_dylibs -Wl,-headerpad_max_install_names \
  -shared -install_name @rpath/libgstrtmp2server.dylib \
  -o libgstrtmp2server.dylib libgstrtmp2server.dylib.p/*.o \
  -Wl,-rpath,/opt/homebrew/Cellar/glib/2.86.1/lib \
  -Wl,-rpath,/opt/homebrew/opt/gettext/lib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstreamer-1.0.dylib \
  /opt/homebrew/Cellar/glib/2.86.1/lib/libgobject-2.0.dylib \
  /opt/homebrew/Cellar/glib/2.86.1/lib/libglib-2.0.dylib \
  /opt/homebrew/opt/gettext/lib/libintl.dylib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstbase-1.0.dylib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstapp-1.0.dylib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstvideo-1.0.dylib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstaudio-1.0.dylib \
  /opt/homebrew/Cellar/glib/2.86.1/lib/libgio-2.0.dylib
```

### Test Audio+Video
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2chunk_v2:5,rtmp2client:4

# Terminal 1
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=av_test.flv

# Terminal 2
ffmpeg -re -f lavfi -i testsrc=duration=2:rate=30 \
  -f lavfi -i sine=duration=2 \
  -c:v libx264 -preset ultrafast -g 30 \
  -c:a aac -b:a 128k \
  -f flv rtmp://localhost:1935/live/test

# Verify
ffprobe av_test.flv 2>&1 | grep "Stream #"
# Should show BOTH audio and video streams
```

---

## Starting Point

1. Clone/pull latest code (already on main with v1.0.0)
2. Read `SIMPLE_SRT_TEST.md` to understand what's working
3. Review `gst/rtmp2chunk_v2.c` parser implementation
4. Add debug logging for audio messages
5. Reproduce the error with test command above
6. Fix parser to handle audio the same as video
7. Test until both streams work
8. Update demo scripts
9. Commit, PR, merge

---

## Expected Timeline

**2-4 hours** to debug and fix audio support.

The V2 parser architecture is solid - it just needs to properly handle audio messages alongside video.

---

## Copy This Prompt:

```
I need to add audio support to the gst-rtmp2-server V2 chunk parser.

Current status:
- Video-only streaming works perfectly (100% capture)
- Adding audio causes "Internal data stream error" and parser issues
- Repository: /Users/yarontorbaty/gst-rtmp2-server
- Issue: #4

Read AUDIO_SUPPORT_PROMPT.md for full details.

Fix the V2 parser to handle audio (type=8) messages alongside video (type=9).
Test until audio+video streaming works with 100% capture for both streams.

Don't stop until fully working and tested with valid audio+video output.
```

---

## Additional Notes

- The V2 parser's `ensure()` pattern is correct - don't change that
- Audio messages should be handled identically to video
- Check if chunk stream IDs conflict between audio/video
- Watch for infinite loops in the parser (lines with `continue`)
- Test with both FLV file output and SRT streaming

Good luck! The parser is 95% there - just needs audio message handling! 🎵

