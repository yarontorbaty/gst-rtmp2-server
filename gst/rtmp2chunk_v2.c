/*
 * RTMP Chunk Parser v2 - Based on SRS implementation
 * 
 * Key differences from v1:
 * - Continuous buffering like SRS's SrsFastStream
 * - Waits for complete chunks before parsing
 * - Proper Type 3 continuation handling
 * - No partial chunk processing
 */

#include "rtmp2chunk.h"
#include <gst/gst.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (rtmp2_chunk_v2_debug);
#define GST_CAT_DEFAULT rtmp2_chunk_v2_debug

void
rtmp2_chunk_v2_debug_init (void)
{
  GST_DEBUG_CATEGORY_INIT (rtmp2_chunk_v2_debug, "rtmp2chunk_v2", 0,
      "RTMP2 Chunk Parser V2");
}

/* Like SRS's SrsFastStream - buffers incomplete data */
typedef struct {
  guint8 *buffer;
  gsize buffer_size;
  gsize data_start;
  gsize data_end;
} ChunkBuffer;

static ChunkBuffer*
chunk_buffer_new(gsize initial_size)
{
  ChunkBuffer *buf = g_new0(ChunkBuffer, 1);
  buf->buffer_size = initial_size;
  buf->buffer = g_malloc(initial_size);
  buf->data_start = 0;
  buf->data_end = 0;
  return buf;
}

static void
chunk_buffer_free(ChunkBuffer *buf)
{
  if (buf) {
    g_free(buf->buffer);
    g_free(buf);
  }
}

/* Get available data size */
static gsize
chunk_buffer_available(ChunkBuffer *buf)
{
  return buf->data_end - buf->data_start;
}

/* Append new data to buffer */
static void
chunk_buffer_append(ChunkBuffer *buf, const guint8 *data, gsize size)
{
  /* Check if we need to grow buffer */
  gsize needed = buf->data_end + size;
  if (needed > buf->buffer_size) {
    /* Compact first if there's wasted space at start */
    if (buf->data_start > 0) {
      gsize available = buf->data_end - buf->data_start;
      memmove(buf->buffer, buf->buffer + buf->data_start, available);
      buf->data_start = 0;
      buf->data_end = available;
      needed = buf->data_end + size;
    }
    
    /* Still need more space? Grow buffer */
    if (needed > buf->buffer_size) {
      gsize new_size = buf->buffer_size;
      while (new_size < needed) {
        new_size *= 2;
      }
      buf->buffer = g_realloc(buf->buffer, new_size);
      buf->buffer_size = new_size;
    }
  }
  
  /* Append data */
  memcpy(buf->buffer + buf->data_end, data, size);
  buf->data_end += size;
}

/* Consume N bytes from buffer */
static void
chunk_buffer_consume(ChunkBuffer *buf, gsize size)
{
  buf->data_start += size;
  if (buf->data_start >= buf->data_end) {
    buf->data_start = buf->data_end = 0;  /* Reset */
  }
}

/* Peek at buffer without consuming */
static const guint8*
chunk_buffer_peek(ChunkBuffer *buf)
{
  return buf->buffer + buf->data_start;
}

/* NEW Chunk Parser V2 structure */
typedef struct {
  Rtmp2ChunkConfig config;
  ChunkBuffer *buffer;  /* Like SRS's in_buffer */
  GHashTable *chunk_streams;  /* csid -> Rtmp2ChunkMessage */
} Rtmp2ChunkParserV2;

void
rtmp2_chunk_parser_v2_init(Rtmp2ChunkParserV2 *parser)
{
  parser->config.chunk_size = 128;  /* Default RTMP chunk size */
  parser->buffer = chunk_buffer_new(65536);  /* 64KB initial buffer */
  parser->chunk_streams = g_hash_table_new_full(
      g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) rtmp2_chunk_message_free);
}

void
rtmp2_chunk_parser_v2_clear(Rtmp2ChunkParserV2 *parser)
{
  chunk_buffer_free(parser->buffer);
  g_hash_table_destroy(parser->chunk_streams);
}

/* Process chunks - returns TRUE and sets messages when complete messages ready */
/* Like SRS's recv_interlaced_message - loops internally until message complete */
gboolean
rtmp2_chunk_parser_v2_process(Rtmp2ChunkParserV2 *parser, 
                               const guint8 *data, gsize size,
                               GList **messages, GError **error)
{
  *messages = NULL;
  
  /* Append new data to buffer (like SRS's in_buffer) */
  chunk_buffer_append(parser->buffer, data, size);
  
  GST_DEBUG("Parser v2: appended %zu bytes, buffer now has %zu bytes",
      size, chunk_buffer_available(parser->buffer));
  
  /* Try to parse complete messages from buffer */
  /* Keep going until buffer doesn't have enough for next chunk */
  while (chunk_buffer_available(parser->buffer) > 0) {
    
    const guint8 *ptr = chunk_buffer_peek(parser->buffer);
    gsize available = chunk_buffer_available(parser->buffer);
    
    /* Need at least 1 byte for basic header */
    if (available < 1) {
      GST_DEBUG("Parser v2: need basic header (1 byte), have %zu - waiting", available);
      break;
    }
    
    /* Parse basic header */
    guint8 fmt = (ptr[0] >> 6) & 0x03;
    guint8 csid = ptr[0] & 0x3f;
    gsize header_consumed = 1;
    
    if (csid == 0) {
      if (available < 2) break;  /* Need extended ID */
      csid = 64 + ptr[1];
      header_consumed = 2;
    } else if (csid == 1) {
      if (available < 3) break;  /* Need extended ID */
      csid = 64 + ptr[1] + (ptr[2] << 8);
      header_consumed = 3;
    }
    
    GST_DEBUG("Parser v2: fmt=%d csid=%d consumed=%zu", fmt, csid, header_consumed);
    
    /* Continue with message header parsing... */
    /* For now, just consume basic header and continue */
    chunk_buffer_consume(parser->buffer, header_consumed);
    
    /* TODO: Implement rest of parsing following SRS pattern */
    /* This is a stub - needs full implementation */
    break;  /* Temp: exit to prevent infinite loop */
  }
  
  return TRUE;
}

