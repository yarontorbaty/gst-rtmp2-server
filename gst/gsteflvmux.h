/* GStreamer Enhanced FLV Muxer with HEVC support
 * Copyright (C) 2026 Yaron Torbaty <yarontorbaty@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef __GST_EFLVMUX_H__
#define __GST_EFLVMUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_EFLVMUX (gst_eflvmux_get_type())

G_DECLARE_FINAL_TYPE (GstEFlvMux, gst_eflvmux, GST, EFLVMUX, GstElement)

typedef enum {
  EFLV_VIDEO_CODEC_H264 = 0,
  EFLV_VIDEO_CODEC_HEVC = 1,
} EFlvVideoCodec;

struct _GstEFlvMux {
  GstElement parent;

  GstPad *srcpad;
  GstPad *video_sinkpad;
  GstPad *audio_sinkpad;

  GMutex lock;

  EFlvVideoCodec video_codec;
  gboolean have_video;
  gboolean have_audio;
  gboolean header_sent;
  gboolean video_seq_header_sent;
  gboolean audio_seq_header_sent;

  GstBuffer *video_codec_data;
  GstBuffer *audio_codec_data;

  gint64 base_wall_time_us;

  guint32 video_width;
  guint32 video_height;
  gdouble video_framerate;
  guint32 audio_sample_rate;
  guint32 audio_channels;
};

G_END_DECLS

#endif /* __GST_EFLVMUX_H__ */
