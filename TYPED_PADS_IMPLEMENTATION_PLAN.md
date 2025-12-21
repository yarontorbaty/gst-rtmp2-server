# Implementation Plan: Typed Pads for rtmp2serversrc

## Current Status
- Backed up FLV implementation to `gst/gstrtmp2serversrc_flv_backup.c`
- Updated header `gst/gstrtmp2server.h` to use `GstElement` instead of `GstPushSrc`

## Architecture Changes

### Before (FLV-based)
```
RTMP chunks → FLV tags → serialize FLV → single src pad (video/x-flv)
                                           ↓
                                        flvdemux → video/audio pads
```

### After (Typed pads)
```
RTMP chunks → parse message type 8/9 → extract payload
                    ↓                       ↓
              type 8 (audio)          type 9 (video)
                    ↓                       ↓
              audio pad               video pad
           (audio/mpeg,raw)      (video/x-h264,avc)
```

## Implementation Steps

### 1. Update Class Definition
- Change parent from `GstPushSrc` to `GstElement`
- Remove `create()` vfunc, add state change handlers
- Define sometimes pad templates for video/audio

### 2. Add Pad Templates
```c
static GstStaticPadTemplate video_src_template = 
  GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-h264, stream-format=avc, alignment=au"));

static GstStaticPadTemplate audio_src_template = 
  GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/mpeg, mpegversion=4, stream-format=raw"));
```

### 3. Implement Task-Based Data Flow
Replace `gst_rtmp2_server_src_create()` with a GstTask that runs in a loop:

```c
static void
gst_rtmp2_server_src_loop (GstRtmp2ServerSrc *src)
{
  // 1. Wait for active client
  // 2. Check for pending FLV tags
  // 3. For each tag:
  //    - If video: create/use video pad, push buffer
  //    - If audio: create/use audio pad, push buffer
  //    - Handle codec headers (AVC/AAC sequence headers)
  // 4. Handle EOS when client disconnects
}
```

### 4. Dynamic Pad Creation
```c
static GstPad *
ensure_video_pad (GstRtmp2ServerSrc *src, Rtmp2FlvTag *tag)
{
  if (!src->video_pad) {
    // Create pad from template
    src->video_pad = gst_pad_new_from_static_template (&video_src_template, "video_0");
    
    // Extract codec_data from AVC sequence header
    if (is_avc_sequence_header(tag)) {
      src->video_codec_data = extract_codec_data(tag);
    }
    
    // Set caps
    GstCaps *caps = gst_caps_new_simple ("video/x-h264",
        "stream-format", G_TYPE_STRING, "avc",
        "alignment", G_TYPE_STRING, "au",
        "codec_data", GST_TYPE_BUFFER, src->video_codec_data,
        NULL);
    gst_pad_set_caps (src->video_pad, caps);
    
    // Activate and add to element
    gst_pad_set_active (src->video_pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (src), src->video_pad);
    
    // Signal no-more-pads if we have both streams or only expect one
    check_and_signal_no_more_pads (src);
  }
  return src->video_pad;
}
```

### 5. Handle Codec Headers
RTMP sends special "sequence header" messages:

**AVC Sequence Header** (video):
- Frame type = 1 (keyframe)
- Codec ID = 7 (H.264)
- AVC packet type = 0 (sequence header)
- Contains: AVCDecoderConfigurationRecord (SPS/PPS)

**AAC Sequence Header** (audio):
- Sound format = 10 (AAC)
- AAC packet type = 0 (sequence header)
- Contains: AudioSpecificConfig

Extract these and set as `codec_data` in caps.

### 6. State Management
```c
static GstStateChangeReturn
gst_rtmp2_server_src_change_state (GstElement *element, GstStateChange transition)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (element);
  
  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      // Start server socket
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      // Create and start task
      gst_task_start (src->task);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      // Stop task
      gst_task_stop (src->task);
      gst_task_join (src->task);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      // Cleanup server socket
      break;
  }
  
  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}
```

## Key Functions to Implement

1. `gst_rtmp2_server_src_init()` - Initialize task, mutexes, pads to NULL
2. `gst_rtmp2_server_src_loop()` - Main data flow loop
3. `ensure_video_pad()` / `ensure_audio_pad()` - Dynamic pad creation
4. `extract_avc_codec_data()` / `extract_aac_codec_data()` - Parse sequence headers
5. `is_avc_sequence_header()` / `is_aac_sequence_header()` - Detect codec headers
6. `push_video_buffer()` / `push_audio_buffer()` - Push to pads with proper timestamps
7. `gst_rtmp2_server_src_change_state()` - Handle state transitions

## Testing Plan

1. **Build**: Compile with typed pads implementation
2. **Test video-only**:
   ```bash
   gst-launch-1.0 rtmp2serversrc port=1935 name=src \
     src.video_0 ! h264parse ! fakesink
   ```
3. **Test audio+video to SRT**:
   ```bash
   gst-launch-1.0 rtmp2serversrc port=1935 name=src \
     src.video_0 ! queue ! h264parse ! video/x-h264,stream-format=byte-stream ! mux. \
     src.audio_0 ! queue ! aacparse ! audio/mpeg,stream-format=adts ! mux. \
     mpegtsmux name=mux ! srtsink uri="srt://:9000?mode=listener"
   ```
4. **Verify with ffplay**: `ffplay srt://localhost:9000` should show live video/audio

## Files to Modify

1. ✅ `gst/gstrtmp2server.h` - Updated struct (GstElement, typed pads)
2. ⏳ `gst/gstrtmp2serversrc.c` - Full rewrite for typed pads
3. 📋 `gst/rtmp2flv.c` - Keep for parsing, but use differently
4. 📋 `gst/rtmp2client.c` - May need updates for direct message routing

## Estimated Complexity

- **Lines of code**: ~400-500 LOC for new implementation
- **Time**: 2-3 hours implementation + 1 hour testing/debugging
- **Risk**: Medium (large refactor but well-specified pattern from rtspsrc/flvdemux)

## Next Step

Proceed with full implementation of `gst/gstrtmp2serversrc.c` with typed pads architecture.

