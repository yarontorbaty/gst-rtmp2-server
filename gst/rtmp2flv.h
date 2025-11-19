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

#ifndef __RTMP2_FLV_H__
#define __RTMP2_FLV_H__

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define RTMP2_FLV_TAG_HEADER_SIZE 11

typedef enum {
  RTMP2_FLV_TAG_AUDIO = 8,
  RTMP2_FLV_TAG_VIDEO = 9,
  RTMP2_FLV_TAG_SCRIPT = 18
} Rtmp2FlvTagType;

typedef enum {
  RTMP2_FLV_VIDEO_CODEC_H263 = 2,
  RTMP2_FLV_VIDEO_CODEC_SCREEN = 3,
  RTMP2_FLV_VIDEO_CODEC_VP6 = 4,
  RTMP2_FLV_VIDEO_CODEC_VP6A = 5,
  RTMP2_FLV_VIDEO_CODEC_SCREEN2 = 6,
  RTMP2_FLV_VIDEO_CODEC_H264 = 7,
  RTMP2_FLV_VIDEO_CODEC_H265 = 12,
  RTMP2_FLV_VIDEO_CODEC_VP9 = 13,
  RTMP2_FLV_VIDEO_CODEC_AV1 = 14
} Rtmp2FlvVideoCodec;

typedef enum {
  RTMP2_FLV_AUDIO_CODEC_PCM = 0,
  RTMP2_FLV_AUDIO_CODEC_ADPCM = 1,
  RTMP2_FLV_AUDIO_CODEC_MP3 = 2,
  RTMP2_FLV_AUDIO_CODEC_PCM_LE = 3,
  RTMP2_FLV_AUDIO_CODEC_NELLY = 4,
  RTMP2_FLV_AUDIO_CODEC_NELLY_16 = 5,
  RTMP2_FLV_AUDIO_CODEC_NELLY_8 = 6,
  RTMP2_FLV_AUDIO_CODEC_G711A = 7,
  RTMP2_FLV_AUDIO_CODEC_G711U = 8,
  RTMP2_FLV_AUDIO_CODEC_RESERVED = 9,
  RTMP2_FLV_AUDIO_CODEC_AAC = 10,
  RTMP2_FLV_AUDIO_CODEC_SPEEX = 11,
  RTMP2_FLV_AUDIO_CODEC_OPUS = 13,
  RTMP2_FLV_AUDIO_CODEC_MP3_8 = 14,
  RTMP2_FLV_AUDIO_CODEC_DEVICE = 15
} Rtmp2FlvAudioCodec;

typedef struct {
  Rtmp2FlvTagType tag_type;
  guint32 data_size;
  guint32 timestamp;
  guint32 stream_id;
  
  /* Video specific */
  Rtmp2FlvVideoCodec video_codec;
  gboolean video_keyframe;
  
  /* Audio specific */
  Rtmp2FlvAudioCodec audio_codec;
  guint audio_sample_rate;
  guint audio_sample_size;
  guint audio_channels;
  
  GstBuffer *data;
} Rtmp2FlvTag;

typedef struct {
  GList *pending_tags;
  gboolean have_video_caps;
  gboolean have_audio_caps;
  GMutex pending_tags_lock;
} Rtmp2FlvParser;

void rtmp2_flv_parser_init (Rtmp2FlvParser *parser);
void rtmp2_flv_parser_clear (Rtmp2FlvParser *parser);
gboolean rtmp2_flv_parser_process (Rtmp2FlvParser *parser, const guint8 *data, gsize size,
                                   GList **tags, GError **error);
Rtmp2FlvTag *rtmp2_flv_tag_new (void);
void rtmp2_flv_tag_free (Rtmp2FlvTag *tag);
GstCaps *rtmp2_flv_tag_get_caps (Rtmp2FlvTag *tag);

G_END_DECLS

#endif /* __RTMP2_FLV_H__ */

