# Continue: FLV Frame Capture Optimization - gst-rtmp2-server

## Current Status ✅

I have a working RTMP server (gst-rtmp2-server) that successfully receives streams from FFmpeg and writes FLV files to disk.

**Branch**: `main` (latest commit: `3a919dc`)  
**Repository**: https://github.com/yarontorbaty/gst-rtmp2-server  
**GitHub Issue**: #1 - https://github.com/yarontorbaty/gst-rtmp2-server/issues/1

## What's Working ✅

1. **FLV files are created** with valid content (not 0 bytes!) 🎉
2. **FLV header** (13 bytes) is properly generated and written
3. **Files are playable** with ffprobe, ffplay, VLC
4. **RTMP protocol** is 100% functional (handshake, connect, publish)
5. **Buffer flow** works end-to-end: FFmpeg → RTMP Server → GStreamer → filesink → disk
6. **Data pipeline** successfully processes and writes FLV tags

## Test Commands (Verified Working)

```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

# Terminal 1 - Server
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv

# Terminal 2 - Publisher
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=30 -t 5 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test

# Terminal 1 - Stop with Ctrl+C after FFmpeg finishes

# Check the result
ls -lh output.flv          # Should show >0 bytes
ffprobe output.flv         # Should show valid FLV video
```

## The Problem ⚠️

**Only ~10% of video frames are being captured.**

### Evidence

For a 2-second test stream:
- **Expected**: ~60 frames (2 sec × 30 fps) = 50-100 KB file
- **Actual**: 4-5 frames = 1.6 KB file
- **Duration**: 0.1 seconds captured instead of 2.0 seconds

### Symptoms

```bash
# Example output:
-rw-r--r--  1.6K  output.flv
Duration: 00:00:00.10, start: 0.033000, bitrate: 129 kb/s

# Logs show only sparse FLV tags being created:
0:00:03.101 INFO Returning FLV tag with 64 bytes (type=9)    # Frame 1
0:00:03.387 INFO Returning FLV tag with 216 bytes (type=9)   # Frame 2  
0:00:03.810 INFO Returning FLV tag with 646 bytes (type=9)   # Frame 3
0:00:03.905 INFO Returning FLV tag with 676 bytes (type=9)   # Frame 4
# Then nothing more...
```

## Root Cause Hypothesis

The multi-read socket buffer draining approach (lines 576-594 and 679-689 in `gst/gstrtmp2serversrc.c`) may still not be reading fast enough, OR there's a timing/synchronization issue where:

1. FFmpeg sends data continuously
2. Server reads from socket in bursts every 10ms
3. Socket buffer fills up and drops data
4. Only a fraction of frames get through

## Key Technical Details

### Architecture

```
FFmpeg (RTMP Publisher)
    ↓ TCP socket
Server Socket (accept)
    ↓ 
rtmp2_client_process_data() - reads 4KB chunks
    ↓
rtmp2_chunk_parser_process() - assembles RTMP messages
    ↓
FLV tag reconstruction (11-byte header + data + 4-byte trailer)
    ↓
pending_tags list
    ↓
create() function - returns buffers to GStreamer
    ↓
filesink - writes to disk
```

### Critical Files

1. **`gst/gstrtmp2serversrc.c`**
   - Lines 519-546: FLV header generation
   - Lines 559-603: Initial client processing with multi-read loop
   - Lines 608-705: Main retry loop for getting data
   - Lines 576-594: Multi-read socket draining (up to 50 reads)

2. **`gst/rtmp2client.c`**  
   - Line 463: Buffer size `guint8 buffer[4096]`
   - Lines 596-622: Socket reading logic (non-blocking)
   - Lines 870-942: FLV tag reconstruction from RTMP messages

3. **`gst/rtmp2chunk.c`**
   - Chunk parser that assembles RTMP messages

### Current Multi-Read Implementation

```c
/* Lines 576-594 in gstrtmp2serversrc.c */
for (read_attempts = 0; read_attempts < max_reads; read_attempts++) {
  gboolean processed = rtmp2_client_process_data (client, &error);
  if (!processed) {
    break; /* Stop if no more data */
  }
}
```

This reads up to 50 times (50 × 4KB = 200KB potential), but apparently isn't draining fast enough.

## What to Investigate

### Option 1: Increase Read Frequency
- Reduce 10ms sleep to 1-5ms in retry loop (line 664)
- Read more aggressively while FFmpeg is streaming

### Option 2: Increase Buffer Size
- Change `buffer[4096]` to `buffer[16384]` or larger in rtmp2client.c
- Read bigger chunks per iteration

### Option 3: Check Chunk Parser
- Add logging to `rtmp2_chunk_parser_process()` to verify all messages are assembled
- Check if chunks are being dropped

### Option 4: Socket Buffer Settings  
- Increase SO_RCVBUF socket option
- Check if OS socket buffer is overflowing

### Option 5: Remove Sleep Entirely During Active Streaming
- Keep reading continuously while `pending_tags` is growing
- Only sleep when truly no data available

## Debug Commands

```bash
# Enable detailed logging
export GST_DEBUG=rtmp2serversrc:6,rtmp2client:6,rtmp2chunk:6

# Count FLV tags created vs returned
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink 2>&1 | grep "Created FLV tag" | wc -l
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink 2>&1 | grep "Returning FLV" | wc -l

# Check FFmpeg frame count
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test 2>&1 | grep "frame="
```

## Success Criteria

After the fix:
- 2 second stream should produce ~60 FLV tags
- File size should be 50-100 KB (not 1-2 KB)
- Duration should match stream length
- All frames captured without drops

## Additional Context

- **Build system**: Meson + Clang on macOS
- **Build directory**: `/Users/yarontorbaty/gst-rtmp2-server/build`
- **GStreamer version**: 1.26.7
- **All previous issues SOLVED**: FLV header ✅, buffer flow ✅, caps negotiation ✅

## Documentation

See `FIX_SUMMARY.md` for complete history of fixes applied so far.

## The Goal

Fix the frame capture rate from ~10% to 100% so that the RTMP server can record complete video streams without dropping frames.

---

**Please help optimize the frame capture to get all 60 frames from a 2-second 30fps stream!**

