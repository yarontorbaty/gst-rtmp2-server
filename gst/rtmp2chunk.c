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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtmp2chunk.h"
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

static guint16
read_uint16_be (const guint8 ** data, gsize * size)
{
  guint16 val = 0;
  if (*size < 2)
    return 0;
  val = ((*data)[0] << 8) | (*data)[1];
  *data += 2;
  *size -= 2;
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
read_uint32_le (const guint8 ** data, gsize * size)
{
  guint32 val = 0;
  if (*size < 4)
    return 0;
  val = ((*data)[3] << 24) | ((*data)[2] << 16) | ((*data)[1] << 8) | (*data)[0];
  *data += 4;
  *size -= 4;
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

static gboolean
parse_basic_header (const guint8 ** data, gsize * size, guint8 * chunk_stream_id,
    Rtmp2ChunkType * chunk_type)
{
  guint8 byte;

  if (*size < 1)
    return FALSE;

  byte = read_uint8 (data, size);
  *chunk_type = (byte >> 6) & 0x03;
  *chunk_stream_id = byte & 0x3f;

  if (*chunk_stream_id == 0) {
    if (*size < 1)
      return FALSE;
    *chunk_stream_id = 64 + read_uint8 (data, size);
  } else if (*chunk_stream_id == 1) {
    if (*size < 2)
      return FALSE;
    *chunk_stream_id = 64 + read_uint8 (data, size);
    *chunk_stream_id += (read_uint8 (data, size) << 8);
  }

  return TRUE;
}

static gboolean
parse_message_header (const guint8 ** data, gsize * size,
    Rtmp2ChunkType chunk_type, Rtmp2ChunkMessage * msg)
{
  switch (chunk_type) {
    case RTMP2_CHUNK_TYPE_0:
      if (*size < 11)
        return FALSE;
      msg->timestamp = read_uint24_be (data, size);
      msg->message_length = read_uint24_be (data, size);
      msg->message_type = read_uint8 (data, size);
      msg->message_stream_id = read_uint32_le (data, size);
      if (msg->timestamp == 0xffffff) {
        if (*size < 4)
          return FALSE;
        msg->timestamp = read_uint32_be (data, size);
      }
      break;
    case RTMP2_CHUNK_TYPE_1:
      if (*size < 7)
        return FALSE;
      msg->timestamp_delta = read_uint24_be (data, size);
      msg->message_length = read_uint24_be (data, size);
      msg->message_type = read_uint8 (data, size);
      if (msg->timestamp_delta == 0xffffff) {
        if (*size < 4)
          return FALSE;
        msg->timestamp_delta = read_uint32_be (data, size);
      }
      break;
    case RTMP2_CHUNK_TYPE_2:
      if (*size < 3)
        return FALSE;
      msg->timestamp_delta = read_uint24_be (data, size);
      if (msg->timestamp_delta == 0xffffff) {
        if (*size < 4)
          return FALSE;
        msg->timestamp_delta = read_uint32_be (data, size);
      }
      break;
    case RTMP2_CHUNK_TYPE_3:
      /* No header */
      break;
    default:
      return FALSE;
  }

  return TRUE;
}

void
rtmp2_chunk_parser_init (Rtmp2ChunkParser * parser)
{
  memset (parser, 0, sizeof (Rtmp2ChunkParser));
  parser->config.chunk_size = 128;
  parser->config.window_ack_size = 2500000;
  parser->config.peer_bandwidth = 2500000;
  parser->chunk_streams = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) rtmp2_chunk_message_free);
  parser->read_buffer = g_byte_array_new ();
}

void
rtmp2_chunk_parser_clear (Rtmp2ChunkParser * parser)
{
  if (parser->chunk_streams) {
    g_hash_table_destroy (parser->chunk_streams);
    parser->chunk_streams = NULL;
  }
  if (parser->read_buffer) {
    g_byte_array_free (parser->read_buffer, TRUE);
    parser->read_buffer = NULL;
  }
}

gboolean
rtmp2_chunk_parser_process (Rtmp2ChunkParser * parser, const guint8 * data,
    gsize size, GList ** messages, GError ** error)
{
  const guint8 *ptr = data;
  gsize remaining = size;
  guint8 chunk_stream_id;
  Rtmp2ChunkType chunk_type;
  Rtmp2ChunkMessage *msg;
  gsize chunk_data_size;
  gsize bytes_to_read;

  *messages = NULL;

  while (remaining > 0) {
    if (!parse_basic_header (&ptr, &remaining, &chunk_stream_id, &chunk_type)) {
      g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
          "Failed to parse basic header");
      return FALSE;
    }

    msg = (Rtmp2ChunkMessage *) g_hash_table_lookup (parser->chunk_streams,
        GUINT_TO_POINTER (chunk_stream_id));

    if (!msg) {
      msg = rtmp2_chunk_message_new ();
      msg->chunk_stream_id = chunk_stream_id;
      g_hash_table_insert (parser->chunk_streams,
          GUINT_TO_POINTER (chunk_stream_id), msg);
    }

    if (chunk_type != RTMP2_CHUNK_TYPE_3) {
      if (!parse_message_header (&ptr, &remaining, chunk_type, msg)) {
        g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
            "Failed to parse message header");
        return FALSE;
      }

      if (chunk_type == RTMP2_CHUNK_TYPE_0) {
        msg->buffer = gst_buffer_new_allocate (NULL, msg->message_length, NULL);
        msg->bytes_received = 0;
      } else if (chunk_type == RTMP2_CHUNK_TYPE_1 || chunk_type == RTMP2_CHUNK_TYPE_2) {
        msg->timestamp += msg->timestamp_delta;
      }
    }

    chunk_data_size = MIN (parser->config.chunk_size - msg->bytes_received,
        msg->message_length - msg->bytes_received);
    bytes_to_read = MIN (chunk_data_size, remaining);

    if (msg->buffer && bytes_to_read > 0) {
      GstMapInfo map;
      if (gst_buffer_map (msg->buffer, &map, GST_MAP_WRITE)) {
        memcpy (map.data + msg->bytes_received, ptr, bytes_to_read);
        gst_buffer_unmap (msg->buffer, &map);
      }
    }

    msg->bytes_received += bytes_to_read;
    ptr += bytes_to_read;
    remaining -= bytes_to_read;

    if (msg->bytes_received >= msg->message_length) {
      msg->complete = TRUE;
      *messages = g_list_append (*messages, msg);
      g_hash_table_remove (parser->chunk_streams,
          GUINT_TO_POINTER (chunk_stream_id));
    }
  }

  return TRUE;
}

Rtmp2ChunkMessage *
rtmp2_chunk_message_new (void)
{
  Rtmp2ChunkMessage *msg = g_new0 (Rtmp2ChunkMessage, 1);
  return msg;
}

void
rtmp2_chunk_message_free (Rtmp2ChunkMessage * msg)
{
  if (!msg)
    return;

  if (msg->buffer)
    gst_buffer_unref (msg->buffer);

  g_free (msg);
}

