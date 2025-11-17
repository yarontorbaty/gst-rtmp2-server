/*
 * GStreamer
 * Copyright (C) 2024 Your Name <your.email@example.com>
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

#ifndef __RTMP2_ENHANCED_H__
#define __RTMP2_ENHANCED_H__

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/* Enhanced RTMP Capabilities (capsEx) */
#define RTMP2_CAPS_RECONNECT           0x01
#define RTMP2_CAPS_MULTITRACK          0x02
#define RTMP2_CAPS_MODEX               0x04
#define RTMP2_CAPS_TIMESTAMP_NANO_OFFSET 0x08

/* Enhanced Video Codecs (FourCC) */
#define RTMP2_FOURCC_H264  "H264"
#define RTMP2_FOURCC_H265  "H265"
#define RTMP2_FOURCC_VP9   "VP9 "
#define RTMP2_FOURCC_AV1   "AV01"

/* Enhanced Audio Codecs */
#define RTMP2_AUDIO_CODEC_AAC     10
#define RTMP2_AUDIO_CODEC_MP3     2
#define RTMP2_AUDIO_CODEC_OPUS    13
#define RTMP2_AUDIO_CODEC_G711A   7
#define RTMP2_AUDIO_CODEC_G711U   8

typedef struct {
  guint8 caps_ex;
  GHashTable *video_fourcc_info_map;
  gboolean supports_amf3;
  gboolean supports_reconnect;
  gboolean supports_multitrack;
  gboolean supports_timestamp_nano_offset;
} Rtmp2EnhancedCapabilities;

typedef struct {
  gchar *fourcc;
  guint8 codec_id;
  gchar *description;
} Rtmp2VideoFourCcInfo;

Rtmp2EnhancedCapabilities *rtmp2_enhanced_capabilities_new (void);
void rtmp2_enhanced_capabilities_free (Rtmp2EnhancedCapabilities * caps);
gboolean rtmp2_enhanced_parse_connect (const guint8 * data, gsize size,
    Rtmp2EnhancedCapabilities * client_caps, gdouble * transaction_id, GError ** error);
gboolean rtmp2_enhanced_send_connect_result (GByteArray * ba,
    Rtmp2EnhancedCapabilities * server_caps, gdouble transaction_id, GError ** error);

G_END_DECLS

#endif /* __RTMP2_ENHANCED_H__ */

