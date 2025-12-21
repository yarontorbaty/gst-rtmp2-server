/*
 * GStreamer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtmp2flv.h"
#include <string.h>

static guint8
read_uint8 (const guint8 ** data, gsize * size)
{
  guint8 val;
  if (*size < 1)
    return 0;
  val = (*data)[0];
  (*data)++;
  (*size)--;
  return val;
}

static guint32
read_uint24_be (const guint8 ** data, gsize * size)
{
  guint32 val = 0;
  if (*size < 3)
    return 0;
  val = ((*data)[0] << 16) | ((*data)[1] << 8) | (*data)[2];
  *data += 3;
  *size -= 3;
  return val;
}

static guint32
read_uint32_be (const guint8 ** data, gsize * size)
{
  guint32 val = 0;
  if (*size < 4)
    return 0;
  val = ((*data)[0] << 24) | ((*data)[1] << 16) | ((*data)[2] << 8) | (*data)[3];
  *data += 4;
  *size -= 4;
  return val;
}

void
rtmp2_flv_parser_init (Rtmp2FlvParser * parser)
{
  memset (parser, 0, sizeof (Rtmp2FlvParser));
  parser->pending_tags = NULL;
  parser->have_video_caps = FALSE;
  parser->have_audio_caps = FALSE;
  g_mutex_init (&parser->pending_tags_lock);
}

void
rtmp2_flv_parser_clear (Rtmp2FlvParser * parser)
{
  g_mutex_lock (&parser->pending_tags_lock);
  if (parser->pending_tags) {
    g_list_free_full (parser->pending_tags, (GDestroyNotify) rtmp2_flv_tag_free);
    parser->pending_tags = NULL;
  }
  g_mutex_unlock (&parser->pending_tags_lock);
  g_mutex_clear (&parser->pending_tags_lock);
}

gboolean
rtmp2_flv_parser_process (Rtmp2FlvParser * parser, const guint8 * data,
    gsize size, GList ** tags, GError ** error)
{
  const guint8 *ptr = data;
  gsize remaining = size;
  Rtmp2FlvTag *tag;
  guint8 tag_type_byte;
  guint8 codec_info;
  guint32 data_size;
  guint32 timestamp;
  guint32 stream_id;

  *tags = NULL;

  while (remaining >= RTMP2_FLV_TAG_HEADER_SIZE) {
    tag = rtmp2_flv_tag_new ();

    tag_type_byte = read_uint8 (&ptr, &remaining);
    tag->tag_type = (Rtmp2FlvTagType) (tag_type_byte & 0x1f);
    data_size = read_uint24_be (&ptr, &remaining);
    timestamp = read_uint24_be (&ptr, &remaining);
    stream_id = read_uint32_be (&ptr, &remaining);

    tag->data_size = data_size;
    tag->timestamp = timestamp;
    tag->stream_id = stream_id;

    if (remaining < data_size) {
      rtmp2_flv_tag_free (tag);
      g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
          "Not enough data for FLV tag");
      return FALSE;
    }

    if (tag->tag_type == RTMP2_FLV_TAG_VIDEO) {
      if (data_size < 1) {
        rtmp2_flv_tag_free (tag);
        ptr += data_size;
        remaining -= data_size;
        continue;
      }

      codec_info = read_uint8 (&ptr, &remaining);
      tag->video_codec = (Rtmp2FlvVideoCodec) (codec_info & 0x0f);
      tag->video_keyframe = ((codec_info >> 4) & 0x0f) == 1;

      /* Enhanced RTMP: Check for extended codec ID (12-15) */
      if (tag->video_codec == 12) {
        if (remaining >= 1) {
          guint8 ext_codec = read_uint8 (&ptr, &remaining);
          if (ext_codec == 0) {
            tag->video_codec = RTMP2_FLV_VIDEO_CODEC_H265;
          } else if (ext_codec == 1) {
            tag->video_codec = RTMP2_FLV_VIDEO_CODEC_VP9;
          } else if (ext_codec == 2) {
            tag->video_codec = RTMP2_FLV_VIDEO_CODEC_AV1;
          }
        }
      }

      tag->data = gst_buffer_new_allocate (NULL, data_size - 1, NULL);
      GstMapInfo map;
      if (gst_buffer_map (tag->data, &map, GST_MAP_WRITE)) {
        memcpy (map.data, ptr, data_size - 1);
        gst_buffer_unmap (tag->data, &map);
      }
      ptr += data_size - 1;
      remaining -= data_size - 1;

    } else if (tag->tag_type == RTMP2_FLV_TAG_AUDIO) {
      if (data_size < 1) {
        rtmp2_flv_tag_free (tag);
        ptr += data_size;
        remaining -= data_size;
        continue;
      }

      codec_info = read_uint8 (&ptr, &remaining);
      tag->audio_codec = (Rtmp2FlvAudioCodec) ((codec_info >> 4) & 0x0f);
      tag->audio_sample_rate = ((codec_info >> 2) & 0x03);
      tag->audio_sample_size = ((codec_info >> 1) & 0x01);
      tag->audio_channels = (codec_info & 0x01);

      /* Enhanced RTMP: Check for extended audio codec (13 = Opus) */
      if (tag->audio_codec == 13) {
        tag->audio_codec = RTMP2_FLV_AUDIO_CODEC_OPUS;
      }

      tag->data = gst_buffer_new_allocate (NULL, data_size - 1, NULL);
      GstMapInfo map;
      if (gst_buffer_map (tag->data, &map, GST_MAP_WRITE)) {
        memcpy (map.data, ptr, data_size - 1);
        gst_buffer_unmap (tag->data, &map);
      }
      ptr += data_size - 1;
      remaining -= data_size - 1;
    } else {
      tag->data = gst_buffer_new_allocate (NULL, data_size, NULL);
      GstMapInfo map;
      if (gst_buffer_map (tag->data, &map, GST_MAP_WRITE)) {
        memcpy (map.data, ptr, data_size);
        gst_buffer_unmap (tag->data, &map);
      }
      ptr += data_size;
      remaining -= data_size;
    }

    *tags = g_list_append (*tags, tag);
  }

  return TRUE;
}

Rtmp2FlvTag *
rtmp2_flv_tag_new (void)
{
  Rtmp2FlvTag *tag = g_new0 (Rtmp2FlvTag, 1);
  return tag;
}

void
rtmp2_flv_tag_free (Rtmp2FlvTag * tag)
{
  if (!tag)
    return;

  if (tag->data)
    gst_buffer_unref (tag->data);

  g_free (tag);
}

GstCaps *
rtmp2_flv_tag_get_caps (Rtmp2FlvTag * tag)
{
  if (tag->tag_type == RTMP2_FLV_TAG_VIDEO) {
    switch (tag->video_codec) {
      case RTMP2_FLV_VIDEO_CODEC_H264:
        return gst_caps_new_simple ("video/x-h264",
            "stream-format", G_TYPE_STRING, "avc",
            "alignment", G_TYPE_STRING, "au",
            NULL);
      case RTMP2_FLV_VIDEO_CODEC_H265:
        return gst_caps_new_simple ("video/x-h265",
            "stream-format", G_TYPE_STRING, "hev1",
            "alignment", G_TYPE_STRING, "au",
            NULL);
      case RTMP2_FLV_VIDEO_CODEC_VP9:
        return gst_caps_new_simple ("video/x-vp9",
            "profile", G_TYPE_STRING, "0",
            NULL);
      case RTMP2_FLV_VIDEO_CODEC_AV1:
        return gst_caps_new_simple ("video/x-av1",
            "stream-format", G_TYPE_STRING, "obu-stream",
            NULL);
      default:
        return NULL;
    }
  } else if (tag->tag_type == RTMP2_FLV_TAG_AUDIO) {
    switch (tag->audio_codec) {
      case RTMP2_FLV_AUDIO_CODEC_AAC:
        return gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "stream-format", G_TYPE_STRING, "raw",
            NULL);
      case RTMP2_FLV_AUDIO_CODEC_MP3:
        return gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1,
            "layer", G_TYPE_INT, 3,
            NULL);
      case RTMP2_FLV_AUDIO_CODEC_OPUS:
        return gst_caps_new_simple ("audio/x-opus",
            "streamheader", GST_TYPE_BUFFER, NULL,
            NULL);
      default:
        return NULL;
    }
  }

  return NULL;
}

