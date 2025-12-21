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

#include "rtmp2chunk_v2.h"
#include <string.h>
#include <gst/gst.h>

/* Define our own debug category */
GST_DEBUG_CATEGORY_STATIC (rtmp2_chunk_v2_debug);
#define GST_CAT_DEFAULT rtmp2_chunk_v2_debug

/* Initialize debug category - called from plugin init */
void
rtmp2_chunk_v2_debug_init (void)
{
  GST_DEBUG_CATEGORY_INIT (rtmp2_chunk_v2_debug, "rtmp2chunk_v2",
      0, "RTMP2 Chunk Parser V2 (SRS-based)");
}

/* ===== FastBuffer Implementation ===== */
/* Like SRS's SrsFastStream - buffers data and ensures bytes available */

Rtmp2FastBuffer *
rtmp2_fast_buffer_new (GInputStream *stream, gsize capacity)
{
  Rtmp2FastBuffer *buf = g_new0 (Rtmp2FastBuffer, 1);
  buf->data = g_malloc (capacity);
  buf->capacity = capacity;
  buf->read_pos = 0;
  buf->write_pos = 0;
  buf->stream = g_object_ref (stream);
  return buf;
}

void
rtmp2_fast_buffer_free (Rtmp2FastBuffer *buf)
{
  if (!buf)
    return;
  g_free (buf->data);
  if (buf->stream)
    g_object_unref (buf->stream);
  g_free (buf);
}

gsize
rtmp2_fast_buffer_available (Rtmp2FastBuffer *buf)
{
  return buf->write_pos - buf->read_pos;
}

/* KEY FUNCTION: Like SRS's in_buffer->grow(skt, N) */
/* Ensures at least 'needed' bytes are available in buffer */
/* Reads from socket until we have enough data */
gboolean
rtmp2_fast_buffer_ensure (Rtmp2FastBuffer *buf, gsize needed, GError **error)
{
  gsize available = buf->write_pos - buf->read_pos;
  
  GST_DEBUG ("ensure: needed=%zu, available=%zu, read_pos=%zu, write_pos=%zu",
      needed, available, buf->read_pos, buf->write_pos);
  
  /* Already have enough */
  if (available >= needed) {
    GST_DEBUG ("ensure: already have enough data");
    return TRUE;
  }
  
  /* Calculate space at end of buffer */
  gsize space_at_end = buf->capacity - buf->write_pos;
  gsize space_needed = needed - available;
  
  /* Only compact if we actually need the space at the beginning */
  /* This prevents mid-stream position resets that corrupt parsing */
  if (buf->read_pos > 0 && space_at_end < space_needed) {
    if (available > 0) {
      memmove (buf->data, buf->data + buf->read_pos, available);
    }
    buf->write_pos = available;
    buf->read_pos = 0;
    GST_DEBUG ("ensure: compacted buffer (necessary), new write_pos=%zu", buf->write_pos);
    
    /* Recalculate space at end after compaction */
    space_at_end = buf->capacity - buf->write_pos;
  }
  
  /* Grow buffer if still not enough space */
  if (space_at_end < space_needed) {
    gsize new_capacity = buf->capacity;
    while (new_capacity < (buf->write_pos + space_needed)) {
      new_capacity *= 2;
    }
    GST_INFO ("ensure: growing buffer from %zu to %zu bytes", buf->capacity, new_capacity);
    buf->data = g_realloc (buf->data, new_capacity);
    buf->capacity = new_capacity;
  }
  
  /* Read until we have enough data - THIS IS THE KEY DIFFERENCE FROM V1 */
  /* V1 would return partial data, V2 waits for complete data like SRS */
  while ((buf->write_pos - buf->read_pos) < needed) {
    gsize space = buf->capacity - buf->write_pos;
    gssize n;
    
    GST_DEBUG ("ensure: reading more data (need %zu more bytes, have space for %zu)",
        needed - (buf->write_pos - buf->read_pos), space);
    
    /* Synchronous blocking read - wait for data */
    n = g_input_stream_read (buf->stream, 
        buf->data + buf->write_pos, 
        space, NULL, error);
    
    if (n < 0) {
      GST_WARNING ("ensure: read error: %s", error && *error ? (*error)->message : "unknown");
      return FALSE;
    }
    
    if (n == 0) {
      GST_WARNING ("ensure: EOF - connection closed (needed %zu, have %zu)",
          needed, buf->write_pos - buf->read_pos);
      g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
          "Connection closed while waiting for data");
      return FALSE;
    }
    
    buf->write_pos += n;
    GST_DEBUG ("ensure: read %zd bytes, write_pos now %zu, available now %zu",
        n, buf->write_pos, buf->write_pos - buf->read_pos);
  }
  
  GST_DEBUG ("ensure: success - have %zu bytes available (needed %zu)",
      buf->write_pos - buf->read_pos, needed);
  return TRUE;
}

guint8
rtmp2_fast_buffer_read_uint8 (Rtmp2FastBuffer *buf)
{
  guint8 val = buf->data[buf->read_pos];
  buf->read_pos++;
  return val;
}

guint32
rtmp2_fast_buffer_read_uint24_be (Rtmp2FastBuffer *buf)
{
  guint32 val = (buf->data[buf->read_pos] << 16) |
                (buf->data[buf->read_pos + 1] << 8) |
                buf->data[buf->read_pos + 2];
  buf->read_pos += 3;
  return val;
}

guint32
rtmp2_fast_buffer_read_uint32_be (Rtmp2FastBuffer *buf)
{
  guint32 val = (buf->data[buf->read_pos] << 24) |
                (buf->data[buf->read_pos + 1] << 16) |
                (buf->data[buf->read_pos + 2] << 8) |
                buf->data[buf->read_pos + 3];
  buf->read_pos += 4;
  return val;
}

guint32
rtmp2_fast_buffer_read_uint32_le (Rtmp2FastBuffer *buf)
{
  guint32 val = buf->data[buf->read_pos] |
                (buf->data[buf->read_pos + 1] << 8) |
                (buf->data[buf->read_pos + 2] << 16) |
                (buf->data[buf->read_pos + 3] << 24);
  buf->read_pos += 4;
  return val;
}

gboolean
rtmp2_fast_buffer_read_bytes (Rtmp2FastBuffer *buf, guint8 *dest, gsize size)
{
  memcpy (dest, buf->data + buf->read_pos, size);
  buf->read_pos += size;
  return TRUE;
}

static inline void
rtmp2_chunk_v2_trace_chunk (const gchar *stage, guint8 chunk_stream_id,
    Rtmp2ChunkType chunk_type, Rtmp2ChunkMessage *msg, gsize payload)
{
  GST_TRACE ("[pkt %s] csid=%u fmt=%d ts=%u delta=%u len=%u type=%u received=%zu/%u payload=%zu",
      stage, chunk_stream_id, chunk_type,
      msg ? msg->timestamp : 0,
      msg ? msg->timestamp_delta : 0,
      msg ? msg->message_length : 0,
      msg ? msg->message_type : 0,
      msg ? msg->bytes_received : 0,
      msg ? msg->message_length : 0,
      payload);
}

static Rtmp2ChunkMessage *
rtmp2_chunk_message_detach_output (Rtmp2ChunkMessage *state)
{
  Rtmp2ChunkMessage *out = rtmp2_chunk_message_new ();

  out->chunk_stream_id = state->chunk_stream_id;
  out->chunk_type = state->chunk_type;
  out->timestamp = state->timestamp;
  out->timestamp_delta = state->timestamp_delta;
  out->message_length = state->message_length;
  out->message_type = state->message_type;
  out->message_stream_id = state->message_stream_id;
  out->buffer = state->buffer;
  out->bytes_received = state->bytes_received;
  out->complete = TRUE;

  state->buffer = NULL;
  state->bytes_received = 0;
  state->complete = FALSE;

  return out;
}

/* ===== V2 Parser Implementation ===== */
/* Following SRS's recv_interlaced_message pattern */

void
rtmp2_chunk_parser_v2_init (Rtmp2ChunkParserV2 *parser, GInputStream *stream)
{
  memset (parser, 0, sizeof (Rtmp2ChunkParserV2));
  parser->config.chunk_size = 128;
  parser->config.window_ack_size = 2500000;
  parser->config.peer_bandwidth = 2500000;
  parser->chunk_streams = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) rtmp2_chunk_message_free);
  parser->buffer = rtmp2_fast_buffer_new (stream, 65536);  /* 64KB initial buffer */
  memset (&parser->diagnostics, 0, sizeof (parser->diagnostics));
  GST_INFO ("Parser V2 initialized with 64KB buffer");
}

void
rtmp2_chunk_parser_v2_clear (Rtmp2ChunkParserV2 *parser)
{
  rtmp2_chunk_parser_v2_dump_diagnostics (parser);
  if (parser->chunk_streams) {
    g_hash_table_destroy (parser->chunk_streams);
    parser->chunk_streams = NULL;
  }
  if (parser->buffer) {
    rtmp2_fast_buffer_free (parser->buffer);
    parser->buffer = NULL;
  }
}

/* Parse basic header - with ensure() before reading */
static gboolean
parse_basic_header_v2 (Rtmp2FastBuffer *buf, guint8 *chunk_stream_id,
    Rtmp2ChunkType *chunk_type, GError **error)
{
  guint8 byte;
  
  /* 1. Ensure 1 byte available */
  if (!rtmp2_fast_buffer_ensure (buf, 1, error)) {
    GST_WARNING ("Failed to ensure 1 byte for basic header");
    return FALSE;
  }
  
  /* 2. Read first byte */
  byte = rtmp2_fast_buffer_read_uint8 (buf);
  *chunk_type = (byte >> 6) & 0x03;
  *chunk_stream_id = byte & 0x3f;
  
  GST_DEBUG ("Basic header: fmt=%d, csid=%d (byte=0x%02x)",
      *chunk_type, *chunk_stream_id, byte);
  
  /* 3. Handle extended chunk stream ID */
  if (*chunk_stream_id == 0) {
    /* Extended 1 byte */
    if (!rtmp2_fast_buffer_ensure (buf, 1, error)) {
      GST_WARNING ("Failed to ensure 1 byte for extended csid");
      return FALSE;
    }
    *chunk_stream_id = 64 + rtmp2_fast_buffer_read_uint8 (buf);
    GST_DEBUG ("Extended csid (1 byte): %d", *chunk_stream_id);
  } else if (*chunk_stream_id == 1) {
    /* Extended 2 bytes */
    if (!rtmp2_fast_buffer_ensure (buf, 2, error)) {
      GST_WARNING ("Failed to ensure 2 bytes for extended csid");
      return FALSE;
    }
    *chunk_stream_id = 64 + rtmp2_fast_buffer_read_uint8 (buf);
    *chunk_stream_id += (rtmp2_fast_buffer_read_uint8 (buf) << 8);
    GST_DEBUG ("Extended csid (2 bytes): %d", *chunk_stream_id);
  }
  
  return TRUE;
}

/* Parse message header - with ensure() before reading */
static gboolean
parse_message_header_v2 (Rtmp2FastBuffer *buf, Rtmp2ChunkType chunk_type,
    Rtmp2ChunkMessage *msg, GError **error)
{
  guint32 timestamp;
  
  GST_DEBUG ("Parsing message header, fmt=%d", chunk_type);
  
  switch (chunk_type) {
    case RTMP2_CHUNK_TYPE_0:
      /* Need 11 bytes */
      if (!rtmp2_fast_buffer_ensure (buf, 11, error)) {
        GST_WARNING ("Failed to ensure 11 bytes for type 0 header");
        return FALSE;
      }
      
      timestamp = rtmp2_fast_buffer_read_uint24_be (buf);
      msg->message_length = rtmp2_fast_buffer_read_uint24_be (buf);
      msg->message_type = rtmp2_fast_buffer_read_uint8 (buf);
      msg->message_stream_id = rtmp2_fast_buffer_read_uint32_le (buf);
      
      /* Extended timestamp? */
      if (timestamp == 0xffffff) {
        if (!rtmp2_fast_buffer_ensure (buf, 4, error)) {
          GST_WARNING ("Failed to ensure 4 bytes for extended timestamp");
          return FALSE;
        }
        msg->timestamp = rtmp2_fast_buffer_read_uint32_be (buf);
        GST_DEBUG ("Type 0: extended timestamp=%u", msg->timestamp);
      } else {
        msg->timestamp = timestamp;
      }
      
      GST_DEBUG ("Type 0: ts=%u, len=%u, type=%u, stream_id=%u",
          msg->timestamp, msg->message_length, msg->message_type, msg->message_stream_id);
      break;
      
    case RTMP2_CHUNK_TYPE_1:
      /* Need 7 bytes */
      if (!rtmp2_fast_buffer_ensure (buf, 7, error)) {
        GST_WARNING ("Failed to ensure 7 bytes for type 1 header");
        return FALSE;
      }
      
      timestamp = rtmp2_fast_buffer_read_uint24_be (buf);
      msg->message_length = rtmp2_fast_buffer_read_uint24_be (buf);
      msg->message_type = rtmp2_fast_buffer_read_uint8 (buf);
      
      /* Extended timestamp? */
      if (timestamp == 0xffffff) {
        if (!rtmp2_fast_buffer_ensure (buf, 4, error)) {
          GST_WARNING ("Failed to ensure 4 bytes for extended timestamp delta");
          return FALSE;
        }
        msg->timestamp_delta = rtmp2_fast_buffer_read_uint32_be (buf);
        GST_DEBUG ("Type 1: extended timestamp_delta=%u", msg->timestamp_delta);
      } else {
        msg->timestamp_delta = timestamp;
      }
      
      msg->timestamp += msg->timestamp_delta;
      GST_DEBUG ("Type 1: ts_delta=%u, ts=%u, len=%u, type=%u",
          msg->timestamp_delta, msg->timestamp, msg->message_length, msg->message_type);
      break;
      
    case RTMP2_CHUNK_TYPE_2:
      /* Need 3 bytes */
      if (!rtmp2_fast_buffer_ensure (buf, 3, error)) {
        GST_WARNING ("Failed to ensure 3 bytes for type 2 header");
        return FALSE;
      }
      
      timestamp = rtmp2_fast_buffer_read_uint24_be (buf);
      
      /* Extended timestamp? */
      if (timestamp == 0xffffff) {
        if (!rtmp2_fast_buffer_ensure (buf, 4, error)) {
          GST_WARNING ("Failed to ensure 4 bytes for extended timestamp delta");
          return FALSE;
        }
        msg->timestamp_delta = rtmp2_fast_buffer_read_uint32_be (buf);
        GST_DEBUG ("Type 2: extended timestamp_delta=%u", msg->timestamp_delta);
      } else {
        msg->timestamp_delta = timestamp;
      }
      
      msg->timestamp += msg->timestamp_delta;
      GST_DEBUG ("Type 2: ts_delta=%u, ts=%u", msg->timestamp_delta, msg->timestamp);
      break;
      
    case RTMP2_CHUNK_TYPE_3:
      /* No header - continue previous message */
      GST_DEBUG ("Type 3: no header, continuing previous message");
      break;
      
    default:
      g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
          "Unknown chunk type: %d", chunk_type);
      return FALSE;
  }
  
  return TRUE;
}

/* Read one complete RTMP message - like SRS's recv_interlaced_message */
/* Returns TRUE when a complete message is ready */
gboolean
rtmp2_chunk_parser_v2_read_message (Rtmp2ChunkParserV2 *parser,
    Rtmp2ChunkMessage **message, GError **error)
{
  Rtmp2FastBuffer *buf = parser->buffer;
  guint8 chunk_stream_id;
  Rtmp2ChunkType chunk_type;
  Rtmp2ChunkMessage *msg = NULL;
  
  *message = NULL;
  
  /* Loop to read chunks until we have a complete message */
  while (TRUE) {
    /* 1. Parse basic header */
    if (!parse_basic_header_v2 (buf, &chunk_stream_id, &chunk_type, error)) {
      return FALSE;
    }
    
    parser->diagnostics.total_chunks++;
    rtmp2_chunk_v2_trace_chunk ("basic-header", chunk_stream_id, chunk_type, NULL, 0);
    
    /* 2. Get or create message for this chunk stream */
    msg = (Rtmp2ChunkMessage *) g_hash_table_lookup (parser->chunk_streams,
        GUINT_TO_POINTER (chunk_stream_id));
    
    if (!msg) {
      if (chunk_type != RTMP2_CHUNK_TYPE_0) {
        parser->diagnostics.invalid_fresh_headers++;
        parser->diagnostics.dropped_chunks++;
        GST_WARNING ("Fresh chunk stream %d started with fmt=%d - invalid per RTMP spec, dropping chunk",
            chunk_stream_id, chunk_type);
        continue;
      }
      
      GST_DEBUG ("Creating new message for chunk stream %d", chunk_stream_id);
      msg = rtmp2_chunk_message_new ();
      msg->chunk_stream_id = chunk_stream_id;
      g_hash_table_insert (parser->chunk_streams,
          GUINT_TO_POINTER (chunk_stream_id), msg);
    } else {
      GST_DEBUG ("Continuing message for chunk stream %d (received=%zu/%u)",
          chunk_stream_id, msg->bytes_received, msg->message_length);
    }
    
    rtmp2_chunk_v2_trace_chunk ("chunk-start", chunk_stream_id, chunk_type, msg, 0);
    
    /* 3. Parse message header (if not type 3) */
    if (chunk_type != RTMP2_CHUNK_TYPE_3) {
      msg->chunk_stream_id = chunk_stream_id;
      msg->chunk_type = chunk_type;
      
      if (!parse_message_header_v2 (buf, chunk_type, msg, error)) {
        return FALSE;
      }
      
      /* For type 0/1/2, (re)allocate buffer for new message */
      if (msg->bytes_received == 0 || chunk_type == RTMP2_CHUNK_TYPE_0) {
        /* If Type 0 on partially received message, abandon old and start new */
        if (msg->bytes_received > 0 && chunk_type == RTMP2_CHUNK_TYPE_0) {
          parser->diagnostics.restarts_from_type0++;
          GST_DEBUG ("Type 0 on partially complete message (csid=%d) - starting fresh",
              chunk_stream_id);
          if (msg->buffer) {
            gst_buffer_unref (msg->buffer);
            msg->buffer = NULL;
          }
        }
        /* Handle zero-length messages (empty control messages) */
        if (msg->message_length == 0) {
          GST_DEBUG ("Zero-length message for type=%d stream=%d - returning empty complete message",
              msg->message_type, chunk_stream_id);
          msg->buffer = gst_buffer_new_allocate (NULL, 0, NULL);
          msg->bytes_received = 0;
          msg->complete = TRUE;
          parser->diagnostics.completed_messages++;
          *message = rtmp2_chunk_message_detach_output (msg);
          return TRUE;
        }
        
        /* Sanity check message length */
        if (msg->message_length > 10 * 1024 * 1024) {  /* 10MB max */
          parser->diagnostics.dropped_chunks++;
          GST_WARNING ("Suspicious message length: %u bytes (type=%d, csid=%d), skipping this stream",
              msg->message_length, msg->message_type, chunk_stream_id);
          g_hash_table_remove (parser->chunk_streams, GUINT_TO_POINTER (chunk_stream_id));
          continue;
        }
        
        /* Normal message - allocate buffer */
        if (msg->buffer) {
          gst_buffer_unref (msg->buffer);
        }
        
        msg->buffer = gst_buffer_new_allocate (NULL, msg->message_length, NULL);
        msg->bytes_received = 0;
        msg->complete = FALSE;
        GST_DEBUG ("Allocated %u byte buffer for message type=%d", msg->message_length, msg->message_type);
      }
    } else {
      msg->chunk_type = chunk_type;
      /* Type 3 - continuation chunk that reuses previous header */
      if (msg->message_length == 0) {
        /* No previous message to continue from */
        parser->diagnostics.continuations_without_state++;
        parser->diagnostics.dropped_chunks++;
        GST_WARNING ("Type 3 continuation but no previous message header (csid=%d) - dropping chunk",
            chunk_stream_id);
        g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
            "Type 3 continuation without an in-flight message (csid=%d)", chunk_stream_id);
        return FALSE;
      }
      
      /* If buffer is NULL but we have message_length, this is a NEW message reusing previous header */
      if (!msg->buffer && msg->bytes_received == 0) {
        GST_DEBUG ("Type 3 starting new message on csid=%d, reusing previous header (length=%u, type=%u)",
            chunk_stream_id, msg->message_length, msg->message_type);
        msg->buffer = gst_buffer_new_allocate (NULL, msg->message_length, NULL);
        msg->complete = FALSE;
      }
    }
    
    /* 4. Calculate chunk payload size */
    gsize bytes_left = msg->message_length - msg->bytes_received;
    gsize chunk_payload_size = MIN (parser->config.chunk_size, bytes_left);
    
    GST_DEBUG ("Reading chunk payload: chunk_size=%u, bytes_left=%zu, payload=%zu",
        parser->config.chunk_size, bytes_left, chunk_payload_size);
    rtmp2_chunk_v2_trace_chunk ("chunk-payload", chunk_stream_id, chunk_type, msg, chunk_payload_size);
    
    /* 5. Ensure payload bytes available - KEY DIFFERENCE FROM V1 */
    /* V1 would process partial payload, V2 waits for complete chunk */
    if (!rtmp2_fast_buffer_ensure (buf, chunk_payload_size, error)) {
      GST_WARNING ("Failed to ensure %zu bytes for chunk payload", chunk_payload_size);
      return FALSE;
    }
    
    /* 6. Copy payload to message buffer */
    if (msg->buffer) {
      GstMapInfo map;
      if (gst_buffer_map (msg->buffer, &map, GST_MAP_WRITE)) {
        rtmp2_fast_buffer_read_bytes (buf, map.data + msg->bytes_received, chunk_payload_size);
        gst_buffer_unmap (msg->buffer, &map);
        GST_DEBUG ("Copied %zu bytes to message buffer at offset %zu",
            chunk_payload_size, msg->bytes_received);
      } else {
        g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
            "Failed to map message buffer");
        return FALSE;
      }
    }
    
    msg->bytes_received += chunk_payload_size;
    
    /* 7. Check if message is complete */
    if (msg->bytes_received >= msg->message_length) {
      parser->diagnostics.completed_messages++;
      GST_INFO ("✅ Message complete! type=%u, length=%u, timestamp=%u",
          msg->message_type, msg->message_length, msg->timestamp);
      msg->complete = TRUE;
      rtmp2_chunk_v2_trace_chunk ("message-complete", chunk_stream_id, chunk_type, msg, 0);
      
      *message = rtmp2_chunk_message_detach_output (msg);
      return TRUE;
    }
    
    /* Message not complete yet, continue reading next chunk */
    GST_DEBUG ("Message incomplete (%zu/%u), reading next chunk",
        msg->bytes_received, msg->message_length);
  }
  
  /* Unreachable */
  return FALSE;
}

void
rtmp2_chunk_parser_v2_dump_diagnostics (Rtmp2ChunkParserV2 *parser)
{
  if (!parser)
    return;
  
  GST_INFO ("RTMP parser diagnostics: chunks=%" G_GUINT64_FORMAT
      " completed=%" G_GUINT64_FORMAT " dropped=%" G_GUINT64_FORMAT
      " invalid_fresh=%" G_GUINT64_FORMAT " continuations=%" G_GUINT64_FORMAT
      " restarts=%" G_GUINT64_FORMAT,
      parser->diagnostics.total_chunks,
      parser->diagnostics.completed_messages,
      parser->diagnostics.dropped_chunks,
      parser->diagnostics.invalid_fresh_headers,
      parser->diagnostics.continuations_without_state,
      parser->diagnostics.restarts_from_type0);
}

