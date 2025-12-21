# Typed Pads Implementation - Handoff Status

## ✅ Completed

### Architecture Changes
- **Header updated** (`gst/gstrtmp2server.h`):
  - Changed from `GstPushSrc` to `GstElement`
  - Added `video_pad`, `audio_pad` fields
  - Added `GstTask` + `GRecMutex` for threading
  - Added `video_codec_data`, `audio_codec_data` for caps

### Skeleton Implementation
- **File**: `gst/gstrtmp2serversrc.c` (rewritten from scratch)
- **Backup**: Original FLV implementation saved to `gst/gstrtmp2serversrc_flv_backup.c`

**What's implemented**:
- ✅ Class/object init with GstElement base
- ✅ Sometimes pad templates (video/audio)
- ✅ Property system (host, port, application, etc.)
- ✅ Task framework (start/stop in state changes)
- ✅ Mutex/lock infrastructure

## ⏳ Remaining Work

### 1. Server Socket Logic (~50 LOC)
Copy from backup file lines 440-520:
- Create `GSocket` bound to `host:port`
- Set up `GSource` to watch for connections
- Implement `server_accept_cb()` to handle new clients

### 2. Client Management (~100 LOC)
Copy from backup file lines 220-440 + adapt:
- `rtmp2_client_new()` - Create client with RTMP2 handshake
- `on_client_handshake_done()` - Transition to ready for streaming
- `on_client_chunk_received()` - Parse RTMP messages
- `rtmp2_client_free()` - Cleanup client resources

### 3. Main Loop Implementation (~80 LOC)
Replace `gst_rtmp2_server_src_loop()` placeholder:
```c
static void
gst_rtmp2_server_src_loop (gpointer user_data)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (user_data);
  GstFlowReturn ret = GST_FLOW_OK;
  Rtmp2FlvTag *tag = NULL;

  /* 1. Process I/O events */
  while (g_main_context_pending (src->context))
    g_main_context_iteration (src->context, FALSE);

  /* 2. Wait for active client */
  g_mutex_lock (&src->clients_lock);
  
  if (!src->active_client || !src->active_client->flv_parser.pending_tags) {
    g_mutex_unlock (&src->clients_lock);
    g_usleep (10000);  /* 10ms */
    return;
  }

  /* 3. Get next tag */
  g_mutex_lock (&src->active_client->flv_parser.pending_tags_lock);
  GList *first = src->active_client->flv_parser.pending_tags;
  if (first) {
    tag = (Rtmp2FlvTag *) first->data;
    src->active_client->flv_parser.pending_tags = 
        g_list_remove_link (src->active_client->flv_parser.pending_tags, first);
    g_list_free (first);
  }
  g_mutex_unlock (&src->active_client->flv_parser.pending_tags_lock);
  g_mutex_unlock (&src->clients_lock);

  if (!tag)
    return;

  /* 4. Route to appropriate pad */
  if (tag->tag_type == RTMP2_FLV_TAG_VIDEO) {
    ret = push_video_buffer (src, tag);
  } else if (tag->tag_type == RTMP2_FLV_TAG_AUDIO) {
    ret = push_audio_buffer (src, tag);
  }

  rtmp2_flv_tag_free (tag);

  /* 5. Handle flow errors */
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (src, "Flow error: %s", gst_flow_get_name (ret));
    gst_task_pause (src->task);
  }
}
```

### 4. Dynamic Pad Creation (~60 LOC)
```c
static GstPad *
ensure_video_pad (GstRtmp2ServerSrc *src, Rtmp2FlvTag *tag)
{
  g_mutex_lock (&src->pad_lock);
  
  if (!src->video_pad) {
    /* Create pad */
    src->video_pad = gst_pad_new_from_static_template (&video_src_template, "video_0");
    
    /* Handle codec headers */
    if (is_avc_sequence_header (tag)) {
      src->video_codec_data = extract_avc_codec_data (tag);
    }
    
    /* Set caps */
    GstCaps *caps = gst_caps_new_simple ("video/x-h264",
        "stream-format", G_TYPE_STRING, "avc",
        "alignment", G_TYPE_STRING, "au",
        NULL);
    
    if (src->video_codec_data) {
      gst_caps_set_simple (caps, 
          "codec_data", GST_TYPE_BUFFER, src->video_codec_data, 
          NULL);
    }
    
    /* Activate and add */
    gst_pad_set_active (src->video_pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (src), src->video_pad);
    
    GST_INFO_OBJECT (src, "Created video pad with caps: %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    
    src->have_video = TRUE;
    check_no_more_pads (src);
  }
  
  g_mutex_unlock (&src->pad_lock);
  return src->video_pad;
}

/* Similar for ensure_audio_pad() */
```

### 5. Codec Header Extraction (~50 LOC)
```c
static gboolean
is_avc_sequence_header (Rtmp2FlvTag *tag)
{
  /* AVC sequence header:
   * - video_codec == H264 (7)
   * - video_keyframe == TRUE
   * - First byte of payload == 0x00 (AVC packet type = sequence header)
   */
  if (tag->tag_type != RTMP2_FLV_TAG_VIDEO)
    return FALSE;
  if (tag->video_codec != RTMP2_FLV_VIDEO_CODEC_H264)
    return FALSE;
  if (!tag->video_keyframe)
    return FALSE;
  
  GstMapInfo map;
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return FALSE;
  
  gboolean is_seq_hdr = (map.size > 0 && map.data[0] == 0x00);
  gst_buffer_unmap (tag->data, &map);
  return is_seq_hdr;
}

static GstBuffer *
extract_avc_codec_data (Rtmp2FlvTag *tag)
{
  /* AVC sequence header format:
   * [0] = 0x00 (AVC packet type)
   * [1-3] = composition time (0x000000)
   * [4...] = AVCDecoderConfigurationRecord (this is codec_data)
   */
  GstMapInfo map;
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return NULL;
  
  if (map.size < 5) {
    gst_buffer_unmap (tag->data, &map);
    return NULL;
  }
  
  /* Extract codec_data (skip first 4 bytes) */
  GstBuffer *codec_data = gst_buffer_new_allocate (NULL, map.size - 4, NULL);
  gst_buffer_fill (codec_data, 0, map.data + 4, map.size - 4);
  
  gst_buffer_unmap (tag->data, &map);
  return codec_data;
}

/* Similar for is_aac_sequence_header() / extract_aac_codec_data() */
```

### 6. Buffer Pushing (~40 LOC)
```c
static GstFlowReturn
push_video_buffer (GstRtmp2ServerSrc *src, Rtmp2FlvTag *tag)
{
  GstPad *pad;
  GstBuffer *buffer;
  GstFlowReturn ret;
  
  /* Ensure pad exists */
  pad = ensure_video_pad (src, tag);
  if (!pad)
    return GST_FLOW_ERROR;
  
  /* Skip sequence headers (already extracted codec_data) */
  if (is_avc_sequence_header (tag))
    return GST_FLOW_OK;
  
  /* Skip non-NALU data (AVC packet type check) */
  GstMapInfo map;
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return GST_FLOW_ERROR;
  
  if (map.size < 5 || map.data[0] != 0x01) {  /* 0x01 = AVC NALU */
    gst_buffer_unmap (tag->data, &map);
    return GST_FLOW_OK;
  }
  gst_buffer_unmap (tag->data, &map);
  
  /* Create buffer (skip first 4 bytes: packet type + composition time) */
  buffer = gst_buffer_copy_region (tag->data, GST_BUFFER_COPY_ALL, 4,
      gst_buffer_get_size (tag->data) - 4);
  
  /* Set timestamp */
  GST_BUFFER_PTS (buffer) = tag->timestamp * GST_MSECOND;
  GST_BUFFER_DTS (buffer) = GST_BUFFER_PTS (buffer);
  
  if (tag->video_keyframe)
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  
  /* Push */
  ret = gst_pad_push (pad, buffer);
  GST_LOG_OBJECT (src, "Pushed video buffer: ts=%u size=%zu ret=%s",
      tag->timestamp, gst_buffer_get_size (buffer), gst_flow_get_name (ret));
  
  return ret;
}

/* Similar for push_audio_buffer() */
```

### 7. State Change Handlers (~30 LOC)
Complete the TODO sections in `gst_rtmp2_server_src_change_state()`:
- NULL→READY: Copy server socket creation from backup lines 447-520
- READY→NULL: Copy cleanup from backup lines 524-575

## 📝 Integration Steps

1. **Copy client management code** from `gstrtmp2serversrc_flv_backup.c`
2. **Adapt** to use typed pads instead of FLV serialization
3. **Implement** the 7 functions listed above
4. **Build** and test incrementally

## 🎯 Expected Outcome

```bash
# This should work:
gst-launch-1.0 -v rtmp2serversrc port=1935 name=src \
  src.video_0 ! h264parse ! mpegtsmux ! srtsink uri="srt://:9000?mode=listener"

# And show:
# - "Created video pad" message
# - Video buffers flowing
# - ffplay srt://localhost:9000 plays video
```

## 📂 Files Status

- ✅ `gst/gstrtmp2server.h` - Updated for GstElement
- ⏳ `gst/gstrtmp2serversrc.c` - Skeleton done, needs 7 functions implemented
- 📦 `gst/gstrtmp2serversrc_flv_backup.c` - Reference for server/client logic
- 📋 `TYPED_PADS_IMPLEMENTATION_PLAN.md` - Architecture spec
- 📋 `SRT_LIVE_STREAMING_STATUS.md` - Overall status
- 📋 `SRT_LIVE_STREAMING_TASK.md` - Original task spec

## ⏰ Time Estimate

- Implementing 7 functions: **2 hours**
- Testing + debugging: **1 hour**
- **Total: 3 hours** to completion

## 🚀 Next Step

Implement the 7 functions listed above in `gst/gstrtmp2serversrc.c` to complete the typed pads architecture.

