# Next Session Plan - Chunk Parser V2 Implementation

## Context

### Repository
- **Location**: `/Users/yarontorbaty/gst-rtmp2-server`
- **Main branch**: v0.8.0 - 15-20% capture (production ready)
- **Work branch**: `flv-coordination` - frame tracking + parser v2 work
- **GitHub**: https://github.com/yarontorbaty/gst-rtmp2-server
- **Issue**: #2

### Current Status

**v0.8.0 in main**: 15-20% capture, production ready, merged and tagged  
**Frame tracking (on flv-coordination)**: Shows 9/60 frames received, 100% of those processed correctly  
**Bottleneck identified**: 51 frames (85%) lost in chunk parser layer BEFORE server even sees them

### The Problem

```
FFmpeg sends: 60 frames → [RTMP Chunk Layer] → Server receives: 9 frames (15%)
                              ↑
                         Missing 51 frames lost here
```

**Not** an I/O problem, **not** a threading problem - it's the chunk parser can't reassemble fragments fast enough at 30fps.

### What We Know Works

From frame tracking (commit a3061ca):
- ✅ Read thread receives data continuously
- ✅ All RTMP commands processed (releaseStream, FCPublish, createStream, _checkbw, publish)
- ✅ Publishing state reached
- ✅ Video frames that arrive are processed 100% correctly
- ✅ FLV tags created and queued
- ✅ Tags returned to GStreamer
- ❌ Only 9/60 frames arrive (chunk parser drops 51)

### Current Code Files
- `gst/rtmp2chunk.c` - Current parser (processes partial chunks, causes drops)
- `gst/rtmp2client.c` - Client handling with read thread (working well)
- `gst/gstrtmp2serversrc.c` - Server with frame tracking logs (working well)

## SRS-Based Parser V2 - Implementation Plan

### Phase 1: Core Buffer (2-3 hours)

Implement SRS's `SrsFastStream` equivalent:

```c
typedef struct {
  guint8 *data;
  gsize size;
  gsize read_pos;
  gsize write_pos;
  GInputStream *stream;  // To read more data when needed
} FastBuffer;

// Like SRS's in_buffer->grow(skt, N)
gboolean fast_buffer_ensure(FastBuffer *buf, gsize needed, GError **error) {
  while ((buf->write_pos - buf->read_pos) < needed) {
    // Read more from socket
    gssize n = g_input_stream_read(buf->stream, 
        buf->data + buf->write_pos, 
        buf->size - buf->write_pos, NULL, error);
    if (n <= 0) return FALSE;
    buf->write_pos += n;
  }
  return TRUE;
}
```

### Phase 2: Chunk Parsing (3-4 hours)

Follow SRS's `recv_interlaced_message` pattern exactly:

```c
gboolean parse_one_chunk(FastBuffer *buf, Rtmp2ChunkMessage **msg) {
  // 1. Ensure 1 byte for basic header
  if (!fast_buffer_ensure(buf, 1, &err)) return FALSE;
  
  // 2. Parse basic header
  guint8 fmt, csid;
  // ... parse
  
  // 3. Ensure bytes for message header based on fmt
  gsize header_size = (fmt == 0) ? 11 : (fmt == 1) ? 7 : (fmt == 2) ? 3 : 0;
  if (!fast_buffer_ensure(buf, header_size, &err)) return FALSE;
  
  // 4. Parse message header
  // ... parse
  
  // 5. Calculate chunk payload size
  gsize payload_needed = min(chunk_size, msg_length - bytes_received);
  
  // 6. Ensure payload bytes available
  if (!fast_buffer_ensure(buf, payload_needed, &err)) return FALSE;
  
  // 7. Copy payload
  // ... copy
  
  // 8. Return message if complete
  if (bytes_received == msg_length) {
    return TRUE;  // Message complete
  }
  return FALSE;  // Need more chunks
}
```

### Phase 3: Integration (1-2 hours)

1. Replace `rtmp2_chunk_parser_process` calls with v2
2. Pass GInputStream to parser for `fast_buffer_ensure`
3. Test and iterate

### Phase 4: Optimization (1-2 hours)

- Tune buffer sizes
- Add proper error handling
- Remove old parser code

## Expected Outcome

**With SRS's `ensure()` pattern**: 70-90% capture  
**Why**: Parser waits for complete data instead of fragmenting

## Files to Modify

1. `gst/rtmp2chunk_v2.c` - New implementation
2. `gst/rtmp2chunk_v2.h` - New header
3. `gst/rtmp2client.c` - Use v2 parser
4. `meson.build` - Add v2 to build

## Total Estimate

**8-12 hours** of focused implementation following SRS patterns exactly.

## Fallback

If parser v2 doesn't achieve 70%+, we have:
- Working 15-20% in v0.8.0
- Professional architecture  
- Complete understanding of the problem

## Starting Point for Next Session

### Current Branch
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
git checkout flv-coordination  # Work branch with frame tracking
```

### Build Commands
```bash
cd /Users/yarontorbaty/gst-rtmp2-server/build

# Standard compile flags
FLAGS="-Ilibgstrtmp2server.dylib.p -I. -I.. \
  -I/opt/homebrew/Cellar/gstreamer/1.26.7_1/include/gstreamer-1.0 \
  -I/opt/homebrew/Cellar/glib/2.86.1/include \
  -I/Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk/usr/include/ffi \
  -I/opt/homebrew/Cellar/glib/2.86.1/include/glib-2.0 \
  -I/opt/homebrew/Cellar/glib/2.86.1/lib/glib-2.0/include \
  -I/opt/homebrew/opt/gettext/include \
  -I/opt/homebrew/Cellar/pcre2/10.47/include \
  -I/opt/homebrew/Cellar/orc/0.4.41/include/orc-0.4 \
  -fdiagnostics-color=always -Wall -Winvalid-pch -O2 -g -DHAVE_CONFIG_H"

# Compile (add rtmp2chunk_v2.c when ready)
cc -c ../gst/rtmp2chunk_v2.c -o libgstrtmp2server.dylib.p/gst_rtmp2chunk_v2.c.o $FLAGS
cc -c ../gst/rtmp2client.c -o libgstrtmp2server.dylib.p/gst_rtmp2client.c.o $FLAGS

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

### Test Commands
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2client:5,rtmp2chunk:5

# Terminal 1
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=test.flv 2>&1 | tee test.log

# Terminal 2
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test

# Check
grep -c "📹 VIDEO FRAME" test.log  # Frames received
grep -c "✅ CREATED FLV TAG" test.log  # Tags created
grep -c "📤 RETURNING" test.log  # Tags returned
```

### SRS Reference
Download for reference:
```bash
curl -sL https://raw.githubusercontent.com/ossrs/srs/develop/trunk/src/protocol/srs_protocol_rtmp_stack.cpp > /tmp/srs_rtmp.cpp
```

Key sections:
- Lines 775-870: `recv_interlaced_message` (main loop)
- Lines 920-1160: `read_message_header` (header parsing)  
- Lines 1180-1230: `read_message_payload` (payload with grow())

### Files to Create/Modify

1. **gst/rtmp2chunk_v2.h** - New parser interface
2. **gst/rtmp2chunk_v2.c** - SRS-based implementation
3. **gst/rtmp2client.c** - Switch to v2 parser
4. **meson.build** - Add v2 to build

### Key Functions to Implement

1. `fast_buffer_ensure(buf, N)` - Like SRS's `in_buffer_->grow(skt_, N)`
2. `parse_basic_header_v2()` - With ensure() calls before each read
3. `parse_message_header_v2()` - Handles fmt 0/1/2/3 with ensure()
4. `read_chunk_payload_v2()` - Ensures payload bytes before memcpy
5. `rtmp2_chunk_parser_v2_process()` - Main loop like `recv_interlaced_message`

### Success Criteria

- 70%+ capture rate (42+ frames out of 60)
- Consistent across multiple runs
- No corrupted chunk messages (>1MB)
- All 60 FFmpeg frames reach server

This is doable in 8-12 focused hours following SRS patterns exactly.

