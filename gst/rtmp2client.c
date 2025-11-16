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

#include "rtmp2client.h"
#include "gstrtmp2server.h"
#include <string.h>
#include <glib/gprintf.h>

static void
write_uint8 (GByteArray * ba, guint8 val)
{
  g_byte_array_append (ba, &val, 1);
}

static void
write_uint16_be (GByteArray * ba, guint16 val)
{
  guint8 data[2];
  data[0] = (val >> 8) & 0xff;
  data[1] = val & 0xff;
  g_byte_array_append (ba, data, 2);
}

static void
write_uint24_be (GByteArray * ba, guint32 val)
{
  guint8 data[3];
  data[0] = (val >> 16) & 0xff;
  data[1] = (val >> 8) & 0xff;
  data[2] = val & 0xff;
  g_byte_array_append (ba, data, 3);
}

static void
write_uint32_be (GByteArray * ba, guint32 val)
{
  guint8 data[4];
  data[0] = (val >> 24) & 0xff;
  data[1] = (val >> 16) & 0xff;
  data[2] = (val >> 8) & 0xff;
  data[3] = val & 0xff;
  g_byte_array_append (ba, data, 4);
}

static void
write_uint32_le (GByteArray * ba, guint32 val)
{
  guint8 data[4];
  data[0] = val & 0xff;
  data[1] = (val >> 8) & 0xff;
  data[2] = (val >> 16) & 0xff;
  data[3] = (val >> 24) & 0xff;
  g_byte_array_append (ba, data, 4);
}

Rtmp2Client *
rtmp2_client_new (GSocketConnection * connection, GIOStream * io_stream)
{
  Rtmp2Client *client;
  GIOStream *stream_to_use;

  client = g_new0 (Rtmp2Client, 1);

  /* Use TLS stream if provided, otherwise use connection's stream */
  if (io_stream) {
    client->io_stream = g_object_ref (io_stream);
    stream_to_use = io_stream;
  } else if (connection) {
    client->connection = g_object_ref (connection);
    client->socket = g_socket_connection_get_socket (connection);
    stream_to_use = G_IO_STREAM (connection);
  } else {
    g_free (client);
    return NULL;
  }

  client->input_stream = g_io_stream_get_input_stream (stream_to_use);
  client->output_stream = g_io_stream_get_output_stream (stream_to_use);

  client->state = RTMP2_CLIENT_STATE_HANDSHAKE;
  rtmp2_handshake_init (&client->handshake);
  rtmp2_chunk_parser_init (&client->chunk_parser);
  rtmp2_flv_parser_init (&client->flv_parser);

  client->handshake_complete = FALSE;
  client->connect_received = FALSE;
  client->publish_received = FALSE;
  client->last_activity = g_get_monotonic_time ();

  /* Initialize Enhanced RTMP capabilities */
  client->client_caps = rtmp2_enhanced_capabilities_new ();
  client->server_caps = rtmp2_enhanced_capabilities_new ();
  client->server_caps->caps_ex = RTMP2_CAPS_RECONNECT | RTMP2_CAPS_MULTITRACK |
      RTMP2_CAPS_TIMESTAMP_NANO_OFFSET;
  client->server_caps->supports_amf3 = TRUE;
  client->supports_amf3 = FALSE;
  client->timestamp_nano_offset = 0;

  return client;
}

void
rtmp2_client_free (Rtmp2Client * client)
{
  if (!client)
    return;

  if (client->read_source) {
    g_source_destroy (client->read_source);
    g_source_unref (client->read_source);
  }

  if (client->timeout_source) {
    g_source_destroy (client->timeout_source);
    g_source_unref (client->timeout_source);
  }

  rtmp2_chunk_parser_clear (&client->chunk_parser);
  rtmp2_flv_parser_clear (&client->flv_parser);

  g_free (client->application);
  g_free (client->stream_key);
  g_free (client->tc_url);

  if (client->client_caps)
    rtmp2_enhanced_capabilities_free (client->client_caps);
  if (client->server_caps)
    rtmp2_enhanced_capabilities_free (client->server_caps);

  if (client->io_stream)
    g_object_unref (client->io_stream);
  if (client->connection)
    g_object_unref (client->connection);

  g_free (client);
}

gboolean
rtmp2_client_send_handshake (Rtmp2Client * client, GError ** error)
{
  guint8 s0[1];
  guint8 s1[RTMP2_HANDSHAKE_SIZE];
  guint8 s2[RTMP2_HANDSHAKE_SIZE];
  gsize bytes_written;

  rtmp2_handshake_generate_s0 (&client->handshake, s0);
  rtmp2_handshake_generate_s1 (&client->handshake, s1);
  rtmp2_handshake_generate_s2 (&client->handshake, client->handshake.c1, s2);

  if (!g_output_stream_write_all (client->output_stream, s0, 1, &bytes_written,
          NULL, error))
    return FALSE;

  if (!g_output_stream_write_all (client->output_stream, s1, RTMP2_HANDSHAKE_SIZE,
          &bytes_written, NULL, error))
    return FALSE;

  if (!g_output_stream_write_all (client->output_stream, s2, RTMP2_HANDSHAKE_SIZE,
          &bytes_written, NULL, error))
    return FALSE;

  return TRUE;
}

gboolean
rtmp2_client_process_data (Rtmp2Client * client, GError ** error)
{
  guint8 buffer[4096];
  gssize bytes_read;
  GList *messages = NULL;
  GList *l;
  Rtmp2ChunkMessage *msg;
  GList *flv_tags = NULL;
  Rtmp2FlvTag *tag;
  GstRtmp2ServerSrc *src;

  if (client->state == RTMP2_CLIENT_STATE_DISCONNECTED ||
      client->state == RTMP2_CLIENT_STATE_ERROR) {
    return FALSE;
  }

  /* Handle handshake */
  if (!client->handshake_complete) {
    if (client->handshake.state == RTMP2_HANDSHAKE_STATE_C0) {
      bytes_read = g_input_stream_read (client->input_stream, buffer, 1, NULL,
          error);
      if (bytes_read < 0)
        return FALSE;
      if (bytes_read == 0) {
        client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
        return FALSE;
      }
      if (!rtmp2_handshake_process_c0 (&client->handshake, buffer, bytes_read)) {
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
    } else if (client->handshake.state == RTMP2_HANDSHAKE_STATE_C1) {
      bytes_read = g_input_stream_read (client->input_stream, buffer,
          RTMP2_HANDSHAKE_SIZE, NULL, error);
      if (bytes_read < 0)
        return FALSE;
      if (bytes_read == 0) {
        client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
        return FALSE;
      }
      if (bytes_read < RTMP2_HANDSHAKE_SIZE) {
        /* Need more data */
        return TRUE;
      }
      if (!rtmp2_handshake_process_c1 (&client->handshake, buffer, bytes_read)) {
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
      if (!rtmp2_client_send_handshake (client, error)) {
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
    } else if (client->handshake.state == RTMP2_HANDSHAKE_STATE_C2) {
      bytes_read = g_input_stream_read (client->input_stream, buffer,
          RTMP2_HANDSHAKE_SIZE, NULL, error);
      if (bytes_read < 0)
        return FALSE;
      if (bytes_read == 0) {
        client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
        return FALSE;
      }
      if (bytes_read < RTMP2_HANDSHAKE_SIZE) {
        return TRUE;
      }
      if (!rtmp2_handshake_process_c2 (&client->handshake, buffer, bytes_read)) {
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
      client->handshake_complete = TRUE;
      client->state = RTMP2_CLIENT_STATE_CONNECTING;
    }
    return TRUE;
  }

  /* Read RTMP chunks */
  bytes_read = g_input_stream_read (client->input_stream, buffer,
      sizeof (buffer), NULL, error);
  if (bytes_read < 0)
    return FALSE;
  if (bytes_read == 0) {
    client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
    return FALSE;
  }

  client->last_activity = g_get_monotonic_time ();

  if (!rtmp2_chunk_parser_process (&client->chunk_parser, buffer, bytes_read,
          &messages, error)) {
    return FALSE;
  }

  /* Process messages */
  for (l = messages; l; l = l->next) {
    msg = (Rtmp2ChunkMessage *) l->data;

    switch (msg->message_type) {
      case RTMP2_MESSAGE_SET_CHUNK_SIZE:
        if (gst_buffer_get_size (msg->buffer) >= 4) {
          GstMapInfo map;
          if (gst_buffer_map (msg->buffer, &map, GST_MAP_READ)) {
            guint32 chunk_size = ((guint32) map.data[0] << 24) |
                ((guint32) map.data[1] << 16) |
                ((guint32) map.data[2] << 8) | map.data[3];
            client->chunk_parser.config.chunk_size = chunk_size;
            gst_buffer_unmap (msg->buffer, &map);
          }
        }
        break;

      case RTMP2_MESSAGE_AMF0_COMMAND:
      case RTMP2_MESSAGE_AMF3_COMMAND:{
        GstMapInfo map;
        if (gst_buffer_map (msg->buffer, &map, GST_MAP_READ)) {
          const guint8 *cmd_data = map.data;
          gsize cmd_size = map.size;

          /* Skip AMF3 format selector if present */
          if (msg->message_type == RTMP2_MESSAGE_AMF3_COMMAND && cmd_size > 0 &&
              cmd_data[0] == 0) {
            cmd_data++;
            cmd_size--;
          }

          if (!client->connect_received) {
            rtmp2_client_parse_connect (client, cmd_data, cmd_size, error);
          } else if (!client->publish_received) {
            rtmp2_client_parse_publish (client, cmd_data, cmd_size, error);
          }
          gst_buffer_unmap (msg->buffer, &map);
        }
        break;
      }

      case RTMP2_MESSAGE_AMF0_METADATA:
      case RTMP2_MESSAGE_AMF3_METADATA:
        /* Enhanced metadata - handle if needed */
        break;

      case RTMP2_MESSAGE_VIDEO:
      case RTMP2_MESSAGE_AUDIO:
        if (client->state == RTMP2_CLIENT_STATE_PUBLISHING) {
          GstMapInfo map;
          GList *new_tags = NULL;
          if (gst_buffer_map (msg->buffer, &map, GST_MAP_READ)) {
            rtmp2_flv_parser_process (&client->flv_parser, map.data, map.size,
                &new_tags, error);
            if (new_tags) {
              client->flv_parser.pending_tags =
                  g_list_concat (client->flv_parser.pending_tags, new_tags);
            }
            gst_buffer_unmap (msg->buffer, &map);
          }
        }
        break;
    }
  }

  if (messages) {
    g_list_free_full (messages, (GDestroyNotify) rtmp2_chunk_message_free);
  }

  return TRUE;
}

gboolean
rtmp2_client_send_ack (Rtmp2Client * client, guint32 bytes, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 basic_header = 0x02;  /* Chunk stream ID 2, type 0 */
  guint8 message_type = RTMP2_MESSAGE_ACK;

  write_uint8 (ba, basic_header);
  write_uint24_be (ba, 0);     /* timestamp */
  write_uint24_be (ba, 4);     /* message length */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);     /* message stream ID */
  write_uint32_be (ba, bytes);  /* ack value */

  gsize bytes_written;
  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);

  return ret;
}

gboolean
rtmp2_client_send_window_ack_size (Rtmp2Client * client, guint32 size,
    GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 basic_header = 0x02;
  guint8 message_type = RTMP2_MESSAGE_WINDOW_ACK_SIZE;

  write_uint8 (ba, basic_header);
  write_uint24_be (ba, 0);
  write_uint24_be (ba, 4);
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);
  write_uint32_be (ba, size);

  gsize bytes_written;
  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);

  return ret;
}

gboolean
rtmp2_client_send_peer_bandwidth (Rtmp2Client * client, guint32 size,
    GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 basic_header = 0x02;
  guint8 message_type = RTMP2_MESSAGE_SET_PEER_BANDWIDTH;

  write_uint8 (ba, basic_header);
  write_uint24_be (ba, 0);
  write_uint24_be (ba, 5);
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);
  write_uint32_be (ba, size);
  write_uint8 (ba, 2);         /* limit type: dynamic */

  gsize bytes_written;
  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);

  return ret;
}

gboolean
rtmp2_client_send_connect_result (Rtmp2Client * client, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 basic_header = 0x03;  /* Chunk stream ID 3, type 0 */
  guint8 message_type = client->supports_amf3 ?
      RTMP2_MESSAGE_AMF3_COMMAND : RTMP2_MESSAGE_AMF0_COMMAND;
  gsize bytes_written;

  /* Write basic header */
  write_uint8 (ba, basic_header);
  write_uint24_be (ba, 0);     /* timestamp */
  write_uint32_le (ba, 0);     /* message stream ID */

  /* Write message type and length (will update later) */
  gsize msg_start = ba->len;
  write_uint24_be (ba, 0);     /* message length placeholder */
  write_uint8 (ba, message_type);

  /* Write AMF0 command response */
  if (client->supports_amf3) {
    /* AMF3 format selector byte */
    write_uint8 (ba, 0);       /* Format 0 = AMF0 */
  }

  rtmp2_enhanced_send_connect_result (ba, client->server_caps, error);

  /* Update message length */
  guint32 msg_len = ba->len - msg_start - 4;
  ba->data[msg_start] = (msg_len >> 16) & 0xff;
  ba->data[msg_start + 1] = (msg_len >> 8) & 0xff;
  ba->data[msg_start + 2] = msg_len & 0xff;

  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);

  if (ret)
    client->state = RTMP2_CLIENT_STATE_CONNECTED;

  return ret;
}

gboolean
rtmp2_client_send_publish_result (Rtmp2Client * client, GError ** error)
{
  client->state = RTMP2_CLIENT_STATE_PUBLISHING;
  return TRUE;
}

gboolean
rtmp2_client_parse_connect (Rtmp2Client * client, const guint8 * data,
    gsize size, GError ** error)
{
  GstRtmp2ServerSrc *src = (GstRtmp2ServerSrc *) client->user_data;
  const guint8 *ptr = data;
  gsize remaining = size;

  /* Check for AMF3 format selector */
  if (remaining > 0 && data[0] == 0) {
    ptr++;
    remaining--;
  }

  /* Parse Enhanced RTMP connect command */
  if (!rtmp2_enhanced_parse_connect (ptr, remaining, client->client_caps,
          error)) {
    return FALSE;
  }

  client->supports_amf3 = client->client_caps->supports_amf3;

  if (src && src->application) {
    client->application = g_strdup (src->application);
  }

  client->connect_received = TRUE;
  rtmp2_client_send_connect_result (client, error);
  rtmp2_client_send_window_ack_size (client, 2500000, error);
  rtmp2_client_send_peer_bandwidth (client, 2500000, error);

  return TRUE;
}

gboolean
rtmp2_client_parse_publish (Rtmp2Client * client, const guint8 * data,
    gsize size, GError ** error)
{
  /* Simplified parsing - full implementation would parse AMF0 */
  GstRtmp2ServerSrc *src = (GstRtmp2ServerSrc *) client->user_data;

  if (src && src->stream_key) {
    /* Validate stream key if set */
    /* For now, just accept */
  }

  client->publish_received = TRUE;
  rtmp2_client_send_publish_result (client, error);

  return TRUE;
}

