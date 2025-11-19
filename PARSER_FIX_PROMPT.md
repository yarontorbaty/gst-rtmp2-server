# Chat Prompt: Fix V2 Parser Buffer Compaction Bug

## Context

**Repository:** `/Users/yarontorbaty/gst-rtmp2-server`  
**Current Status:** Video-only works (100%), audio+video broken (~1 frame)  
**Critical Bug:** Buffer compaction mid-stream causes parser corruption  
**Goal:** Fix parser to achieve 60/60 frames with audio+video

---

## What's Working ✅

### Video-Only Streaming
- **Frame Capture:** 60/60 frames (100%)
- **File:** `test_video_only.flv` (76KB)
- **Command:**
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=test.flv

ffmpeg -re -f lavfi -i testsrc=duration=2:rate=30 \
  -c:v libx264 -preset ultrafast -g 30 \
  -f flv rtmp://localhost:1935/live/test

# Result: 60/60 frames ✅
```

### Audio Support Code
- Audio message (type=8) parsing: ✅ Works
- FLV tag creation: ✅ Works
- Audio+video interleaving: ✅ Works when parser doesn't corrupt

---

## What's Broken ❌

### Audio+Video Streaming

**Symptom:**
```bash
ffmpeg -re -f lavfi -i testsrc=duration=2:rate=30 \
  -f lavfi -i sine=duration=2 \
  -c:v libx264 -c:a aac \
  -f flv rtmp://localhost:1935/live/test

# Result: Only 1-4 frames captured (should be 60)
```

**FFmpeg Log Shows:**
- Sends all 60 video + 88 audio packets successfully
- Server receives only ~4 frames
- Connection closes early

**Parser Logs Show:**
```
✅ Message complete! type=9, length=6666 (video frame at pos=5123/7693)
fmt=2 csid=4 type=0 len=0 ← GARBAGE!
fmt=2 csid=47 type=191 len=16686206 ← VIDEO DATA AS HEADER!
fmt=3 csid=172 ← Invalid continuation
```

---

## Root Cause (IDENTIFIED)

**File:** `gst/rtmp2chunk_v2.c`  
**Function:** `rtmp2_fast_buffer_ensure()` lines 91-98

**The Bug:**

```c
// Line 91-98: Buffer compaction logic
if (buf->read_pos > 0) {
  if (available > 0) {
    memmove (buf->data, buf->data + buf->read_pos, available);
  }
  buf->write_pos = available;
  buf->read_pos = 0;  // ← RESETS TO 0 MID-STREAM!
  GST_DEBUG ("ensure: compacted buffer, new write_pos=%zu", buf->write_pos);
}
```

**What Happens:**

1. **Before compaction:**
   - read_pos=5123, write_pos=7693
   - Position 5123-7693 contains: continuation of video frame + next chunks

2. **After compaction:**
   - Data moves to position 0
   - read_pos=0, write_pos=2570
   - **Parser thinks position 0 is a new chunk header**
   - **But it's actually video payload data!**

3. **Result:**
   - Parser interprets video bytes (0x84, 0xaf, 0x9a, etc.) as RTMP headers
   - Creates fake messages: csid=4, csid=47, csid=172
   - Stream corrupts, only 1-4 frames captured

---

## The Fix

**Strategy:** Only compact buffer when NECESSARY, not every time

**Current Code (BROKEN):**
```c
if (buf->read_pos > 0) {
  // Always compacts when read_pos > 0
  memmove(buf->data, buf->data + buf->read_pos, available);
  buf->read_pos = 0;
}
```

**Fixed Code (from SRS pattern):**
```c
// Only compact if we actually need the space
gsize space_at_end = buf->capacity - buf->write_pos;
if (buf->read_pos > 0 && space_at_end < needed) {
  // Only compact when no room at end
  memmove(buf->data, buf->data + buf->read_pos, available);
  buf->read_pos = 0;
}
```

**Why This Works:**
- 64KB buffer has plenty of space
- Compaction only happens when truly needed
- Avoids mid-stream position resets

---

## Additional Validations Needed

Based on SRS implementation:

### 1. Reject Invalid Fresh Chunks
```c
// Type 2/3 cannot start NEW chunk streams
if (!msg && (chunk_type == RTMP2_CHUNK_TYPE_2 || chunk_type == RTMP2_CHUNK_TYPE_3)) {
  GST_WARNING("Fresh chunk stream with Type 2/3 - this is garbage data");
  continue;  // Skip this byte
}
```

### 2. Handle Type 0 on Partial Messages  
```c
// Type 0 on existing stream = abandon old message, start new
if (msg && msg->bytes_received > 0 && chunk_type == RTMP2_CHUNK_TYPE_0) {
  // Free old partial message
  cleanup_message(msg);
  msg = NULL;
}
```

### 3. Skip Type 2 with Uninitialized Type
```c
// Type 2 inherits type/length - if type=0, it's garbage
if (chunk_type == RTMP2_CHUNK_TYPE_2 && msg->message_type == 0) {
  GST_WARNING("Type 2 with type=0 - skipping garbage");
  continue;
}
```

---

## Test Commands

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

# Terminal 1
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=test.flv

# Terminal 2  
ffmpeg -re -f lavfi -i testsrc=duration=2:rate=30 \
  -f lavfi -i sine=duration=2 \
  -c:v libx264 -preset ultrafast -g 30 \
  -c:a aac -f flv rtmp://localhost:1935/live/test

# Verify
ffprobe -count_frames test.flv 2>&1 | grep nb_read_frames
# Should show: 60 video frames, ~88 audio frames
```

### Debug Logging
```bash
export GST_DEBUG=rtmp2chunk_v2:6,rtmp2client:4

# Look for:
# - "compacted buffer" messages
# - Position jumps (4097 → 0)
# - Garbage chunk stream IDs (csid > 50)
# - Suspicious message lengths (> 10MB)
```

---

## Success Criteria

### Must Have
- ✅ 60/60 video frames with audio+video
- ✅ ~88 audio frames captured
- ✅ No "compacted buffer" during active streaming
- ✅ No garbage messages (type=0, csid > 50)
- ✅ FFmpeg completes full 2 seconds

### Nice To Have
- ✅ Video-only still works (maintain 60/60)
- ✅ No performance regression
- ✅ Clean logs (no warnings)

---

## Reference Materials

### SRS Buffer Management
```cpp
// SRS only compacts when actually out of space
// Reference: srs_protocol_rtmp_stack.cpp
if ((err = in_buffer_->grow(skt_, bytes_needed)) != srs_success) {
  return error;
}
// grow() only compacts if necessary
```

### Current Test Files
- `test_video_only.flv` - 60 frames (working baseline)
- `test_baseline_clean.flv` - 1 frame (shows bug)
- Logs show exact corruption point

---

## Files to Modify

**Primary:**
- `gst/rtmp2chunk_v2.c` lines 91-98 - Buffer compaction fix
- `gst/rtmp2chunk_v2.c` lines 410-418 - Add SRS validations

**Optional:**
- `gst/rtmp2chunk_v2.c` lines 419-430 - Skip type=0 messages
- `gst/rtmp2chunk_v2.c` lines 492-510 - Validate before returning

---

## Starting Point

1. Read `PARSER_BUG_REPORT.md` for detailed analysis
2. Read `INVESTIGATION_COMPLETE.md` for full context  
3. Review `gst/rtmp2chunk_v2.c` lines 75-115 (buffer ensure function)
4. Apply buffer compaction fix
5. Add SRS-style validations
6. Test until 60/60 frames achieved
7. Verify video-only still works
8. Commit and create PR

---

## Expected Timeline

**2-4 hours** - The bug is identified, just needs careful implementation.

---

## Copy This Prompt:

```
I need to fix the V2 parser buffer compaction bug in gst-rtmp2-server.

Current issue:
- Video-only: 60/60 frames ✅
- Audio+video: Only 1-4 frames ❌
- Root cause: Buffer compaction at position 4096+ resets read_pos to 0

Repository: /Users/yarontorbaty/gst-rtmp2-server

Read PARSER_BUG_REPORT.md and INVESTIGATION_COMPLETE.md for full details.

Fix the buffer compaction in gst/rtmp2chunk_v2.c (lines 91-98) to only
compact when necessary, not every time. Add SRS-style validations to
reject garbage chunk headers.

Test until audio+video achieves 60/60 frames like video-only does.

Don't stop until fully working with both streams at 100% capture rate.
```

---

## Key Insights from Investigation

1. **Buffer compaction is the root cause** - happens at wrong time
2. **Video payload gets interpreted as headers** - position reset issue
3. **Type 2/3 for new streams = garbage** - should be rejected
4. **64KB buffer is plenty** - compaction rarely needed
5. **SRS only compacts when necessary** - smart buffer management

Good luck! The bug is well-understood - just needs careful fixing! 🔧

