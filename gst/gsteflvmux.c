/* GStreamer Enhanced FLV Muxer with HEVC support
 * Copyright (C) 2026 Yaron Torbaty <yarontorbaty@gmail.com>
 *
 * Generates Enhanced FLV tags with FourCC identifiers for HEVC,
 * and standard FLV tags for H.264.  Accepts AAC audio.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsteflvmux.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_eflvmux_debug);
#define GST_CAT_DEFAULT gst_eflvmux_debug

/* ========== Pad Templates ========== */

static GstStaticPadTemplate video_sink_template =
    GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS (
        "video/x-h264, stream-format=avc, alignment=au; "
        "video/x-h265, stream-format={hvc1,hev1}, alignment=au"));

static GstStaticPadTemplate audio_sink_template =
    GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS (
        "audio/mpeg, mpegversion=4, stream-format=raw"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-flv"));

G_DEFINE_TYPE (GstEFlvMux, gst_eflvmux, GST_TYPE_ELEMENT);

static void gst_eflvmux_finalize (GObject * object);
static GstStateChangeReturn gst_eflvmux_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_eflvmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_eflvmux_release_pad (GstElement * element, GstPad * pad);
static GstFlowReturn gst_eflvmux_video_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstFlowReturn gst_eflvmux_audio_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_eflvmux_video_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_eflvmux_audio_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

/* ========== Helpers ========== */

static guint32
get_timestamp_ms (GstEFlvMux * mux)
{
  gint64 now_us = g_get_monotonic_time ();
  if (mux->base_wall_time_us == 0)
    mux->base_wall_time_us = now_us;
  gint64 elapsed = now_us - mux->base_wall_time_us;
  if (elapsed < 0)
    elapsed = 0;
  return (guint32) (elapsed / 1000);
}

static void
write_be24 (guint8 * p, guint32 val)
{
  p[0] = (val >> 16) & 0xFF;
  p[1] = (val >> 8) & 0xFF;
  p[2] = val & 0xFF;
}

static void
write_be32 (guint8 * p, guint32 val)
{
  p[0] = (val >> 24) & 0xFF;
  p[1] = (val >> 16) & 0xFF;
  p[2] = (val >> 8) & 0xFF;
  p[3] = val & 0xFF;
}

/*
 * Build an FLV tag and push it downstream.
 *
 * tag_type: 8=audio, 9=video, 18=script
 * timestamp_ms: FLV timestamp
 * tag_data/tag_data_size: the tag body (after the 11-byte FLV tag header)
 */
static GstFlowReturn
push_flv_tag (GstEFlvMux * mux, guint8 tag_type, guint32 timestamp_ms,
    const guint8 * tag_data, guint32 tag_data_size)
{
  guint32 total = 11 + tag_data_size + 4;
  GstBuffer *buf = gst_buffer_new_allocate (NULL, total, NULL);
  GstMapInfo map;

  gst_buffer_map (buf, &map, GST_MAP_WRITE);

  /* 11-byte FLV tag header */
  map.data[0] = tag_type;
  write_be24 (map.data + 1, tag_data_size);
  write_be24 (map.data + 4, timestamp_ms & 0xFFFFFF);
  map.data[7] = (timestamp_ms >> 24) & 0xFF;
  map.data[8] = 0;
  map.data[9] = 0;
  map.data[10] = 0;

  memcpy (map.data + 11, tag_data, tag_data_size);

  /* PreviousTagSize */
  write_be32 (map.data + 11 + tag_data_size, 11 + tag_data_size);

  gst_buffer_unmap (buf, &map);

  GST_BUFFER_DTS (buf) = (GstClockTime) timestamp_ms * GST_MSECOND;
  GST_BUFFER_PTS (buf) = GST_BUFFER_DTS (buf);

  return gst_pad_push (mux->srcpad, buf);
}

/* ========== FLV File Header + Metadata ========== */

/* AMF0 helper: write a string (type 0x02) */
static guint32
amf0_write_string (guint8 * p, const gchar * str)
{
  guint16 len = (guint16) strlen (str);
  p[0] = 0x02;
  p[1] = (len >> 8) & 0xFF;
  p[2] = len & 0xFF;
  memcpy (p + 3, str, len);
  return 3 + len;
}

/* AMF0 helper: write a number (type 0x00) */
static guint32
amf0_write_number (guint8 * p, const gchar * key, gdouble val)
{
  guint16 klen = (guint16) strlen (key);
  guint32 off = 0;
  /* key (AMF0 object property) */
  p[off++] = (klen >> 8) & 0xFF;
  p[off++] = klen & 0xFF;
  memcpy (p + off, key, klen);
  off += klen;
  /* number value */
  p[off++] = 0x00;
  union { gdouble d; guint64 u; } u;
  u.d = val;
  guint64 be = GUINT64_TO_BE (u.u);
  memcpy (p + off, &be, 8);
  off += 8;
  return off;
}

/* AMF0 helper: write a boolean */
static guint32
amf0_write_bool (guint8 * p, const gchar * key, gboolean val)
{
  guint16 klen = (guint16) strlen (key);
  guint32 off = 0;
  p[off++] = (klen >> 8) & 0xFF;
  p[off++] = klen & 0xFF;
  memcpy (p + off, key, klen);
  off += klen;
  p[off++] = 0x01;
  p[off++] = val ? 1 : 0;
  return off;
}

/* AMF0 helper: write a string property */
static guint32
amf0_write_string_prop (guint8 * p, const gchar * key, const gchar * val)
{
  guint16 klen = (guint16) strlen (key);
  guint16 vlen = (guint16) strlen (val);
  guint32 off = 0;
  p[off++] = (klen >> 8) & 0xFF;
  p[off++] = klen & 0xFF;
  memcpy (p + off, key, klen);
  off += klen;
  p[off++] = 0x02;
  p[off++] = (vlen >> 8) & 0xFF;
  p[off++] = vlen & 0xFF;
  memcpy (p + off, val, vlen);
  off += vlen;
  return off;
}

static GstFlowReturn
push_flv_header_and_metadata (GstEFlvMux * mux)
{
  GstFlowReturn ret;

  /* Send required srcpad events before the first buffer push.
   * Without these, gst_pad_push returns GST_FLOW_ERROR. */
  gst_pad_push_event (mux->srcpad,
      gst_event_new_stream_start ("eflvmux-stream"));

  GstCaps *flv_caps = gst_caps_new_empty_simple ("video/x-flv");
  gst_pad_push_event (mux->srcpad, gst_event_new_caps (flv_caps));
  gst_caps_unref (flv_caps);

  GstSegment segment;
  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (mux->srcpad, gst_event_new_segment (&segment));

  /* FLV file header (9 bytes) + PreviousTagSize0 (4 bytes) */
  guint8 header[13] = {
    'F', 'L', 'V', 0x01,
    0x05,                       /* audio + video */
    0x00, 0x00, 0x00, 0x09,     /* header size = 9 */
    0x00, 0x00, 0x00, 0x00      /* PreviousTagSize0 = 0 */
  };
  GstBuffer *hdr_buf = gst_buffer_new_allocate (NULL, 13, NULL);
  gst_buffer_fill (hdr_buf, 0, header, 13);
  GST_BUFFER_DTS (hdr_buf) = 0;
  GST_BUFFER_PTS (hdr_buf) = 0;
  ret = gst_pad_push (mux->srcpad, hdr_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  /* Build onMetaData script tag */
  guint8 meta[512];
  guint32 off = 0;

  /* @setDataFrame string (required by YouTube) */
  off += amf0_write_string (meta + off, "@setDataFrame");

  /* onMetaData string */
  off += amf0_write_string (meta + off, "onMetaData");

  /* ECMA array */
  meta[off++] = 0x08;           /* AMF0 ECMA array */
  write_be32 (meta + off, 0);
  off += 4;                     /* approximate count (0 = unknown) */

  off += amf0_write_number (meta + off, "duration", 0.0);
  off += amf0_write_number (meta + off, "width",
      (gdouble) mux->video_width);
  off += amf0_write_number (meta + off, "height",
      (gdouble) mux->video_height);
  off += amf0_write_number (meta + off, "framerate", mux->video_framerate);
  off += amf0_write_number (meta + off, "videoframerate",
      mux->video_framerate);

  if (mux->video_codec == EFLV_VIDEO_CODEC_HEVC) {
    off += amf0_write_number (meta + off, "videocodecid", 12.0);
    off += amf0_write_string_prop (meta + off, "videocodecid_fourcc", "hvc1");
  } else {
    off += amf0_write_number (meta + off, "videocodecid", 7.0);
  }

  off += amf0_write_number (meta + off, "audiocodecid", 10.0);
  off += amf0_write_number (meta + off, "audiosamplerate",
      (gdouble) mux->audio_sample_rate);
  off += amf0_write_number (meta + off, "audiosamplesize", 16.0);
  off += amf0_write_bool (meta + off, "stereo", mux->audio_channels >= 2);
  off += amf0_write_string_prop (meta + off, "encoder", "OrbiBond");

  /* Object end marker */
  meta[off++] = 0x00;
  meta[off++] = 0x00;
  meta[off++] = 0x09;

  return push_flv_tag (mux, 18, 0, meta, off);
}

/* ========== Video Tags ========== */

static GstFlowReturn
push_video_sequence_header (GstEFlvMux * mux, guint32 ts)
{
  GstMapInfo cd_map;
  if (!mux->video_codec_data)
    return GST_FLOW_OK;

  gst_buffer_map (mux->video_codec_data, &cd_map, GST_MAP_READ);

  if (mux->video_codec == EFLV_VIDEO_CODEC_HEVC) {
    /* Enhanced RTMP: 0x90 + "hvc1" + hvcC */
    guint32 sz = 5 + cd_map.size;
    guint8 *data = g_malloc (sz);
    data[0] = 0x90;            /* enhanced(0x80) | keyframe(0x10) | seq_header(0x00) */
    data[1] = 'h'; data[2] = 'v'; data[3] = 'c'; data[4] = '1';
    memcpy (data + 5, cd_map.data, cd_map.size);

    gst_buffer_unmap (mux->video_codec_data, &cd_map);
    GstFlowReturn ret = push_flv_tag (mux, 9, ts, data, sz);
    g_free (data);
    return ret;
  } else {
    /* Standard H.264: 0x17 0x00 CTS(3)=0 + avcC */
    guint32 sz = 5 + cd_map.size;
    guint8 *data = g_malloc (sz);
    data[0] = 0x17;            /* keyframe(1) | AVC(7) */
    data[1] = 0x00;            /* AVC sequence header */
    data[2] = 0; data[3] = 0; data[4] = 0;     /* CTS = 0 */
    memcpy (data + 5, cd_map.data, cd_map.size);

    gst_buffer_unmap (mux->video_codec_data, &cd_map);
    GstFlowReturn ret = push_flv_tag (mux, 9, ts, data, sz);
    g_free (data);
    return ret;
  }
}

static GstFlowReturn
gst_eflvmux_video_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstEFlvMux *mux = GST_EFLVMUX (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  guint32 ts;
  gboolean is_keyframe;

  g_mutex_lock (&mux->lock);

  ts = get_timestamp_ms (mux);

  if (!mux->header_sent) {
    ret = push_flv_header_and_metadata (mux);
    if (ret != GST_FLOW_OK)
      goto out;
    mux->header_sent = TRUE;
  }

  /* Check for keyframe via buffer flags */
  is_keyframe = !GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

  /* Send sequence header before the first frame (and on each keyframe) */
  if (mux->video_codec_data &&
      (is_keyframe || !mux->video_seq_header_sent)) {
    ret = push_video_sequence_header (mux, ts);
    if (ret != GST_FLOW_OK)
      goto out;
    mux->video_seq_header_sent = TRUE;
  }

  gst_buffer_map (buf, &map, GST_MAP_READ);

  if (mux->video_codec == EFLV_VIDEO_CODEC_HEVC) {
    /* Enhanced RTMP video frame: header(1) + FourCC(4) + CTS(3) + NALUs */
    guint32 sz = 8 + map.size;
    guint8 *data = g_malloc (sz);

    if (is_keyframe)
      data[0] = 0x91;          /* enhanced | keyframe(1) | coded_frames(1) */
    else
      data[0] = 0xA1;          /* enhanced | inter-frame(2) | coded_frames(1) */
    data[1] = 'h'; data[2] = 'v'; data[3] = 'c'; data[4] = '1';
    data[5] = 0; data[6] = 0; data[7] = 0;     /* CTS = 0 */
    memcpy (data + 8, map.data, map.size);

    gst_buffer_unmap (buf, &map);
    ret = push_flv_tag (mux, 9, ts, data, sz);
    g_free (data);
  } else {
    /* Standard H.264 video frame */
    guint32 sz = 5 + map.size;
    guint8 *data = g_malloc (sz);

    data[0] = is_keyframe ? 0x17 : 0x27;       /* keyframe/inter + AVC(7) */
    data[1] = 0x01;            /* AVC NALU */
    data[2] = 0; data[3] = 0; data[4] = 0;     /* CTS = 0 */
    memcpy (data + 5, map.data, map.size);

    gst_buffer_unmap (buf, &map);
    ret = push_flv_tag (mux, 9, ts, data, sz);
    g_free (data);
  }

out:
  g_mutex_unlock (&mux->lock);
  gst_buffer_unref (buf);
  return ret;
}

/* ========== Audio Tags ========== */

static GstFlowReturn
push_audio_sequence_header (GstEFlvMux * mux, guint32 ts)
{
  GstMapInfo cd_map;
  if (!mux->audio_codec_data)
    return GST_FLOW_OK;

  gst_buffer_map (mux->audio_codec_data, &cd_map, GST_MAP_READ);

  guint32 sz = 2 + cd_map.size;
  guint8 *data = g_malloc (sz);
  data[0] = 0xAF;              /* AAC, 44100, 16-bit, stereo */
  data[1] = 0x00;              /* AAC sequence header */
  memcpy (data + 2, cd_map.data, cd_map.size);

  gst_buffer_unmap (mux->audio_codec_data, &cd_map);
  GstFlowReturn ret = push_flv_tag (mux, 8, ts, data, sz);
  g_free (data);
  return ret;
}

static GstFlowReturn
gst_eflvmux_audio_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstEFlvMux *mux = GST_EFLVMUX (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  guint32 ts;

  g_mutex_lock (&mux->lock);

  ts = get_timestamp_ms (mux);

  if (!mux->header_sent) {
    ret = push_flv_header_and_metadata (mux);
    if (ret != GST_FLOW_OK)
      goto out;
    mux->header_sent = TRUE;
  }

  if (mux->audio_codec_data && !mux->audio_seq_header_sent) {
    ret = push_audio_sequence_header (mux, ts);
    if (ret != GST_FLOW_OK)
      goto out;
    mux->audio_seq_header_sent = TRUE;
  }

  gst_buffer_map (buf, &map, GST_MAP_READ);

  guint32 sz = 2 + map.size;
  guint8 *data = g_malloc (sz);
  data[0] = 0xAF;              /* AAC */
  data[1] = 0x01;              /* AAC raw */
  memcpy (data + 2, map.data, map.size);

  gst_buffer_unmap (buf, &map);
  ret = push_flv_tag (mux, 8, ts, data, sz);
  g_free (data);

out:
  g_mutex_unlock (&mux->lock);
  gst_buffer_unref (buf);
  return ret;
}

/* ========== Pad Events ========== */

static gboolean
gst_eflvmux_video_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstEFlvMux *mux = GST_EFLVMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      GstStructure *s = gst_caps_get_structure (caps, 0);

      g_mutex_lock (&mux->lock);

      if (gst_structure_has_name (s, "video/x-h265")) {
        mux->video_codec = EFLV_VIDEO_CODEC_HEVC;
        GST_INFO_OBJECT (mux, "Video codec: HEVC");
      } else {
        mux->video_codec = EFLV_VIDEO_CODEC_H264;
        GST_INFO_OBJECT (mux, "Video codec: H.264");
      }

      gst_structure_get_int (s, "width", (gint *) & mux->video_width);
      gst_structure_get_int (s, "height", (gint *) & mux->video_height);

      gint fps_n = 0, fps_d = 1;
      gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);
      if (fps_d > 0)
        mux->video_framerate = (gdouble) fps_n / fps_d;

      /* Extract codec_data for sequence header */
      const GValue *cd = gst_structure_get_value (s, "codec_data");
      if (cd && G_VALUE_HOLDS (cd, GST_TYPE_BUFFER)) {
        gst_buffer_replace (&mux->video_codec_data,
            gst_value_get_buffer (cd));
        GST_INFO_OBJECT (mux,
            "Got video codec_data (%zu bytes)",
            gst_buffer_get_size (mux->video_codec_data));
      }

      mux->have_video = TRUE;
      mux->video_seq_header_sent = FALSE;
      g_mutex_unlock (&mux->lock);

      gst_event_unref (event);
      return TRUE;
    }
    default:
      return gst_pad_event_default (pad, parent, event);
  }
}

static gboolean
gst_eflvmux_audio_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstEFlvMux *mux = GST_EFLVMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      GstStructure *s = gst_caps_get_structure (caps, 0);

      g_mutex_lock (&mux->lock);

      gst_structure_get_int (s, "rate", (gint *) & mux->audio_sample_rate);
      gst_structure_get_int (s, "channels", (gint *) & mux->audio_channels);

      const GValue *cd = gst_structure_get_value (s, "codec_data");
      if (cd && G_VALUE_HOLDS (cd, GST_TYPE_BUFFER)) {
        gst_buffer_replace (&mux->audio_codec_data,
            gst_value_get_buffer (cd));
        GST_INFO_OBJECT (mux,
            "Got audio codec_data (%zu bytes)",
            gst_buffer_get_size (mux->audio_codec_data));
      }

      mux->have_audio = TRUE;
      mux->audio_seq_header_sent = FALSE;
      g_mutex_unlock (&mux->lock);

      gst_event_unref (event);
      return TRUE;
    }
    default:
      return gst_pad_event_default (pad, parent, event);
  }
}

/* ========== Element Infrastructure ========== */

static GstPad *
gst_eflvmux_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstEFlvMux *mux = GST_EFLVMUX (element);
  GstPad *pad = NULL;
  const gchar *templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE (templ);

  if (g_str_equal (templ_name, "video")) {
    if (mux->video_sinkpad)
      return NULL;
    pad = gst_pad_new_from_template (templ, "video");
    gst_pad_set_chain_function (pad, gst_eflvmux_video_chain);
    gst_pad_set_event_function (pad, gst_eflvmux_video_event);
    mux->video_sinkpad = pad;
  } else if (g_str_equal (templ_name, "audio")) {
    if (mux->audio_sinkpad)
      return NULL;
    pad = gst_pad_new_from_template (templ, "audio");
    gst_pad_set_chain_function (pad, gst_eflvmux_audio_chain);
    gst_pad_set_event_function (pad, gst_eflvmux_audio_event);
    mux->audio_sinkpad = pad;
  }

  if (pad) {
    gst_pad_set_active (pad, TRUE);
    gst_element_add_pad (element, pad);
  }

  return pad;
}

static void
gst_eflvmux_release_pad (GstElement * element, GstPad * pad)
{
  GstEFlvMux *mux = GST_EFLVMUX (element);

  if (pad == mux->video_sinkpad)
    mux->video_sinkpad = NULL;
  else if (pad == mux->audio_sinkpad)
    mux->audio_sinkpad = NULL;

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_eflvmux_change_state (GstElement * element, GstStateChange transition)
{
  GstEFlvMux *mux = GST_EFLVMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_mutex_lock (&mux->lock);
      mux->header_sent = FALSE;
      mux->video_seq_header_sent = FALSE;
      mux->audio_seq_header_sent = FALSE;
      mux->base_wall_time_us = 0;
      g_mutex_unlock (&mux->lock);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (gst_eflvmux_parent_class)->change_state (element,
      transition);
}

static void
gst_eflvmux_finalize (GObject * object)
{
  GstEFlvMux *mux = GST_EFLVMUX (object);

  gst_buffer_replace (&mux->video_codec_data, NULL);
  gst_buffer_replace (&mux->audio_codec_data, NULL);
  g_mutex_clear (&mux->lock);

  G_OBJECT_CLASS (gst_eflvmux_parent_class)->finalize (object);
}

static void
gst_eflvmux_class_init (GstEFlvMuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_eflvmux_finalize;
  element_class->change_state = gst_eflvmux_change_state;
  element_class->request_new_pad = gst_eflvmux_request_new_pad;
  element_class->release_pad = gst_eflvmux_release_pad;

  gst_element_class_set_static_metadata (element_class,
      "Enhanced FLV Muxer",
      "Codec/Muxer",
      "Muxes H.264/HEVC video and AAC audio into Enhanced FLV",
      "Yaron Torbaty <yarontorbaty@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &video_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &audio_sink_template);

  GST_DEBUG_CATEGORY_INIT (gst_eflvmux_debug, "eflvmux", 0,
      "Enhanced FLV Muxer");
}

static void
gst_eflvmux_init (GstEFlvMux * mux)
{
  mux->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_active (mux->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);

  g_mutex_init (&mux->lock);
  mux->video_codec = EFLV_VIDEO_CODEC_H264;
  mux->video_width = 1920;
  mux->video_height = 1080;
  mux->video_framerate = 30.0;
  mux->audio_sample_rate = 48000;
  mux->audio_channels = 2;
}
