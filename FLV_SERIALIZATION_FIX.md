# Fix: Proper FLV Tag Serialization for flvdemux

## Problem Identified

`gstrtmp2serversrc.c` currently outputs **incomplete FLV data**:

1. ✅ Sends FLV file header (13 bytes) 
2. ❌ Sends **raw codec payload only** (just the H.264/AAC data from `Rtmp2FlvTag.data`)
3. ❌ Missing FLV tag headers (11 bytes per tag)
4. ❌ Missing previous tag size fields (4 bytes after each tag)

This causes `flvdemux` to stall because it's receiving malformed FLV stream data.

## Root Cause

**File**: `gst/gstrtmp2serversrc.c` line 667

```c
*buf = gst_buffer_ref (tag->data);  // ❌ Only sends payload, not full FLV tag
```

**File**: `gst/rtmp2flv.c` lines 147, 175, 184

```c
tag->data = gst_buffer_new_allocate (NULL, data_size - 1, NULL);  
// Stores ONLY payload (minus codec byte), NOT full FLV tag
```

## FLV Tag Format

Each FLV tag must be serialized as:

```
| Tag Header (11 bytes) | Tag Data (N bytes) | Previous Tag Size (4 bytes) |
```

### Tag Header Structure (11 bytes):
```
Byte 0:     Tag Type (8=audio, 9=video, 18=script)
Bytes 1-3:  Data Size (24-bit big-endian, size of tag data)
Bytes 4-6:  Timestamp (24-bit big-endian, in milliseconds)
Byte 7:     Timestamp Extended (upper 8 bits, for timestamps > 16.7M ms)
Bytes 8-10: Stream ID (always 0, 24-bit big-endian)
```

### Previous Tag Size (4 bytes):
```
32-bit big-endian: Size of previous tag (header + data), used for backward seeking
```

## Solution

### Option 1: Serialize in `gstrtmp2serversrc.c` (Recommended)

Modify `gst_rtmp2_server_src_create()` to wrap `tag->data` in proper FLV structure:

```c
// In gst/gstrtmp2serversrc.c, around line 657-667:

if (flv_tags) {
  tag = (Rtmp2FlvTag *) flv_tags->data;
  if (tag->data && gst_buffer_get_size (tag->data) > 0) {
    gsize payload_size = gst_buffer_get_size (tag->data);
    gsize total_size = 11 + payload_size + 4;  // header + data + prev_tag_size
    
    *buf = gst_buffer_new_allocate (NULL, total_size, NULL);
    GstMapInfo map_out, map_in;
    
    if (gst_buffer_map (*buf, &map_out, GST_MAP_WRITE) &&
        gst_buffer_map (tag->data, &map_in, GST_MAP_READ)) {
      
      guint8 *ptr = map_out.data;
      
      // Write FLV tag header (11 bytes)
      *ptr++ = (guint8) tag->tag_type;                    // Tag type
      *ptr++ = (payload_size >> 16) & 0xFF;               // Data size (24-bit BE)
      *ptr++ = (payload_size >> 8) & 0xFF;
      *ptr++ = payload_size & 0xFF;
      *ptr++ = (tag->timestamp >> 16) & 0xFF;             // Timestamp (24-bit BE)
      *ptr++ = (tag->timestamp >> 8) & 0xFF;
      *ptr++ = tag->timestamp & 0xFF;
      *ptr++ = (tag->timestamp >> 24) & 0xFF;             // Timestamp extended
      *ptr++ = 0; *ptr++ = 0; *ptr++ = 0;                 // Stream ID (always 0)
      
      // Write tag data (payload)
      memcpy (ptr, map_in.data, payload_size);
      ptr += payload_size;
      
      // Write previous tag size (header + data = 11 + payload_size)
      guint32 prev_tag_size = 11 + payload_size;
      *ptr++ = (prev_tag_size >> 24) & 0xFF;
      *ptr++ = (prev_tag_size >> 16) & 0xFF;
      *ptr++ = (prev_tag_size >> 8) & 0xFF;
      *ptr++ = prev_tag_size & 0xFF;
      
      gst_buffer_unmap (tag->data, &map_in);
      gst_buffer_unmap (*buf, &map_out);
      
      // Set timestamp
      GST_BUFFER_PTS (*buf) = tag->timestamp * GST_MSECOND;
      GST_BUFFER_DTS (*buf) = GST_BUFFER_PTS (*buf);
      
      GST_INFO_OBJECT (src, "📤 Serialized FLV tag: type=%s size=%zu ts=%u",
          tag->tag_type == RTMP2_FLV_TAG_VIDEO ? "video" : "audio",
          total_size, tag->timestamp);
      
      rtmp2_flv_tag_free (tag);
      g_list_free (flv_tags);
      g_mutex_unlock (&src->clients_lock);
      return GST_FLOW_OK;
    } else {
      gst_buffer_unmap (*buf, &map_out);
      gst_buffer_unref (*buf);
      GST_ERROR_OBJECT (src, "Failed to map buffers for FLV serialization");
      rtmp2_flv_tag_free (tag);
      g_list_free (flv_tags);
      g_mutex_unlock (&src->clients_lock);
      return GST_FLOW_ERROR;
    }
  }
}
```

### Option 2: Store Full Tag in `Rtmp2FlvTag.data`

Alternatively, modify `gst/rtmp2flv.c` to store the **complete FLV tag** (header + data + prev_tag_size) in `tag->data`, but this requires tracking previous tag sizes across calls.

**Recommendation**: Use Option 1—it's cleaner and keeps FLV serialization logic in the src element.

## Testing Commands

### 1. Build with fix
```bash
cd /Users/yarontorbaty/gst-rtmp2-server/build
cc -c ../gst/gstrtmp2serversrc.c -o libgstrtmp2server.dylib.p/gst_gstrtmp2serversrc.c.o \
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

cc -Wl,-dead_strip_dylibs -Wl,-headerpad_max_install_names -shared \
  -install_name @rpath/libgstrtmp2server.dylib -o libgstrtmp2server.dylib \
  libgstrtmp2server.dylib.p/*.o \
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

### 2. Test SRT pipeline (Terminal A)
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

gst-launch-1.0 -v \
  rtmp2serversrc port=1935 ! queue ! flvdemux name=d \
    d.video ! queue ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au ! queue ! mux. \
    d.audio ! queue ! aacparse ! audio/mpeg,stream-format=adts ! queue ! mux. \
  mpegtsmux name=mux ! srtsink uri="srt://:9000?mode=listener"
```

### 3. Push RTMP stream (Terminal B)
```bash
ffmpeg -re -f lavfi -i testsrc=duration=10:rate=30 \
  -f lavfi -i sine=duration=10 \
  -c:v libx264 -preset ultrafast -g 30 \
  -c:a aac \
  -f flv rtmp://localhost:1935/live/test
```

### 4. Verify playback (Terminal C)
```bash
ffplay srt://localhost:9000
```

## Success Criteria

✅ `flvdemux` emits video/audio pads immediately after receiving first tags  
✅ `ffplay` shows `aq=XXkB vq=XXkB` (data flowing, not `0KB`)  
✅ Video plays with < 200ms latency  
✅ No EOS required—stream stays live until ffmpeg stops

## Current Status

- FLV header is sent correctly (13 bytes)
- Tag payloads are parsed correctly by RTMP parser
- Missing: FLV tag serialization wrapper
- Once fixed, SRT live streaming will work natively with `flvdemux`

