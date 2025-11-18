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

#ifndef __RTMP2_CHUNK_V2_H__
#define __RTMP2_CHUNK_V2_H__

#include <glib.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include "rtmp2chunk.h"  /* Reuse types from original */

G_BEGIN_DECLS

/* FastBuffer - like SRS's SrsFastStream */
/* Buffered reading with ensure() to wait for needed bytes */
typedef struct {
  guint8 *data;
  gsize capacity;
  gsize read_pos;
  gsize write_pos;
  GInputStream *stream;  /* Source for reading more data */
} Rtmp2FastBuffer;

/* V2 Parser - SRS-based with FastBuffer */
typedef struct {
  Rtmp2ChunkConfig config;
  GHashTable *chunk_streams;  /* chunk_stream_id -> Rtmp2ChunkMessage */
  Rtmp2FastBuffer *buffer;
} Rtmp2ChunkParserV2;

/* FastBuffer operations */
Rtmp2FastBuffer *rtmp2_fast_buffer_new (GInputStream *stream, gsize capacity);
void rtmp2_fast_buffer_free (Rtmp2FastBuffer *buf);
gboolean rtmp2_fast_buffer_ensure (Rtmp2FastBuffer *buf, gsize needed, GError **error);
guint8 rtmp2_fast_buffer_read_uint8 (Rtmp2FastBuffer *buf);
guint32 rtmp2_fast_buffer_read_uint24_be (Rtmp2FastBuffer *buf);
guint32 rtmp2_fast_buffer_read_uint32_be (Rtmp2FastBuffer *buf);
guint32 rtmp2_fast_buffer_read_uint32_le (Rtmp2FastBuffer *buf);
gboolean rtmp2_fast_buffer_read_bytes (Rtmp2FastBuffer *buf, guint8 *dest, gsize size);
gsize rtmp2_fast_buffer_available (Rtmp2FastBuffer *buf);

/* V2 Parser operations */
void rtmp2_chunk_parser_v2_init (Rtmp2ChunkParserV2 *parser, GInputStream *stream);
void rtmp2_chunk_parser_v2_clear (Rtmp2ChunkParserV2 *parser);
gboolean rtmp2_chunk_parser_v2_read_message (Rtmp2ChunkParserV2 *parser, 
                                              Rtmp2ChunkMessage **message,
                                              GError **error);

void rtmp2_chunk_v2_debug_init (void);

G_END_DECLS

#endif /* __RTMP2_CHUNK_V2_H__ */

