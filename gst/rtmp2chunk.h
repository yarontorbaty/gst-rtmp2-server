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

#ifndef __RTMP2_CHUNK_H__
#define __RTMP2_CHUNK_H__

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define RTMP2_CHUNK_BASIC_HEADER_MAX_SIZE 3
#define RTMP2_CHUNK_MESSAGE_HEADER_MAX_SIZE 11
#define RTMP2_CHUNK_MAX_SIZE 65536

typedef enum {
  RTMP2_CHUNK_TYPE_0 = 0,  /* 11 bytes */
  RTMP2_CHUNK_TYPE_1 = 1,  /* 7 bytes */
  RTMP2_CHUNK_TYPE_2 = 2,  /* 3 bytes */
  RTMP2_CHUNK_TYPE_3 = 3   /* 0 bytes */
} Rtmp2ChunkType;

typedef enum {
  RTMP2_MESSAGE_SET_CHUNK_SIZE = 1,
  RTMP2_MESSAGE_ABORT = 2,
  RTMP2_MESSAGE_ACK = 3,
  RTMP2_MESSAGE_USER_CONTROL = 4,
  RTMP2_MESSAGE_WINDOW_ACK_SIZE = 5,
  RTMP2_MESSAGE_SET_PEER_BANDWIDTH = 6,
  RTMP2_MESSAGE_AUDIO = 8,
  RTMP2_MESSAGE_VIDEO = 9,
  RTMP2_MESSAGE_AMF3_METADATA = 15,
  RTMP2_MESSAGE_AMF0_METADATA = 18,
  RTMP2_MESSAGE_AMF0_COMMAND = 20,
  RTMP2_MESSAGE_AMF3_COMMAND = 17
} Rtmp2MessageType;

typedef struct {
  guint32 chunk_size;
  guint32 window_ack_size;
  guint32 peer_bandwidth;
  guint32 in_ack_size;
  guint64 bytes_received;
} Rtmp2ChunkConfig;

typedef struct {
  guint8 chunk_stream_id;
  Rtmp2ChunkType chunk_type;
  guint32 timestamp;
  guint32 timestamp_delta;
  guint32 message_length;
  guint8 message_type;
  guint32 message_stream_id;
  
  GstBuffer *buffer;
  gsize bytes_received;
  gboolean complete;
} Rtmp2ChunkMessage;

typedef struct {
  Rtmp2ChunkConfig config;
  GHashTable *chunk_streams;  /* chunk_stream_id -> Rtmp2ChunkMessage */
  GByteArray *read_buffer;
  gsize bytes_read;
  gboolean reading_header;
  guint8 current_chunk_stream_id;
  Rtmp2ChunkType current_chunk_type;
} Rtmp2ChunkParser;

void rtmp2_chunk_parser_init (Rtmp2ChunkParser *parser);
void rtmp2_chunk_parser_clear (Rtmp2ChunkParser *parser);
gboolean rtmp2_chunk_parser_process (Rtmp2ChunkParser *parser, const guint8 *data, gsize size, 
                                     GList **messages, GError **error);
Rtmp2ChunkMessage *rtmp2_chunk_message_new (void);
void rtmp2_chunk_message_free (Rtmp2ChunkMessage *msg);

G_END_DECLS

#endif /* __RTMP2_CHUNK_H__ */

