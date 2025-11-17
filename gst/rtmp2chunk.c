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
#include <gst/gst.h>

/* Define our own debug category */
GST_DEBUG_CATEGORY_STATIC (rtmp2_chunk_debug);
#define GST_CAT_DEFAULT rtmp2_chunk_debug

/* Initialize debug category - called from plugin init */
void
rtmp2_chunk_debug_init (void)
{
  GST_DEBUG_CATEGORY_INIT (rtmp2_chunk_debug, "rtmp2chunk",
      0, "RTMP2 Chunk Parser");
}

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

  GST_DEBUG ("Parsing basic header, available bytes: %zu", *size);
  if (*size < 1) {
    GST_DEBUG ("Not enough data for basic header (need 1, have %zu)", *size);
    return FALSE;
  }

  byte = read_uint8 (data, size);
  *chunk_type = (byte >> 6) & 0x03;
  *chunk_stream_id = byte & 0x3f;
  GST_DEBUG ("Basic header: chunk_type=%d, chunk_stream_id=%d (from first byte=0x%02x)",
      *chunk_type, *chunk_stream_id, byte);

  if (*chunk_stream_id == 0) {
    GST_DEBUG ("Extended chunk stream ID (1 byte)");
    if (*size < 1) {
      GST_DEBUG ("Not enough data for extended chunk stream ID (need 1, have %zu)", *size);
      return FALSE;
    }
    *chunk_stream_id = 64 + read_uint8 (data, size);
    GST_DEBUG ("Extended chunk stream ID: %d", *chunk_stream_id);
  } else if (*chunk_stream_id == 1) {
    GST_DEBUG ("Extended chunk stream ID (2 bytes)");
    if (*size < 2) {
      GST_DEBUG ("Not enough data for extended chunk stream ID (need 2, have %zu)", *size);
      return FALSE;
    }
    *chunk_stream_id = 64 + read_uint8 (data, size);
    *chunk_stream_id += (read_uint8 (data, size) << 8);
    GST_DEBUG ("Extended chunk stream ID: %d", *chunk_stream_id);
  }

  GST_DEBUG ("Basic header parsed: chunk_type=%d, chunk_stream_id=%d, remaining=%zu",
      *chunk_type, *chunk_stream_id, *size);
  return TRUE;
}

static gboolean
parse_message_header (const guint8 ** data, gsize * size,
    Rtmp2ChunkType chunk_type, Rtmp2ChunkMessage * msg)
{
  GST_DEBUG ("Parsing message header, chunk_type=%d, available bytes: %zu",
      chunk_type, *size);
  switch (chunk_type) {
    case RTMP2_CHUNK_TYPE_0:
      GST_DEBUG ("Chunk type 0: need 11 bytes");
      if (*size < 11) {
        GST_DEBUG ("Not enough data for type 0 header (need 11, have %zu)", *size);
        return FALSE;
      }
      msg->timestamp = read_uint24_be (data, size);
      msg->message_length = read_uint24_be (data, size);
      msg->message_type = read_uint8 (data, size);
      msg->message_stream_id = read_uint32_le (data, size);
      GST_DEBUG ("Type 0 header: timestamp=%u, length=%u, type=%u, stream_id=%u",
          msg->timestamp, msg->message_length, msg->message_type, msg->message_stream_id);
      if (msg->timestamp == 0xffffff) {
        GST_DEBUG ("Extended timestamp (4 bytes)");
        if (*size < 4) {
          GST_DEBUG ("Not enough data for extended timestamp (need 4, have %zu)", *size);
          return FALSE;
        }
        msg->timestamp = read_uint32_be (data, size);
        GST_DEBUG ("Extended timestamp: %u", msg->timestamp);
      }
      break;
    case RTMP2_CHUNK_TYPE_1:
      GST_DEBUG ("Chunk type 1: need 7 bytes");
      if (*size < 7) {
        GST_DEBUG ("Not enough data for type 1 header (need 7, have %zu)", *size);
        return FALSE;
      }
      msg->timestamp_delta = read_uint24_be (data, size);
      msg->message_length = read_uint24_be (data, size);
      msg->message_type = read_uint8 (data, size);
      GST_DEBUG ("Type 1 header: timestamp_delta=%u, length=%u, type=%u",
          msg->timestamp_delta, msg->message_length, msg->message_type);
      if (msg->timestamp_delta == 0xffffff) {
        GST_DEBUG ("Extended timestamp delta (4 bytes)");
        if (*size < 4) {
          GST_DEBUG ("Not enough data for extended timestamp delta (need 4, have %zu)", *size);
          return FALSE;
        }
        msg->timestamp_delta = read_uint32_be (data, size);
        GST_DEBUG ("Extended timestamp delta: %u", msg->timestamp_delta);
      }
      break;
    case RTMP2_CHUNK_TYPE_2:
      GST_DEBUG ("Chunk type 2: need 3 bytes");
      if (*size < 3) {
        GST_DEBUG ("Not enough data for type 2 header (need 3, have %zu)", *size);
        return FALSE;
      }
      msg->timestamp_delta = read_uint24_be (data, size);
      GST_DEBUG ("Type 2 header: timestamp_delta=%u", msg->timestamp_delta);
      if (msg->timestamp_delta == 0xffffff) {
        GST_DEBUG ("Extended timestamp delta (4 bytes)");
        if (*size < 4) {
          GST_DEBUG ("Not enough data for extended timestamp delta (need 4, have %zu)", *size);
          return FALSE;
        }
        msg->timestamp_delta = read_uint32_be (data, size);
        GST_DEBUG ("Extended timestamp delta: %u", msg->timestamp_delta);
      }
      break;
    case RTMP2_CHUNK_TYPE_3:
      GST_DEBUG ("Chunk type 3: no header, using previous message");
      break;
    default:
      GST_WARNING ("Unknown chunk type: %d", chunk_type);
      return FALSE;
  }

  GST_DEBUG ("Message header parsed successfully, remaining bytes: %zu", *size);
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

  GST_DEBUG ("Processing %zu bytes through chunk parser", size);
  *messages = NULL;

  while (remaining > 0) {
    GST_DEBUG ("Starting new chunk, remaining bytes: %zu", remaining);
    if (!parse_basic_header (&ptr, &remaining, &chunk_stream_id, &chunk_type)) {
      GST_WARNING ("Failed to parse basic header, remaining: %zu", remaining);
      g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
          "Failed to parse basic header");
      return FALSE;
    }

    msg = (Rtmp2ChunkMessage *) g_hash_table_lookup (parser->chunk_streams,
        GUINT_TO_POINTER (chunk_stream_id));

    if (!msg) {
      GST_DEBUG ("Creating new message for chunk stream %d", chunk_stream_id);
      msg = rtmp2_chunk_message_new ();
      msg->chunk_stream_id = chunk_stream_id;
      g_hash_table_insert (parser->chunk_streams,
          GUINT_TO_POINTER (chunk_stream_id), msg);
    } else {
      GST_DEBUG ("Found existing message for chunk stream %d (bytes_received=%zu, length=%u)",
          chunk_stream_id, msg->bytes_received, msg->message_length);
    }

    if (chunk_type != RTMP2_CHUNK_TYPE_3) {
      GST_DEBUG ("Parsing message header for chunk type %d", chunk_type);
      if (!parse_message_header (&ptr, &remaining, chunk_type, msg)) {
        GST_WARNING ("Failed to parse message header, chunk_type=%d, remaining=%zu",
            chunk_type, remaining);
        g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
            "Failed to parse message header");
        return FALSE;
      }

      if (chunk_type == RTMP2_CHUNK_TYPE_0) {
        GST_DEBUG ("Type 0: allocating buffer for message length %u", msg->message_length);
        msg->buffer = gst_buffer_new_allocate (NULL, msg->message_length, NULL);
        msg->bytes_received = 0;
      } else if (chunk_type == RTMP2_CHUNK_TYPE_1 || chunk_type == RTMP2_CHUNK_TYPE_2) {
        GST_DEBUG ("Type %d: updating timestamp (delta=%u)", chunk_type, msg->timestamp_delta);
        msg->timestamp += msg->timestamp_delta;
      }
    } else {
      GST_DEBUG ("Type 3: continuing previous message");
    }

    /* Calculate how much data to read for this chunk */
    if (chunk_type == RTMP2_CHUNK_TYPE_3) {
      /* Type 3: read remaining bytes to complete the message */
      chunk_data_size = msg->message_length - msg->bytes_received;
    } else {
      /* Type 0/1/2: read up to chunk_size, but not more than message length */
      chunk_data_size = MIN (parser->config.chunk_size - (msg->bytes_received % parser->config.chunk_size),
          msg->message_length - msg->bytes_received);
    }
    bytes_to_read = MIN (chunk_data_size, remaining);
    GST_DEBUG ("Reading chunk data: chunk_size=%u, bytes_received=%zu, message_length=%u, "
        "chunk_data_size=%zu, bytes_to_read=%zu, remaining=%zu",
        parser->config.chunk_size, msg->bytes_received, msg->message_length,
        chunk_data_size, bytes_to_read, remaining);

    if (msg->buffer && bytes_to_read > 0) {
      GstMapInfo map;
      if (gst_buffer_map (msg->buffer, &map, GST_MAP_WRITE)) {
        memcpy (map.data + msg->bytes_received, ptr, bytes_to_read);
        gst_buffer_unmap (msg->buffer, &map);
        GST_DEBUG ("Copied %zu bytes to buffer", bytes_to_read);
      }
    }

    msg->bytes_received += bytes_to_read;
    ptr += bytes_to_read;
    remaining -= bytes_to_read;

    GST_DEBUG ("Chunk processed: bytes_received=%zu/%u, remaining=%zu",
        msg->bytes_received, msg->message_length, remaining);

    if (msg->bytes_received >= msg->message_length) {
      GST_DEBUG ("Message complete! type=%u, length=%u", msg->message_type, msg->message_length);
      msg->complete = TRUE;
      *messages = g_list_append (*messages, msg);
      /* Don't remove from hash table yet - caller will free it after processing */
      /* g_hash_table_remove (parser->chunk_streams, GUINT_TO_POINTER (chunk_stream_id)); */
    }
  }

  GST_DEBUG ("Chunk parser finished, returning %d messages", g_list_length (*messages));
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

