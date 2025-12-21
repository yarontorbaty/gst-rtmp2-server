/* GStreamer RTMP2 Server Source Element
 * Copyright (C) 2024 Yaron Torbaty <yaron.torbaty@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-rtmp2serversrc
 * @title: rtmp2serversrc
 * @short_description: RTMP server source element
 *
 * rtmp2serversrc is a source element that listens for incoming RTMP
 * connections on a configurable port. When a client connects and starts
 * publishing, the element outputs the received audio/video data as FLV.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv
 * ]|
 * Save incoming RTMP stream to file.
 *
 * |[
 * gst-launch-1.0 rtmp2serversrc port=1935 ! flvdemux name=demux \
 *   demux.video ! queue ! h264parse ! mpegtsmux name=mux ! srtsink uri="srt://:9000" \
 *   demux.audio ! queue ! aacparse ! mux.
 * ]|
 * Re-stream RTMP to SRT.
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtmp2server.h"
#include "gstrtmp2serversrc.h"
#include "rtmp2chunk.h"
#include "rtmp2chunk_v2.h"

#include <string.h>
#include <sys/socket.h>
#include <gio/gio.h>

GST_DEBUG_CATEGORY_STATIC (gst_rtmp2_server_src_debug);
#define GST_CAT_DEFAULT gst_rtmp2_server_src_debug

enum {
  PROP_0,
  PROP_HOST,
  PROP_PORT,
  PROP_APPLICATION,
  PROP_STREAM_KEY,
  PROP_TIMEOUT,
  PROP_TLS,
  PROP_CERTIFICATE,
  PROP_PRIVATE_KEY
};

/* Always pad template - raw FLV output for simple pipelines */
static GstStaticPadTemplate src_template = 
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-flv"));

/* Sometimes pad templates - created dynamically when streams detected */
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

/* Forward declarations */
static void gst_rtmp2_server_src_finalize (GObject *object);
static void gst_rtmp2_server_src_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_rtmp2_server_src_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_rtmp2_server_src_change_state (GstElement *element,
    GstStateChange transition);
static void gst_rtmp2_server_src_loop (gpointer user_data);
static gboolean server_accept_cb (GSocket *socket, GIOCondition condition,
    gpointer user_data);

#define gst_rtmp2_server_src_parent_class parent_class
G_DEFINE_TYPE (GstRtmp2ServerSrc, gst_rtmp2_server_src, GST_TYPE_ELEMENT);

static void
gst_rtmp2_server_src_class_init (GstRtmp2ServerSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_rtmp2_server_src_set_property;
  gobject_class->get_property = gst_rtmp2_server_src_get_property;
  gobject_class->finalize = gst_rtmp2_server_src_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rtmp2_server_src_change_state);

  g_object_class_install_property (gobject_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "Address to bind to", "0.0.0.0",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_uint ("port", "Port",
          "TCP port to listen on", 1, 65535, 1935,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_APPLICATION,
      g_param_spec_string ("application", "Application",
          "RTMP application name", "live",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STREAM_KEY,
      g_param_spec_string ("stream-key", "Stream Key",
          "If set, only accept this stream key", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "Timeout",
          "Client timeout in seconds", 1, 3600, 30,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TLS,
      g_param_spec_boolean ("tls", "TLS",
          "Enable TLS/SSL encryption", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CERTIFICATE,
      g_param_spec_string ("certificate", "Certificate",
          "PEM certificate file for TLS", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PRIVATE_KEY,
      g_param_spec_string ("private-key", "Private Key",
          "PEM private key file for TLS", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTMP2 Server Source",
      "Source/Network",
      "Receive audio/video stream from an RTMP client",
      "Yaron Torbaty <yaron.torbaty@gmail.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_add_static_pad_template (gstelement_class, &video_src_template);
  gst_element_class_add_static_pad_template (gstelement_class, &audio_src_template);

  GST_DEBUG_CATEGORY_INIT (gst_rtmp2_server_src_debug, "rtmp2serversrc", 0,
      "RTMP2 Server Source");

  /* Initialize debug categories for dependent modules */
  rtmp2_client_debug_init ();
  rtmp2_chunk_debug_init ();
  rtmp2_chunk_v2_debug_init ();
}

static void
gst_rtmp2_server_src_init (GstRtmp2ServerSrc *src)
{
  /* Properties */
  src->host = g_strdup ("0.0.0.0");
  src->port = 1935;
  src->application = g_strdup ("live");
  src->stream_key = NULL;
  src->timeout = 30;
  src->tls = FALSE;
  src->certificate = NULL;
  src->private_key = NULL;

  /* Server state */
  src->server_socket = NULL;
  src->server_source = NULL;
  src->context = NULL;
  src->event_thread = NULL;
  src->running = FALSE;
  src->clients = NULL;
  g_mutex_init (&src->clients_lock);
  src->tls_cert = NULL;
  src->tls_key = NULL;

  /* Active client */
  src->active_client = NULL;

  /* Pads */
  src->video_pad = NULL;
  src->audio_pad = NULL;
  g_mutex_init (&src->pad_lock);
  
  /* Create the always-present src pad for FLV output */
  src->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (src->srcpad);
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);
  src->srcpad_started = FALSE;
  src->eos_wait_start = 0;

  /* Task */
  g_rec_mutex_init (&src->task_lock);
  src->task = gst_task_new ((GstTaskFunction) gst_rtmp2_server_src_loop, src, NULL);
  gst_task_set_lock (src->task, &src->task_lock);

  /* Stream info */
  src->have_video = FALSE;
  src->have_audio = FALSE;
  src->video_caps = NULL;
  src->audio_caps = NULL;
  src->video_codec_data = NULL;
  src->audio_codec_data = NULL;
}

static void
gst_rtmp2_server_src_finalize (GObject *object)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (object);

  g_free (src->host);
  g_free (src->application);
  g_free (src->stream_key);
  g_free (src->certificate);
  g_free (src->private_key);

  if (src->video_caps)
    gst_caps_unref (src->video_caps);
  if (src->audio_caps)
    gst_caps_unref (src->audio_caps);
  if (src->video_codec_data)
    gst_buffer_unref (src->video_codec_data);
  if (src->audio_codec_data)
    gst_buffer_unref (src->audio_codec_data);

  g_mutex_clear (&src->clients_lock);
  g_mutex_clear (&src->pad_lock);
  g_rec_mutex_clear (&src->task_lock);

  if (src->task) {
    gst_object_unref (src->task);
    src->task = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Property getters/setters */
static void
gst_rtmp2_server_src_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (object);

  switch (prop_id) {
    case PROP_HOST:
      g_free (src->host);
      src->host = g_value_dup_string (value);
      break;
    case PROP_PORT:
      src->port = g_value_get_uint (value);
      break;
    case PROP_APPLICATION:
      g_free (src->application);
      src->application = g_value_dup_string (value);
      break;
    case PROP_STREAM_KEY:
      g_free (src->stream_key);
      src->stream_key = g_value_dup_string (value);
      break;
    case PROP_TIMEOUT:
      src->timeout = g_value_get_uint (value);
      break;
    case PROP_TLS:
      src->tls = g_value_get_boolean (value);
      break;
    case PROP_CERTIFICATE:
      g_free (src->certificate);
      src->certificate = g_value_dup_string (value);
      break;
    case PROP_PRIVATE_KEY:
      g_free (src->private_key);
      src->private_key = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtmp2_server_src_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, src->host);
      break;
    case PROP_PORT:
      g_value_set_uint (value, src->port);
      break;
    case PROP_APPLICATION:
      g_value_set_string (value, src->application);
      break;
    case PROP_STREAM_KEY:
      g_value_set_string (value, src->stream_key);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint (value, src->timeout);
      break;
    case PROP_TLS:
      g_value_set_boolean (value, src->tls);
      break;
    case PROP_CERTIFICATE:
      g_value_set_string (value, src->certificate);
      break;
    case PROP_PRIVATE_KEY:
      g_value_set_string (value, src->private_key);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Helper functions for codec headers */
static gboolean
is_avc_sequence_header (Rtmp2FlvTag *tag)
{
  if (tag->tag_type != RTMP2_FLV_TAG_VIDEO)
    return FALSE;
  if (tag->video_codec != RTMP2_FLV_VIDEO_CODEC_H264)
    return FALSE;
  if (!tag->video_keyframe)
    return FALSE;
  
  GstMapInfo map;
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return FALSE;
  
  /* data[0] = codec info, data[1] = AVC packet type (0=seq header) */
  gboolean is_seq_hdr = (map.size > 1 && map.data[1] == 0x00);
  gst_buffer_unmap (tag->data, &map);
  return is_seq_hdr;
}

static GstBuffer *
extract_avc_codec_data (Rtmp2FlvTag *tag)
{
  GstMapInfo map;
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return NULL;
  
  /* data[0]=codec_info, data[1]=avc_type, data[2-4]=comp_time, data[5+]=avcC */
  if (map.size < 6) {
    gst_buffer_unmap (tag->data, &map);
    return NULL;
  }
  
  /* Skip first 5 bytes: codec_info + packet type + composition time */
  GstBuffer *codec_data = gst_buffer_new_allocate (NULL, map.size - 5, NULL);
  gst_buffer_fill (codec_data, 0, map.data + 5, map.size - 5);
  
  gst_buffer_unmap (tag->data, &map);
  return codec_data;
}

static gboolean
is_aac_sequence_header (Rtmp2FlvTag *tag)
{
  if (tag->tag_type != RTMP2_FLV_TAG_AUDIO)
    return FALSE;
  if (tag->audio_codec != RTMP2_FLV_AUDIO_CODEC_AAC)
    return FALSE;
  
  GstMapInfo map;
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return FALSE;
  
  /* data[0] = audio info, data[1] = AAC packet type (0=seq header) */
  gboolean is_seq_hdr = (map.size > 1 && map.data[1] == 0x00);
  gst_buffer_unmap (tag->data, &map);
  return is_seq_hdr;
}

static GstBuffer *
extract_aac_codec_data (Rtmp2FlvTag *tag)
{
  GstMapInfo map;
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return NULL;
  
  /* data[0]=audio_info, data[1]=aac_type, data[2+]=AudioSpecificConfig */
  if (map.size < 3) {
    gst_buffer_unmap (tag->data, &map);
    return NULL;
  }
  
  /* Skip first 2 bytes: audio_info + AAC packet type */
  GstBuffer *codec_data = gst_buffer_new_allocate (NULL, map.size - 2, NULL);
  gst_buffer_fill (codec_data, 0, map.data + 2, map.size - 2);
  
  gst_buffer_unmap (tag->data, &map);
  return codec_data;
}

/* Pad management */
static void
check_no_more_pads (GstRtmp2ServerSrc *src)
{
  /* Signal no-more-pads once we've seen both video and audio (or determined we only have one) */
  if ((src->have_video && src->have_audio) ||
      (src->have_video && src->active_client && 
       src->active_client->state == RTMP2_CLIENT_STATE_DISCONNECTED) ||
      (src->have_audio && src->active_client && 
       src->active_client->state == RTMP2_CLIENT_STATE_DISCONNECTED)) {
    GST_INFO_OBJECT (src, "Signaling no-more-pads");
    gst_element_no_more_pads (GST_ELEMENT (src));
  }
}

static GstPad *
ensure_video_pad (GstRtmp2ServerSrc *src, Rtmp2FlvTag *tag)
{
  g_mutex_lock (&src->pad_lock);
  
  if (!src->video_pad) {
    src->video_pad = gst_pad_new_from_static_template (&video_src_template, "video_0");
    
    /* Extract codec data from sequence header */
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
    
    gst_pad_use_fixed_caps (src->video_pad);
    gst_pad_set_active (src->video_pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (src), src->video_pad);
    
    /* Send stream-start event */
    gchar *stream_id = gst_pad_create_stream_id (src->video_pad, GST_ELEMENT (src), "video");
    gst_pad_push_event (src->video_pad, gst_event_new_stream_start (stream_id));
    g_free (stream_id);
    
    /* Send caps event */
    gst_pad_push_event (src->video_pad, gst_event_new_caps (caps));
    
    /* Send segment event */
    GstSegment segment;
    gst_segment_init (&segment, GST_FORMAT_TIME);
    gst_pad_push_event (src->video_pad, gst_event_new_segment (&segment));
    
    GST_INFO_OBJECT (src, "Created video pad with caps: %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    
    src->have_video = TRUE;
    check_no_more_pads (src);
  }
  
  g_mutex_unlock (&src->pad_lock);
  return src->video_pad;
}

static GstPad *
ensure_audio_pad (GstRtmp2ServerSrc *src, Rtmp2FlvTag *tag)
{
  g_mutex_lock (&src->pad_lock);
  
  if (!src->audio_pad) {
    src->audio_pad = gst_pad_new_from_static_template (&audio_src_template, "audio_0");
    
    /* Extract codec data from sequence header */
    if (is_aac_sequence_header (tag)) {
      src->audio_codec_data = extract_aac_codec_data (tag);
    }
    
    /* Set caps */
    GstCaps *caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 4,
        "stream-format", G_TYPE_STRING, "raw",
        NULL);
    
    if (src->audio_codec_data) {
      gst_caps_set_simple (caps,
          "codec_data", GST_TYPE_BUFFER, src->audio_codec_data,
          NULL);
    }
    
    gst_pad_use_fixed_caps (src->audio_pad);
    gst_pad_set_active (src->audio_pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (src), src->audio_pad);
    
    /* Send stream-start event */
    gchar *stream_id = gst_pad_create_stream_id (src->audio_pad, GST_ELEMENT (src), "audio");
    gst_pad_push_event (src->audio_pad, gst_event_new_stream_start (stream_id));
    g_free (stream_id);
    
    /* Send caps event */
    gst_pad_push_event (src->audio_pad, gst_event_new_caps (caps));
    
    /* Send segment event */
    GstSegment segment;
    gst_segment_init (&segment, GST_FORMAT_TIME);
    gst_pad_push_event (src->audio_pad, gst_event_new_segment (&segment));
    
    GST_INFO_OBJECT (src, "Created audio pad with caps: %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    
    src->have_audio = TRUE;
    check_no_more_pads (src);
  }
  
  g_mutex_unlock (&src->pad_lock);
  return src->audio_pad;
}

/* Push raw FLV data to main srcpad */
static GstFlowReturn
push_to_srcpad (GstRtmp2ServerSrc *src, Rtmp2FlvTag *tag)
{
  GstBuffer *buffer;
  GstFlowReturn ret;
  guint8 flv_header[11 + 4];  /* FLV tag header (11 bytes) + prev tag size (4 bytes) */
  guint32 data_size;
  guint32 timestamp;
  
  /* Send stream-start and segment on first data */
  if (!src->srcpad_started) {
    gchar *stream_id = gst_pad_create_stream_id (src->srcpad, GST_ELEMENT (src), "flv");
    gst_pad_push_event (src->srcpad, gst_event_new_stream_start (stream_id));
    g_free (stream_id);
    
    GstCaps *caps = gst_caps_new_empty_simple ("video/x-flv");
    gst_pad_push_event (src->srcpad, gst_event_new_caps (caps));
    gst_caps_unref (caps);
    
    GstSegment segment;
    gst_segment_init (&segment, GST_FORMAT_TIME);
    gst_pad_push_event (src->srcpad, gst_event_new_segment (&segment));
    
    /* Push FLV file header */
    guint8 flv_file_header[13] = {
      'F', 'L', 'V',              /* Signature */
      0x01,                       /* Version */
      0x05,                       /* Flags: audio (0x04) + video (0x01) = 0x05 */
      0x00, 0x00, 0x00, 0x09,     /* Header size (9 bytes) */
      0x00, 0x00, 0x00, 0x00      /* Previous tag size (0 for first) */
    };
    GstBuffer *file_header = gst_buffer_new_allocate (NULL, 13, NULL);
    gst_buffer_fill (file_header, 0, flv_file_header, 13);
    gst_pad_push (src->srcpad, file_header);
    
    src->srcpad_started = TRUE;
    GST_INFO_OBJECT (src, "Srcpad initialized with FLV file header");
  }
  
  /* tag->data now includes the full RTMP message body including codec info byte */
  data_size = gst_buffer_get_size (tag->data);
  timestamp = tag->timestamp;
  
  /* Tag type */
  if (tag->tag_type == RTMP2_FLV_TAG_VIDEO)
    flv_header[0] = 0x09;
  else if (tag->tag_type == RTMP2_FLV_TAG_AUDIO)
    flv_header[0] = 0x08;
  else
    flv_header[0] = 0x12;  /* Script data */
  
  /* Data size (24-bit BE) */
  flv_header[1] = (data_size >> 16) & 0xff;
  flv_header[2] = (data_size >> 8) & 0xff;
  flv_header[3] = data_size & 0xff;
  
  /* Timestamp (24-bit BE + 8-bit extended) */
  flv_header[4] = (timestamp >> 16) & 0xff;
  flv_header[5] = (timestamp >> 8) & 0xff;
  flv_header[6] = timestamp & 0xff;
  flv_header[7] = (timestamp >> 24) & 0xff;
  
  /* Stream ID (always 0) */
  flv_header[8] = 0;
  flv_header[9] = 0;
  flv_header[10] = 0;
  
  /* Create buffer: header (11 bytes) + data (which includes codec info byte) */
  GstBuffer *header_buf = gst_buffer_new_allocate (NULL, 11, NULL);
  gst_buffer_fill (header_buf, 0, flv_header, 11);
  
  buffer = gst_buffer_append (header_buf, gst_buffer_ref (tag->data));
  
  /* Add previous tag size (11 byte header + data_size) */
  guint32 prev_tag_size = 11 + data_size;
  guint8 prev_size_bytes[4];
  prev_size_bytes[0] = (prev_tag_size >> 24) & 0xff;
  prev_size_bytes[1] = (prev_tag_size >> 16) & 0xff;
  prev_size_bytes[2] = (prev_tag_size >> 8) & 0xff;
  prev_size_bytes[3] = prev_tag_size & 0xff;
  
  GstBuffer *size_buf = gst_buffer_new_allocate (NULL, 4, NULL);
  gst_buffer_fill (size_buf, 0, prev_size_bytes, 4);
  buffer = gst_buffer_append (buffer, size_buf);
  
  GST_BUFFER_PTS (buffer) = timestamp * GST_MSECOND;
  
  ret = gst_pad_push (src->srcpad, buffer);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (src, "Srcpad push failed: %s", gst_flow_get_name (ret));
  }
  
  return ret;
}

/* Buffer pushing */
static GstFlowReturn
push_video_buffer (GstRtmp2ServerSrc *src, Rtmp2FlvTag *tag)
{
  GstPad *pad;
  GstBuffer *buffer;
  GstFlowReturn ret;
  GstMapInfo map;
  
  pad = ensure_video_pad (src, tag);
  if (!pad)
    return GST_FLOW_ERROR;
  
  /* Skip sequence headers */
  if (is_avc_sequence_header (tag))
    return GST_FLOW_OK;
  
  /* Check AVC packet type - data format is: [codec_info][avc_type][comp_time_3bytes][data] */
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return GST_FLOW_ERROR;
  
  GST_INFO_OBJECT (src, "Video tag: size=%zu first_bytes=[%02x %02x %02x %02x %02x]",
      map.size,
      map.size > 0 ? map.data[0] : 0,
      map.size > 1 ? map.data[1] : 0,
      map.size > 2 ? map.data[2] : 0,
      map.size > 3 ? map.data[3] : 0,
      map.size > 4 ? map.data[4] : 0);
  
  /* data[0] = codec info, data[1] = AVC packet type (0=seq header, 1=NALU, 2=end) */
  if (map.size < 6 || map.data[1] != 0x01) {  /* 0x01 = AVC NALU */
    gst_buffer_unmap (tag->data, &map);
    GST_DEBUG_OBJECT (src, "Skipping: not AVC NALU (packet type = 0x%02x)", 
        map.size > 1 ? map.data[1] : 0);
    return GST_FLOW_OK;
  }
  gst_buffer_unmap (tag->data, &map);
  
  /* Create buffer (skip first 5 bytes: codec_info + packet type + composition time) */
  buffer = gst_buffer_copy_region (tag->data, GST_BUFFER_COPY_ALL, 5,
      gst_buffer_get_size (tag->data) - 5);
  
  /* Set timestamp */
  GST_BUFFER_PTS (buffer) = tag->timestamp * GST_MSECOND;
  GST_BUFFER_DTS (buffer) = GST_BUFFER_PTS (buffer);
  
  if (tag->video_keyframe)
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  
  GST_INFO_OBJECT (src, "Pushing video buffer: size=%zu pts=%" GST_TIME_FORMAT,
      gst_buffer_get_size (buffer), GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));
  
  ret = gst_pad_push (pad, buffer);
  
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (src, "Video push failed: %s", gst_flow_get_name (ret));
  } else {
    GST_DEBUG_OBJECT (src, "Video push OK");
  }
  
  return ret;
}

static GstFlowReturn
push_audio_buffer (GstRtmp2ServerSrc *src, Rtmp2FlvTag *tag)
{
  GstPad *pad;
  GstBuffer *buffer;
  GstFlowReturn ret;
  GstMapInfo map;
  
  pad = ensure_audio_pad (src, tag);
  if (!pad)
    return GST_FLOW_ERROR;
  
  /* Skip sequence headers */
  if (is_aac_sequence_header (tag))
    return GST_FLOW_OK;
  
  /* Check AAC packet type - data format is: [audio_info][aac_type][data] */
  if (!gst_buffer_map (tag->data, &map, GST_MAP_READ))
    return GST_FLOW_ERROR;
  
  GST_INFO_OBJECT (src, "Audio tag: size=%zu first_bytes=[%02x %02x %02x]",
      map.size,
      map.size > 0 ? map.data[0] : 0,
      map.size > 1 ? map.data[1] : 0,
      map.size > 2 ? map.data[2] : 0);
  
  /* data[0] = audio info, data[1] = AAC packet type (0=seq header, 1=raw) */
  if (map.size < 3 || map.data[1] != 0x01) {  /* 0x01 = AAC raw */
    gst_buffer_unmap (tag->data, &map);
    GST_DEBUG_OBJECT (src, "Skipping: not AAC raw (packet type = 0x%02x)", 
        map.size > 1 ? map.data[1] : 0);
    return GST_FLOW_OK;
  }
  gst_buffer_unmap (tag->data, &map);
  
  /* Create buffer (skip first 2 bytes: audio_info + AAC packet type) */
  buffer = gst_buffer_copy_region (tag->data, GST_BUFFER_COPY_ALL, 2,
      gst_buffer_get_size (tag->data) - 2);
  
  /* Set timestamp */
  GST_BUFFER_PTS (buffer) = tag->timestamp * GST_MSECOND;
  GST_BUFFER_DTS (buffer) = GST_BUFFER_PTS (buffer);
  
  GST_INFO_OBJECT (src, "Pushing audio buffer: size=%zu pts=%" GST_TIME_FORMAT,
      gst_buffer_get_size (buffer), GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));
  
  ret = gst_pad_push (pad, buffer);
  
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (src, "Audio push failed: %s", gst_flow_get_name (ret));
  }
  
  return ret;
}

/* Event loop thread function */
static gpointer
event_loop_thread_func (gpointer user_data)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (user_data);
  
  GST_INFO_OBJECT (src, "Event loop thread started");
  
  while (src->running) {
    g_main_context_iteration (src->context, TRUE);
  }
  
  GST_INFO_OBJECT (src, "Event loop thread stopping");
  return NULL;
}

/* Client accept callback - simplified for typed pads */
static gboolean
server_accept_cb (GSocket *socket, GIOCondition condition, gpointer user_data)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (user_data);
  GSocketConnection *connection;
  GError *error = NULL;
  Rtmp2Client *client;
  GSocket *client_socket;

  client_socket = g_socket_accept (socket, NULL, &error);
  if (!client_socket) {
    GST_WARNING_OBJECT (src, "Failed to accept connection: %s",
        error ? error->message : "Unknown error");
    g_clear_error (&error);
    return G_SOURCE_CONTINUE;
  }

  g_socket_set_blocking (client_socket, FALSE);

  connection = g_socket_connection_factory_create_connection (client_socket);
  g_object_unref (client_socket);

  if (!connection) {
    GST_WARNING_OBJECT (src, "Failed to create connection");
    return G_SOURCE_CONTINUE;
  }

  /* Create client */
  client = rtmp2_client_new (connection, NULL);  /* NULL for non-TLS */
  if (!client) {
    GST_WARNING_OBJECT (src, "Failed to create client");
      g_object_unref (connection);
    return G_SOURCE_CONTINUE;
  }

  /* Initialize client settings */
  client->timeout_seconds = src->timeout;
  client->user_data = src;

  /* Store client reference */
  g_mutex_lock (&src->clients_lock);
  src->clients = g_list_append (src->clients, client);
  if (!src->active_client) {
    src->active_client = client;
    GST_INFO_OBJECT (src, "New active client connected");
  }
  g_mutex_unlock (&src->clients_lock);

  /* Start async reading - this triggers handshake */
  if (!rtmp2_client_start_reading (client, src->context)) {
    GST_WARNING_OBJECT (src, "Failed to start client reading");
    g_mutex_lock (&src->clients_lock);
    src->clients = g_list_remove (src->clients, client);
    if (src->active_client == client)
      src->active_client = NULL;
    g_mutex_unlock (&src->clients_lock);
    rtmp2_client_free (client);
    return G_SOURCE_CONTINUE;
  }
  
  GST_INFO_OBJECT (src, "Client connected and handshake started");

  return G_SOURCE_CONTINUE;
}

static void
gst_rtmp2_server_src_loop (gpointer user_data)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (user_data);
  Rtmp2FlvTag *tag = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  
  /* Wait for active client */
  g_mutex_lock (&src->clients_lock);
  
  if (!src->active_client) {
    g_mutex_unlock (&src->clients_lock);
    g_usleep (10000);  /* 10ms */
    return;
  }
  
  /* Log loop entry with queue size */
  gint queue_len = 0;
  g_mutex_lock (&src->active_client->flv_parser.pending_tags_lock);
  queue_len = g_list_length (src->active_client->flv_parser.pending_tags);
  g_mutex_unlock (&src->active_client->flv_parser.pending_tags_lock);
  if (queue_len > 0) {
    GST_INFO_OBJECT (src, "Loop: %d tags in queue", queue_len);
  }
  
  /* Check for pending tags - hold lock while checking state */
  g_mutex_lock (&src->active_client->flv_parser.pending_tags_lock);
  GList *first = src->active_client->flv_parser.pending_tags;
  if (first) {
    tag = (Rtmp2FlvTag *) first->data;
    src->active_client->flv_parser.pending_tags = 
        g_list_remove_link (src->active_client->flv_parser.pending_tags, first);
    g_list_free (first);
  }
  gint remaining_tags = g_list_length (src->active_client->flv_parser.pending_tags);
  g_mutex_unlock (&src->active_client->flv_parser.pending_tags_lock);
  
  /* Check for EOS - only if client was actually publishing */
  gboolean was_publishing = (src->active_client->state == RTMP2_CLIENT_STATE_PUBLISHING ||
                             src->active_client->publish_received);
  gboolean client_done = (src->active_client->state == RTMP2_CLIENT_STATE_DISCONNECTED ||
                          src->active_client->state == RTMP2_CLIENT_STATE_ERROR);
  
  g_mutex_unlock (&src->clients_lock);
  
  /* No tag available */
  if (!tag) {
    /* Only send EOS if client was publishing AND is now disconnected with no more tags */
    /* Wait a bit to make sure no more data is coming - the read thread might be lagging */
    if (was_publishing && client_done && remaining_tags == 0) {
      /* Wait 100ms before sending EOS to ensure all queued data is processed */
      gint64 now = g_get_monotonic_time ();
      
      if (src->eos_wait_start == 0) {
        src->eos_wait_start = now;
        GST_DEBUG_OBJECT (src, "Queue empty, waiting grace period before EOS");
        g_usleep (10000);  /* 10ms */
        return;
      }
      
      /* Only send EOS after 100ms of empty queue */
      if ((now - src->eos_wait_start) < 100000) {  /* 100ms in microseconds */
        g_usleep (10000);  /* 10ms */
        return;
      }
      
      GST_INFO_OBJECT (src, "Publishing client disconnected with no remaining tags after grace period, sending EOS");
      src->eos_wait_start = 0;  /* Reset for next time */
      gst_pad_push_event (src->srcpad, gst_event_new_eos ());
      if (src->video_pad)
        gst_pad_push_event (src->video_pad, gst_event_new_eos ());
      if (src->audio_pad)
        gst_pad_push_event (src->audio_pad, gst_event_new_eos ());
      gst_task_pause (src->task);
    } else {
      src->eos_wait_start = 0;  /* Reset if we're still waiting for more */
      g_usleep (5000);  /* 5ms */
    }
    return;
  }
  
  /* Got a tag, reset EOS wait timer */
  src->eos_wait_start = 0;
  
  /* Route tag to appropriate pad */
  GST_INFO_OBJECT (src, "Loop: dequeued tag type=%d ts=%u", tag->tag_type, tag->timestamp);
  
  /* Always push to srcpad (raw FLV output) */
  ret = push_to_srcpad (src, tag);
  
  /* Also push to typed pads if someone is listening */
  if (tag->tag_type == RTMP2_FLV_TAG_VIDEO) {
    push_video_buffer (src, tag);
  } else if (tag->tag_type == RTMP2_FLV_TAG_AUDIO) {
    push_audio_buffer (src, tag);
  }
  
  rtmp2_flv_tag_free (tag);
  
  /* Handle flow errors */
  if (ret != GST_FLOW_OK && ret != GST_FLOW_FLUSHING) {
    GST_ERROR_OBJECT (src, "Flow error: %s, pausing task", gst_flow_get_name (ret));
    gst_task_pause (src->task);
  }
}

static GstStateChangeReturn
gst_rtmp2_server_src_change_state (GstElement *element, GstStateChange transition)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
  GSocketAddress *address;
  GInetAddress *inet_address;
  GError *error = NULL;
      gboolean bind_ret;
      
      GST_INFO_OBJECT (src, "NULL→READY: Starting server on %s:%u", src->host, src->port);

      /* Use default main context */
  src->context = g_main_context_default ();

      /* Create socket */
  src->server_socket = g_socket_new (G_SOCKET_FAMILY_IPV4,
      G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
  if (!src->server_socket) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Failed to create socket: %s", error->message), (NULL));
    g_error_free (error);
        return GST_STATE_CHANGE_FAILURE;
  }

  g_socket_set_blocking (src->server_socket, FALSE);

      /* Bind to address */
  inet_address = g_inet_address_new_from_string (src->host);
  if (!inet_address) {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        ("Invalid host address: %s", src->host), (NULL));
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
        return GST_STATE_CHANGE_FAILURE;
  }

  address = g_inet_socket_address_new (inet_address, src->port);
  g_object_unref (inet_address);

      bind_ret = g_socket_bind (src->server_socket, address, TRUE, &error);
  g_object_unref (address);

      if (!bind_ret) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
            ("Failed to bind to %s:%u: %s", src->host, src->port, error->message), (NULL));
    g_error_free (error);
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
        return GST_STATE_CHANGE_FAILURE;
  }

      /* Listen */
      bind_ret = g_socket_listen (src->server_socket, &error);
      if (!bind_ret) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Failed to listen: %s", error->message), (NULL));
    g_error_free (error);
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
        return GST_STATE_CHANGE_FAILURE;
  }

      /* Create source for accept */
      src->server_source = g_socket_create_source (src->server_socket, G_IO_IN, NULL);
  g_source_set_callback (src->server_source,
      (GSourceFunc) server_accept_cb, src, NULL);
  g_source_attach (src->server_source, src->context);

      /* Start event loop thread */
  src->running = TRUE;
  src->event_thread = g_thread_new ("rtmp-event-loop", 
      event_loop_thread_func, src);
  
  if (!src->event_thread) {
    GST_ERROR_OBJECT (src, "Failed to create event loop thread");
        g_source_destroy (src->server_source);
        g_source_unref (src->server_source);
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
        return GST_STATE_CHANGE_FAILURE;
  }

      GST_INFO_OBJECT (src, "Server listening on %s:%u", src->host, src->port);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_INFO_OBJECT (src, "READY→PAUSED: Starting task");
      gst_task_start (src->task);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_INFO_OBJECT (src, "PAUSED→READY: Stopping task");
      if (src->task) {
        gst_task_stop (src->task);
        g_rec_mutex_lock (&src->task_lock);
        g_rec_mutex_unlock (&src->task_lock);
        gst_task_join (src->task);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
  GList *l;

      GST_INFO_OBJECT (src, "READY→NULL: Cleaning up");
      
      /* Stop event loop */
  if (src->event_thread) {
    src->running = FALSE;
        if (src->context)
      g_main_context_wakeup (src->context);
    g_thread_join (src->event_thread);
    src->event_thread = NULL;
  }

      /* Cleanup server source */
  if (src->server_source) {
    g_source_destroy (src->server_source);
    g_source_unref (src->server_source);
    src->server_source = NULL;
  }

      /* Cleanup server socket */
  if (src->server_socket) {
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
  }

      /* Cleanup clients */
  g_mutex_lock (&src->clients_lock);
  for (l = src->clients; l; l = l->next) {
    Rtmp2Client *client = (Rtmp2Client *) l->data;
    rtmp2_client_free (client);
  }
  g_list_free (src->clients);
  src->clients = NULL;
  src->active_client = NULL;
  g_mutex_unlock (&src->clients_lock);

      /* Remove pads */
      g_mutex_lock (&src->pad_lock);
      if (src->video_pad) {
        gst_element_remove_pad (GST_ELEMENT (src), src->video_pad);
        src->video_pad = NULL;
    }
      if (src->audio_pad) {
        gst_element_remove_pad (GST_ELEMENT (src), src->audio_pad);
        src->audio_pad = NULL;
      }
      src->have_video = FALSE;
      src->have_audio = FALSE;
      g_mutex_unlock (&src->pad_lock);
      
      break;
    }
    default:
            break;
          }
          
  return ret;
}
