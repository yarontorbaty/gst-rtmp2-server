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
#include "rtmp2amf.h"
#include <string.h>
#include <glib/gprintf.h>

/* Define our own debug category */
GST_DEBUG_CATEGORY_STATIC (rtmp2_client_debug);
#define GST_CAT_DEFAULT rtmp2_client_debug

/* Initialize debug category - called from plugin init */
void
rtmp2_client_debug_init (void)
{
  GST_DEBUG_CATEGORY_INIT (rtmp2_client_debug, "rtmp2client",
      0, "RTMP2 Client");
}

static void
write_uint8 (GByteArray * ba, guint8 val)
{
  g_byte_array_append (ba, &val, 1);
}

/* static void */
/* write_uint16_be (GByteArray * ba, guint16 val) */
/* { */
/*   guint8 data[2]; */
/*   data[0] = (val >> 8) & 0xff; */
/*   data[1] = val & 0xff; */
/*   g_byte_array_append (ba, data, 2); */
/* } */

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

static void
write_uint16_be (GByteArray * ba, guint16 val)
{
  guint8 data[2];
  data[0] = (val >> 8) & 0xff;
  data[1] = val & 0xff;
  g_byte_array_append (ba, data, 2);
}

#define RTMP2_USER_CONTROL_STREAM_BEGIN 0

static gboolean
rtmp2_client_send_user_control (Rtmp2Client * client, guint16 event_type,
    guint32 event_data, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 basic_header = 0x02;  /* Chunk stream ID 2, type 0 */
  guint8 message_type = RTMP2_MESSAGE_USER_CONTROL;
  gsize bytes_written;

  /* Basic header + timestamp */
  write_uint8 (ba, basic_header);
  write_uint24_be (ba, 0);

  /* Message header */
  write_uint24_be (ba, 6);     /* event_type (2) + event_data (4) */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);     /* message stream ID */

  /* Event payload */
  write_uint16_be (ba, event_type);
  write_uint32_be (ba, event_data);

  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);
  return ret;
}

static gboolean
rtmp2_client_send_on_bw_done (Rtmp2Client * client, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 basic_header = 0x03;  /* Chunk stream ID 3, type 0 */
  guint8 message_type = RTMP2_MESSAGE_AMF0_COMMAND;
  guint32 timestamp = 0;
  gsize bytes_written;

  write_uint8 (ba, basic_header);
  write_uint24_be (ba, timestamp);

  gsize msg_start = ba->len;
  write_uint24_be (ba, 0);     /* placeholder */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);     /* message stream ID */

  guint8 amf0_string = RTMP2_AMF0_STRING;
  guint8 amf0_number = RTMP2_AMF0_NUMBER;
  guint8 amf0_null = RTMP2_AMF0_NULL;

  g_byte_array_append (ba, &amf0_string, 1);
  rtmp2_amf0_write_string (ba, "onBWDone");

  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, 0.0);

  g_byte_array_append (ba, &amf0_null, 1);

  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, 0.0);

  guint32 msg_len = ba->len - msg_start - 8;
  ba->data[msg_start] = (msg_len >> 16) & 0xff;
  ba->data[msg_start + 1] = (msg_len >> 8) & 0xff;
  ba->data[msg_start + 2] = msg_len & 0xff;

  GST_DEBUG ("Sending onBWDone command");

  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);
  if (ret)
    GST_DEBUG ("onBWDone sent successfully");
  return ret;
}

gboolean
rtmp2_client_send_release_stream_result (Rtmp2Client * client,
    gdouble transaction_id, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 message_type = client->supports_amf3 ?
      RTMP2_MESSAGE_AMF3_COMMAND : RTMP2_MESSAGE_AMF0_COMMAND;
  gsize bytes_written;

  write_uint8 (ba, 0x03);       /* chunk stream 3 type 0 */
  write_uint24_be (ba, 0);      /* timestamp */

  gsize msg_start = ba->len;
  write_uint24_be (ba, 0);      /* placeholder */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);      /* message stream ID */

  if (client->supports_amf3)
    write_uint8 (ba, 0);        /* AMF0 switch */

  guint8 amf0_string = RTMP2_AMF0_STRING;
  guint8 amf0_number = RTMP2_AMF0_NUMBER;
  guint8 amf0_null = RTMP2_AMF0_NULL;

  g_byte_array_append (ba, &amf0_string, 1);
  rtmp2_amf0_write_string (ba, "_result");

  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, transaction_id);

  g_byte_array_append (ba, &amf0_null, 1);

  guint8 amf0_boolean = RTMP2_AMF0_BOOLEAN;
  g_byte_array_append (ba, &amf0_boolean, 1);
  rtmp2_amf0_write_boolean (ba, TRUE);

  guint32 msg_len = ba->len - msg_start - 8;
  ba->data[msg_start] = (msg_len >> 16) & 0xff;
  ba->data[msg_start + 1] = (msg_len >> 8) & 0xff;
  ba->data[msg_start + 2] = msg_len & 0xff;

  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);
  return ret;
}

gboolean
rtmp2_client_send_check_bw_result (Rtmp2Client * client,
    gdouble transaction_id, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 message_type = client->supports_amf3 ?
      RTMP2_MESSAGE_AMF3_COMMAND : RTMP2_MESSAGE_AMF0_COMMAND;
  gsize bytes_written;

  write_uint8 (ba, 0x03);       /* chunk stream 3 type 0 */
  write_uint24_be (ba, 0);      /* timestamp */

  gsize msg_start = ba->len;
  write_uint24_be (ba, 0);      /* placeholder */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);      /* message stream ID */

  if (client->supports_amf3)
    write_uint8 (ba, 0);        /* AMF0 switch */

  guint8 amf0_string = RTMP2_AMF0_STRING;
  guint8 amf0_number = RTMP2_AMF0_NUMBER;
  guint8 amf0_null = RTMP2_AMF0_NULL;

  g_byte_array_append (ba, &amf0_string, 1);
  rtmp2_amf0_write_string (ba, "_result");

  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, transaction_id);

  g_byte_array_append (ba, &amf0_null, 1);

  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, 0.0);

  guint32 msg_len = ba->len - msg_start - 8;
  ba->data[msg_start] = (msg_len >> 16) & 0xff;
  ba->data[msg_start + 1] = (msg_len >> 8) & 0xff;
  ba->data[msg_start + 2] = msg_len & 0xff;

  GST_DEBUG ("Sending _checkbw result: %u total bytes, message length=%u",
      (guint)ba->len, msg_len);

  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  
  if (ret && G_IS_OUTPUT_STREAM (client->output_stream)) {
    g_output_stream_flush (client->output_stream, NULL, NULL);
  }
  
  g_byte_array_free (ba, TRUE);
  return ret;
}

gboolean
rtmp2_client_send_on_fc_publish (Rtmp2Client * client,
    const gchar * stream_name, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 message_type = RTMP2_MESSAGE_AMF0_COMMAND;
  gsize bytes_written;

  write_uint8 (ba, 0x03);
  write_uint24_be (ba, 0);

  gsize msg_start = ba->len;
  write_uint24_be (ba, 0);
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);

  guint8 amf0_string = RTMP2_AMF0_STRING;
  guint8 amf0_number = RTMP2_AMF0_NUMBER;
  guint8 amf0_null = RTMP2_AMF0_NULL;
  guint8 amf0_object = RTMP2_AMF0_OBJECT;

  g_byte_array_append (ba, &amf0_string, 1);
  rtmp2_amf0_write_string (ba, "onFCPublish");

  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, 0.0);

  g_byte_array_append (ba, &amf0_null, 1);

  g_byte_array_append (ba, &amf0_object, 1);
  rtmp2_amf0_write_object_property (ba, "level", "status");
  rtmp2_amf0_write_object_property (ba, "code",
      "NetStream.Publish.Start");
  if (stream_name) {
    gchar *description = g_strdup_printf ("FCPublish to stream %s",
        stream_name);
    rtmp2_amf0_write_object_property (ba, "description", description);
    g_free (description);
  } else {
    rtmp2_amf0_write_object_property (ba, "description",
        "FCPublish received");
  }
  rtmp2_amf0_write_object_end (ba);

  guint32 msg_len = ba->len - msg_start - 8;
  ba->data[msg_start] = (msg_len >> 16) & 0xff;
  ba->data[msg_start + 1] = (msg_len >> 8) & 0xff;
  ba->data[msg_start + 2] = msg_len & 0xff;

  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);
  return ret;
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

  /* Don't create buffered_input yet - will create after handshake */
  /* Handshake needs non-blocking reads, buffered input is for chunks only */
  client->buffered_input = NULL;
  client->read_thread = NULL;
  client->thread_running = FALSE;

  client->state = RTMP2_CLIENT_STATE_HANDSHAKE;
  rtmp2_handshake_init (&client->handshake);
  rtmp2_chunk_parser_init (&client->chunk_parser);
  rtmp2_flv_parser_init (&client->flv_parser);

  client->handshake_complete = FALSE;
  client->connect_received = FALSE;
  client->publish_received = FALSE;
  client->connect_transaction_id = 1.0;
  client->last_activity = g_get_monotonic_time ();
  client->stream_id = 1;

  /* Initialize Enhanced RTMP capabilities */
  client->client_caps = rtmp2_enhanced_capabilities_new ();
  client->server_caps = rtmp2_enhanced_capabilities_new ();
  client->server_caps->caps_ex = RTMP2_CAPS_RECONNECT | RTMP2_CAPS_MULTITRACK |
      RTMP2_CAPS_TIMESTAMP_NANO_OFFSET;
  client->server_caps->supports_amf3 = FALSE;
  client->supports_amf3 = FALSE;
  client->timestamp_nano_offset = 0;

  return client;
}

/* Dedicated read thread function - like gortmplib's goroutine */  
/* Continuously reads and processes data with synchronous buffered I/O */
static gpointer
client_read_thread_func (gpointer user_data)
{
  Rtmp2Client *client = (Rtmp2Client *) user_data;
  GError *error = NULL;
  GstRtmp2ServerSrc *src = NULL;
  
  if (client->user_data) {
    src = GST_RTMP2_SERVER_SRC (client->user_data);
  }
  
  GST_INFO ("Client read thread started (MediaMTX pattern: sync buffered reads)");
  
  /* Continuous read loop - like gortmplib's for loop */
  /* Key: synchronous reads with GBufferedInputStream handle TCP fragmentation */
  while (client->thread_running &&
         client->state != RTMP2_CLIENT_STATE_DISCONNECTED &&
         client->state != RTMP2_CLIENT_STATE_ERROR) {
    
    /* Call rtmp2_client_process_data which reads from buffered_input */
    /* With buffered input, chunks split across TCP packets are reassembled */
    gboolean processed;
    
    if (src) {
      g_mutex_lock (&src->clients_lock);
    }
    
    processed = rtmp2_client_process_data (client, &error);
    
    if (src) {
      g_mutex_unlock (&src->clients_lock);
    }
    
    if (error) {
      GST_WARNING ("Error in read thread: %s", error->message);
      g_error_free (error);
      error = NULL;
      client->state = RTMP2_CLIENT_STATE_ERROR;
      break;
    }
    
    if (!processed) {
      /* No data available or EOF */
      if (client->state == RTMP2_CLIENT_STATE_DISCONNECTED) {
        GST_INFO ("Client disconnected in read thread");
        break;
      }
      /* Brief sleep to avoid spinning on WOULD_BLOCK */
      g_usleep (1000);  /* 1ms */
    }
  }
  
  GST_INFO ("Client read thread exiting");
  return NULL;
}

/* Callback for continuous reading from client socket */
static gboolean
rtmp2_client_read_cb (GSocket * socket, GIOCondition condition, gpointer user_data)
{
  Rtmp2Client *client = (Rtmp2Client *) user_data;
  GError *error = NULL;
  gint read_attempts = 0;
  const gint max_reads = 100;  /* Drain up to 100 chunks per callback */
  static gint64 last_chunk_clear = 0;
  gint64 now = g_get_monotonic_time();

  /* Check if client is disconnected */
  if (client->state == RTMP2_CLIENT_STATE_DISCONNECTED ||
      client->state == RTMP2_CLIENT_STATE_ERROR) {
    GST_DEBUG ("Client disconnected/error in read callback, removing source");
    return G_SOURCE_REMOVE;
  }

  /* Check for error conditions */
  if (condition & (G_IO_ERR | G_IO_HUP)) {
    GST_WARNING ("Socket error or hangup detected");
    client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
    return G_SOURCE_REMOVE;
  }

  /* CRITICAL FIX: Clear stale incomplete chunks every 100ms */
  /* This prevents old control message fragments from blocking new video data */
  if (client->state == RTMP2_CLIENT_STATE_PUBLISHING && 
      (now - last_chunk_clear) > 100000) {  /* 100ms */
    guint incomplete = g_hash_table_size(client->chunk_parser.chunk_streams);
    if (incomplete > 0) {
      GST_INFO ("Clearing %u stale incomplete chunks to unblock video processing", incomplete);
      g_hash_table_remove_all(client->chunk_parser.chunk_streams);
    }
    last_chunk_clear = now;
  }

  /* Read multiple times to drain available data - fast and simple */
  for (read_attempts = 0; read_attempts < max_reads; read_attempts++) {
    gboolean processed = rtmp2_client_process_data (client, &error);
    
    if (error) {
      GST_WARNING ("Error processing client data: %s", error->message);
      g_error_free (error);
      client->state = RTMP2_CLIENT_STATE_ERROR;
      return G_SOURCE_REMOVE;
    }
    
    if (!processed) {
      /* No more data available right now - the 5ms timeout will retry */
      break;
    }
  }

  /* Keep the source alive */
  return G_SOURCE_CONTINUE;
}

/* Setup continuous reading for a client */
gboolean
rtmp2_client_start_reading (Rtmp2Client * client, GMainContext * context)
{
  if (!client || !client->socket) {
    GST_WARNING ("Cannot start reading: invalid client or socket");
    return FALSE;
  }

  /* Don't create if already exists */
  if (client->read_source) {
    GST_DEBUG ("Read source already exists");
    return TRUE;
  }

  /* Create GSource to monitor socket for incoming data */
  /* Use G_IO_IN | G_IO_ERR | G_IO_HUP with periodic polling */
  client->read_source = g_socket_create_source (client->socket,
      G_IO_IN | G_IO_ERR | G_IO_HUP, NULL);
  
  if (!client->read_source) {
    GST_ERROR ("Failed to create socket source");
    return FALSE;
  }

  g_source_set_callback (client->read_source,
      (GSourceFunc) rtmp2_client_read_cb, client, NULL);
  
  /* Set high priority to process I/O events quickly */
  g_source_set_priority (client->read_source, G_PRIORITY_HIGH);
  
  g_source_attach (client->read_source, context);

  /* Add 50ms timeout as backup - balances responsiveness vs CPU usage */
  /* G_IO_IN may not retrigger reliably, timeout ensures we keep checking */
  client->timeout_source = g_timeout_source_new (50);  /* 50ms - 10x slower than before */
  g_source_set_callback (client->timeout_source,
      (GSourceFunc) rtmp2_client_read_cb, client, NULL);
  g_source_attach (client->timeout_source, context);
  
  GST_INFO ("Started async reading for client (socket=%p) with G_IO_IN + 50ms timeout", 
      client->socket);
  return TRUE;
}

void
rtmp2_client_free (Rtmp2Client * client)
{
  if (!client)
    return;

  /* Stop dedicated read thread first */
  if (client->read_thread) {
    GST_INFO ("Stopping client read thread");
    client->thread_running = FALSE;
    g_thread_join (client->read_thread);
    client->read_thread = NULL;
  }

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

  if (client->buffered_input)
    g_object_unref (client->buffered_input);
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

  GST_DEBUG ("Generating handshake response (S0/S1/S2)");
  rtmp2_handshake_generate_s0 (&client->handshake, s0);
  rtmp2_handshake_generate_s1 (&client->handshake, s1);
  rtmp2_handshake_generate_s2 (&client->handshake, client->handshake.c1, s2);

  GST_DEBUG ("Writing S0 (1 byte)");
  if (!g_output_stream_write_all (client->output_stream, s0, 1, &bytes_written,
          NULL, error)) {
    GST_WARNING ("Failed to write S0: %s",
        error && *error ? (*error)->message : "Unknown");
    return FALSE;
  }
  GST_DEBUG ("Wrote %zu bytes (S0)", bytes_written);

  GST_DEBUG ("Writing S1 (%d bytes)", RTMP2_HANDSHAKE_SIZE);
  if (!g_output_stream_write_all (client->output_stream, s1, RTMP2_HANDSHAKE_SIZE,
          &bytes_written, NULL, error)) {
    GST_WARNING ("Failed to write S1: %s",
        error && *error ? (*error)->message : "Unknown");
    return FALSE;
  }
  GST_DEBUG ("Wrote %zu bytes (S1)", bytes_written);

  GST_DEBUG ("Writing S2 (%d bytes)", RTMP2_HANDSHAKE_SIZE);
  if (!g_output_stream_write_all (client->output_stream, s2, RTMP2_HANDSHAKE_SIZE,
          &bytes_written, NULL, error)) {
    GST_WARNING ("Failed to write S2: %s",
        error && *error ? (*error)->message : "Unknown");
    return FALSE;
  }
  GST_DEBUG ("Wrote %zu bytes (S2)", bytes_written);

  GST_DEBUG ("Handshake response sent successfully");
  return TRUE;
}

gboolean
rtmp2_client_process_data (Rtmp2Client * client, GError ** error)
{
  guint8 buffer[16384];  /* Increased from 4KB to 16KB for better throughput */
  gssize bytes_read;
  GList *messages = NULL;
  GList *l;
  Rtmp2ChunkMessage *msg;

  GST_DEBUG ("Processing data for client, state=%d, handshake_complete=%d, handshake_state=%d",
      client->state, client->handshake_complete, client->handshake.state);

  if (client->state == RTMP2_CLIENT_STATE_DISCONNECTED ||
      client->state == RTMP2_CLIENT_STATE_ERROR) {
    GST_DEBUG ("Client in disconnected/error state, returning FALSE");
    return FALSE;
  }

  /* Handle handshake */
  if (!client->handshake_complete) {
    GST_DEBUG ("Handshake not complete, state=%d", client->handshake.state);
    if (client->handshake.state == RTMP2_HANDSHAKE_STATE_C0) {
      GST_DEBUG ("Reading C0 (1 byte)");
      if (G_IS_POLLABLE_INPUT_STREAM (client->input_stream)) {
        bytes_read = g_pollable_input_stream_read_nonblocking (
            G_POLLABLE_INPUT_STREAM (client->input_stream),
            buffer, 1, NULL, error);
      } else {
        bytes_read = g_input_stream_read (client->input_stream, buffer, 1, NULL, error);
      }
      GST_DEBUG ("Read %zd bytes for C0", bytes_read);
      if (bytes_read < 0) {
        if (error && *error && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
          GST_DEBUG ("No data available for C0 (WOULD_BLOCK)");
          g_clear_error (error);
          return TRUE;
        }
        GST_WARNING ("Failed to read C0: %s", error && *error ? (*error)->message : "Unknown");
        return FALSE;
      }
      if (bytes_read == 0) {
        GST_DEBUG ("EOF while reading C0 (connection closed)");
        client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
        return FALSE;
      }
      if (!rtmp2_handshake_process_c0 (&client->handshake, buffer, bytes_read)) {
        GST_WARNING ("Failed to process C0");
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
      GST_DEBUG ("C0 processed successfully, version=0x%02x", buffer[0]);
    } else if (client->handshake.state == RTMP2_HANDSHAKE_STATE_C1) {
      GST_DEBUG ("Reading C1 (%d bytes)", RTMP2_HANDSHAKE_SIZE);
      if (G_IS_POLLABLE_INPUT_STREAM (client->input_stream)) {
        bytes_read = g_pollable_input_stream_read_nonblocking (
            G_POLLABLE_INPUT_STREAM (client->input_stream),
            buffer, RTMP2_HANDSHAKE_SIZE, NULL, error);
      } else {
        bytes_read = g_input_stream_read (client->input_stream, buffer,
            RTMP2_HANDSHAKE_SIZE, NULL, error);
      }
      GST_DEBUG ("Read %zd bytes for C1 (expected %d)", bytes_read, RTMP2_HANDSHAKE_SIZE);
      if (bytes_read < 0) {
        if (error && *error && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
          GST_DEBUG ("No data available for C1 (WOULD_BLOCK)");
          g_clear_error (error);
          return TRUE;
        }
        GST_WARNING ("Failed to read C1: %s", error && *error ? (*error)->message : "Unknown");
        return FALSE;
      }
      if (bytes_read == 0) {
        GST_DEBUG ("EOF while reading C1 (connection closed)");
        client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
        return FALSE;
      }
      if (bytes_read < RTMP2_HANDSHAKE_SIZE) {
        GST_DEBUG ("Need more data for C1 (%zd < %d)", bytes_read, RTMP2_HANDSHAKE_SIZE);
        return TRUE;
      }
      if (!rtmp2_handshake_process_c1 (&client->handshake, buffer, bytes_read)) {
        GST_WARNING ("Failed to process C1");
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
      GST_DEBUG ("C1 processed successfully, sending S0/S1/S2");
      if (!rtmp2_client_send_handshake (client, error)) {
        GST_WARNING ("Failed to send handshake response: %s",
            error && *error ? (*error)->message : "Unknown");
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
      GST_DEBUG ("Handshake response (S0/S1/S2) sent successfully");
    } else if (client->handshake.state == RTMP2_HANDSHAKE_STATE_C2) {
      GST_DEBUG ("Reading C2 (%d bytes)", RTMP2_HANDSHAKE_SIZE);
      if (G_IS_POLLABLE_INPUT_STREAM (client->input_stream)) {
        bytes_read = g_pollable_input_stream_read_nonblocking (
            G_POLLABLE_INPUT_STREAM (client->input_stream),
            buffer, RTMP2_HANDSHAKE_SIZE, NULL, error);
      } else {
        bytes_read = g_input_stream_read (client->input_stream, buffer,
            RTMP2_HANDSHAKE_SIZE, NULL, error);
      }
      GST_DEBUG ("Read %zd bytes for C2 (expected %d)", bytes_read, RTMP2_HANDSHAKE_SIZE);
      if (bytes_read < 0) {
        if (error && *error && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
          GST_DEBUG ("No data available for C2 (WOULD_BLOCK)");
          g_clear_error (error);
          return TRUE;
        }
        GST_WARNING ("Failed to read C2: %s", error && *error ? (*error)->message : "Unknown");
        return FALSE;
      }
      if (bytes_read == 0) {
        GST_DEBUG ("EOF while reading C2 (connection closed)");
        client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
        return FALSE;
      }
      if (bytes_read < RTMP2_HANDSHAKE_SIZE) {
        GST_DEBUG ("Need more data for C2 (%zd < %d)", bytes_read, RTMP2_HANDSHAKE_SIZE);
        return TRUE;
      }
      if (!rtmp2_handshake_process_c2 (&client->handshake, buffer, bytes_read)) {
        GST_WARNING ("Failed to process C2");
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
      GST_DEBUG ("C2 processed successfully, handshake complete!");
      client->handshake_complete = TRUE;
      client->state = RTMP2_CLIENT_STATE_CONNECTING;
      
      /* Create buffered input stream NOW (after handshake) for chunk reads */
      /* This is like gortmplib's bufio.Reader - handles TCP fragmentation */
      if (!client->buffered_input) {
        client->buffered_input = g_buffered_input_stream_new (client->input_stream);
        g_buffered_input_stream_set_buffer_size (
            G_BUFFERED_INPUT_STREAM (client->buffered_input), 65536);
        GST_INFO ("Created 64KB buffered input stream for chunk reading");
      }
      
      /* Start dedicated read thread (MediaMTX pattern) instead of async callbacks */
      client->thread_running = TRUE;
      client->read_thread = g_thread_new ("rtmp-client-reader", 
          client_read_thread_func, client);
      
      if (!client->read_thread) {
        GST_WARNING ("Failed to create read thread");
        client->state = RTMP2_CLIENT_STATE_ERROR;
        return FALSE;
      }
      
      GST_INFO ("Started dedicated read thread (MediaMTX pattern) after handshake");
    } else {
      GST_WARNING ("Unknown handshake state: %d", client->handshake.state);
    }
    return TRUE;
  }

  /* Read RTMP chunks using buffered input (handles TCP fragmentation) */
  GST_DEBUG ("Reading RTMP chunks (handshake complete)");
  
  /* CRITICAL: Use buffered_input like Go's bufio.Reader to handle TCP fragmentation */
  /* GBufferedInputStream internally buffers and reassembles fragmented TCP packets */
  /* This allows chunks split across multiple packets to be read as complete units */
  bytes_read = g_input_stream_read (client->buffered_input, buffer,
      sizeof (buffer), NULL, error);
  
  GST_DEBUG ("Read %zd bytes of chunk data from buffered stream", bytes_read);
  if (bytes_read < 0) {
    /* Check if it's WOULD_BLOCK (no data available yet) */
    if (error && *error && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
      GST_DEBUG ("No data available (WOULD_BLOCK), returning FALSE to avoid spinning");
      g_clear_error (error);
      return FALSE;  /* No data - let caller's adaptive sleep handle retry timing */
    }
    GST_WARNING ("Failed to read chunk data: %s",
        error && *error ? (*error)->message : "Unknown");
    return FALSE;
  }
  if (bytes_read == 0) {
    GST_DEBUG ("EOF while reading chunks (connection closed)");
    client->state = RTMP2_CLIENT_STATE_DISCONNECTED;
    return FALSE;
  }

  client->last_activity = g_get_monotonic_time ();

  GST_DEBUG ("Processing %zd bytes through chunk parser", bytes_read);
  if (!rtmp2_chunk_parser_process (&client->chunk_parser, buffer, bytes_read,
          &messages, error)) {
    GST_WARNING ("Failed to process chunks: %s",
        error && *error ? (*error)->message : "Unknown");
    return FALSE;
  }
  gint msg_count = g_list_length (messages);
  GST_DEBUG ("Chunk parser returned %d messages", msg_count);
  if (msg_count > 0) {
    GST_INFO ("Processing %d RTMP messages from this read", msg_count);
  }

  /* Process messages */
  gint msg_idx = 0;
  for (l = messages; l; l = l->next) {
    msg = (Rtmp2ChunkMessage *) l->data;
    msg_idx++;

    GST_DEBUG ("Processing message %d/%d: type=%d, length=%u, timestamp=%u, stream_id=%u, buffer=%p",
        msg_idx, msg_count, msg->message_type, msg->message_length, msg->timestamp, msg->message_stream_id, msg->buffer);

    if (!msg->buffer) {
      GST_WARNING ("Message type %d has no buffer, skipping", msg->message_type);
      continue;
    }

    GST_INFO ("Message %d/%d: type=%d, length=%u", msg_idx, msg_count, msg->message_type, msg->message_length);

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
        GST_DEBUG ("Processing AMF command message (AMF0=%d, AMF3=%d)",
            msg->message_type == RTMP2_MESSAGE_AMF0_COMMAND,
            msg->message_type == RTMP2_MESSAGE_AMF3_COMMAND);
        if (gst_buffer_map (msg->buffer, &map, GST_MAP_READ)) {
          const guint8 *cmd_data = map.data;
          gsize cmd_size = map.size;

          GST_DEBUG ("Command data: %zu bytes", cmd_size);

          /* Skip AMF3 format selector if present */
          if (msg->message_type == RTMP2_MESSAGE_AMF3_COMMAND && cmd_size > 0 &&
              cmd_data[0] == 0) {
            GST_DEBUG ("Skipping AMF3 format selector");
            cmd_data++;
            cmd_size--;
          }

          if (!client->connect_received) {
            GST_DEBUG ("Parsing connect command");
            if (rtmp2_client_parse_connect (client, cmd_data, cmd_size, error)) {
              GST_DEBUG ("Connect command parsed successfully, state=%d", client->state);
            } else {
              GST_WARNING ("Failed to parse connect command: %s",
                  error && *error ? (*error)->message : "Unknown");
            }
          } else if (!client->publish_received) {
            GST_DEBUG ("Parsing command (not connect, not publish yet)");
            /* Check if it's a publish command */
            if (cmd_size > 0) {
              const guint8 *cmd_ptr = cmd_data;
              gsize cmd_remaining = cmd_size;
              Rtmp2AmfValue value;
              gchar *cmd_name = NULL;
              
              /* Parse command name */
              if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &value, NULL)) {
                if (value.amf0_type == RTMP2_AMF0_STRING) {
                  cmd_name = g_strdup (value.value.string);
                  g_free (value.value.string);
                  GST_INFO ("Received command: %s (connect=%d, publish=%d)",
                      cmd_name, client->connect_received, client->publish_received);
                  
                  gint publish_cmp = g_strcmp0 (cmd_name, "publish");
                  GST_INFO ("Comparing cmd_name='%s' with 'publish': result=%d", cmd_name, publish_cmp);
                  
                  if (publish_cmp == 0) {
                    GST_INFO ("Handling publish command");
                    if (rtmp2_client_parse_publish (client, cmd_data, cmd_size, error)) {
                      GST_INFO ("Publish command parsed successfully, state=%d, client now publishing", client->state);
                    } else {
                      GST_WARNING ("Failed to parse publish command: %s",
                          error && *error ? (*error)->message : "Unknown");
                    }
                  } else if (g_strcmp0 (cmd_name, "releaseStream") == 0) {
                    Rtmp2AmfValue value;
                    gdouble transaction_id = 0.0;
                    gchar *stream_name = NULL;

                    GST_INFO ("Handling releaseStream");
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &value, NULL)) {
                      if (value.amf0_type == RTMP2_AMF0_NUMBER)
                        transaction_id = value.value.number;
                      rtmp2_amf_value_free (&value);
                    }
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &value, NULL))
                      rtmp2_amf_value_free (&value);
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &value, NULL)) {
                      if (value.amf0_type == RTMP2_AMF0_STRING)
                        stream_name = g_strdup (value.value.string);
                      rtmp2_amf_value_free (&value);
                    }

                    if (stream_name) {
                      g_free (client->stream_key);
                      client->stream_key = g_strdup (stream_name);
                    }

                    GST_INFO ("Sending releaseStream result (txn=%.0f)", transaction_id);
                    if (!rtmp2_client_send_release_stream_result (client,
                            transaction_id, error)) {
                      GST_WARNING ("Failed to send releaseStream result");
                    } else {
                      GST_INFO ("releaseStream result sent successfully");
                    }
                    g_free (stream_name);
                  } else if (g_strcmp0 (cmd_name, "FCPublish") == 0) {
                    Rtmp2AmfValue value;
                    gdouble transaction_id = 0.0;
                    gchar *stream_name = NULL;

                    GST_INFO ("Handling FCPublish");
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &value, NULL)) {
                      if (value.amf0_type == RTMP2_AMF0_NUMBER)
                        transaction_id = value.value.number;
                      rtmp2_amf_value_free (&value);
                    }
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &value, NULL))
                      rtmp2_amf_value_free (&value);
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &value, NULL)) {
                      if (value.amf0_type == RTMP2_AMF0_STRING)
                        stream_name = g_strdup (value.value.string);
                      rtmp2_amf_value_free (&value);
                    }

                    if (!stream_name && client->stream_key)
                      stream_name = g_strdup (client->stream_key);
                    else if (stream_name) {
                      g_free (client->stream_key);
                      client->stream_key = g_strdup (stream_name);
                    }

                    GST_INFO ("Sending FCPublish responses");
                    if (!rtmp2_client_send_on_fc_publish (client, stream_name,
                            error)) {
                      GST_WARNING ("Failed to send onFCPublish");
                    } else {
                      GST_INFO ("onFCPublish sent successfully");
                    }
                    if (!rtmp2_client_send_release_stream_result (client,
                            transaction_id, error)) {
                      GST_WARNING ("Failed to ack FCPublish");
                    } else {
                      GST_INFO ("FCPublish ack sent successfully");
                    }

                    if (stream_name)
                      g_free (stream_name);
                  } else if (g_strcmp0 (cmd_name, "createStream") == 0) {
                    GST_INFO ("Received createStream command - sending response");
                    gdouble transaction_id = 1.0;
                    Rtmp2AmfValue txn_value;
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &txn_value, NULL)) {
                      if (txn_value.amf0_type == RTMP2_AMF0_NUMBER)
                        transaction_id = txn_value.value.number;
                      rtmp2_amf_value_free (&txn_value);
                    }
                    /* Consume command object if present */
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &txn_value, NULL)) {
                      rtmp2_amf_value_free (&txn_value);
                    }
                    GST_INFO ("Sending createStream result (txn=%.0f)", transaction_id);
                    if (!rtmp2_client_send_create_stream_result (client, transaction_id, error)) {
                      GST_WARNING ("Failed to send createStream result");
                    } else {
                      client->stream_id = 1;
                      GST_INFO ("createStream result sent successfully (stream_id=%d)", client->stream_id);
                    }
                  } else if (g_strcmp0 (cmd_name, "_checkbw") == 0 ||
                      g_strcmp0 (cmd_name, "checkbw") == 0) {
                    GST_INFO ("Handling %s command", cmd_name);
                    gdouble transaction_id = 0.0;
                    Rtmp2AmfValue txn_value;
                    
                    /* Parse transaction ID */
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &txn_value, NULL)) {
                      if (txn_value.amf0_type == RTMP2_AMF0_NUMBER) {
                        transaction_id = txn_value.value.number;
                        GST_INFO ("_checkbw transaction_id: %.3f", transaction_id);
                      }
                      rtmp2_amf_value_free (&txn_value);
                    }
                    
                    /* Consume command object if present */
                    if (rtmp2_amf0_parse (&cmd_ptr, &cmd_remaining, &txn_value, NULL)) {
                      rtmp2_amf_value_free (&txn_value);
                    }
                    
                    GST_INFO ("Sending _checkbw result");
                    if (!rtmp2_client_send_check_bw_result (client,
                            transaction_id, error)) {
                      GST_WARNING ("Failed to respond to _checkbw");
                    } else {
                      GST_INFO ("_checkbw result sent successfully");
                    }
                  } else {
                    GST_INFO ("Unknown command: %s (connect=%d, publish=%d)",
                        cmd_name, client->connect_received, client->publish_received);
                  }
                  g_free (cmd_name);
                }
              }
            }
          } else {
            GST_DEBUG ("Unknown command (connect=%d, publish=%d)",
                client->connect_received, client->publish_received);
          }
          gst_buffer_unmap (msg->buffer, &map);
        } else {
          GST_WARNING ("Failed to map message buffer");
        }
        break;
      }

      case RTMP2_MESSAGE_AMF0_METADATA:
      case RTMP2_MESSAGE_AMF3_METADATA:
        /* Enhanced metadata - handle if needed */
        break;

      case RTMP2_MESSAGE_VIDEO:
      case RTMP2_MESSAGE_AUDIO:
        GST_DEBUG ("Processing %s message (type=%d, length=%u, state=%d)",
            msg->message_type == RTMP2_MESSAGE_VIDEO ? "video" : "audio",
            msg->message_type, msg->message_length, client->state);
        if (client->state == RTMP2_CLIENT_STATE_PUBLISHING) {
          /* Create FLV tag from RTMP message data */
          /* RTMP video/audio messages contain FLV tag body (without 11-byte header) */
          /* We need to reconstruct the full FLV tag for downstream elements */
          
          GstBuffer *flv_tag_buffer = gst_buffer_new_allocate (NULL, 11 + msg->message_length + 4, NULL);
          GstMapInfo tag_map, msg_map;
          
          if (gst_buffer_map (flv_tag_buffer, &tag_map, GST_MAP_WRITE) &&
              gst_buffer_map (msg->buffer, &msg_map, GST_MAP_READ)) {
            
            guint8 *ptr = tag_map.data;
            
            /* FLV tag header (11 bytes) */
            *ptr++ = msg->message_type; /* Tag type: 8=audio, 9=video */
            
            /* Data size (24-bit big-endian) */
            *ptr++ = (msg->message_length >> 16) & 0xff;
            *ptr++ = (msg->message_length >> 8) & 0xff;
            *ptr++ = msg->message_length & 0xff;
            
            /* Timestamp (24-bit big-endian) + extended timestamp */
            guint32 ts = msg->timestamp;
            *ptr++ = (ts >> 16) & 0xff;
            *ptr++ = (ts >> 8) & 0xff;
            *ptr++ = ts & 0xff;
            *ptr++ = (ts >> 24) & 0xff; /* Extended timestamp */
            
            /* Stream ID (24-bit, always 0) */
            *ptr++ = 0;
            *ptr++ = 0;
            *ptr++ = 0;
            
            /* Copy tag body data */
            memcpy (ptr, msg_map.data, msg->message_length);
            ptr += msg->message_length;
            
            /* Previous tag size (32-bit big-endian) */
            guint32 prev_tag_size = 11 + msg->message_length;
            *ptr++ = (prev_tag_size >> 24) & 0xff;
            *ptr++ = (prev_tag_size >> 16) & 0xff;
            *ptr++ = (prev_tag_size >> 8) & 0xff;
            *ptr++ = prev_tag_size & 0xff;
            
            gst_buffer_unmap (msg->buffer, &msg_map);
            gst_buffer_unmap (flv_tag_buffer, &tag_map);
            
            /* Create FLV tag wrapper and add to pending */
            Rtmp2FlvTag *tag = g_new0 (Rtmp2FlvTag, 1);
            tag->tag_type = msg->message_type == RTMP2_MESSAGE_VIDEO ? 
                RTMP2_FLV_TAG_VIDEO : RTMP2_FLV_TAG_AUDIO;
            tag->timestamp = msg->timestamp;
            tag->data_size = 11 + msg->message_length + 4;
            tag->data = flv_tag_buffer; /* Transfer ownership */
            
            client->flv_parser.pending_tags = 
                g_list_append (client->flv_parser.pending_tags, tag);
            
            GST_DEBUG ("Created FLV tag: type=%s, size=%u, timestamp=%u",
                tag->tag_type == RTMP2_FLV_TAG_VIDEO ? "video" : "audio",
                tag->data_size, tag->timestamp);
          } else {
            gst_buffer_unref (flv_tag_buffer);
          }
        } else {
          GST_WARNING ("Received media but state is not PUBLISHING (state=%d)", client->state);
        }
        break;
    }
  }

  if (messages) {
    GList *l;
    /* Free all messages - they were already removed from hash table via steal() */
    for (l = messages; l; l = l->next) {
      Rtmp2ChunkMessage *msg = (Rtmp2ChunkMessage *) l->data;
      rtmp2_chunk_message_free (msg);
    }
    g_list_free (messages);
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
rtmp2_client_send_set_chunk_size (Rtmp2Client * client, guint32 size,
    GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 basic_header = 0x02;  /* Chunk stream ID 2, type 0 */
  guint8 message_type = RTMP2_MESSAGE_SET_CHUNK_SIZE;

  write_uint8 (ba, basic_header);
  write_uint24_be (ba, 0);     /* timestamp */
  write_uint24_be (ba, 4);     /* message length */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);     /* message stream ID */
  write_uint32_be (ba, size);  /* chunk size */

  gsize bytes_written;
  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);

  if (ret) {
    client->chunk_parser.config.chunk_size = size;
  }

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
  guint8 message_type = client->supports_amf3 ?
      RTMP2_MESSAGE_AMF3_COMMAND : RTMP2_MESSAGE_AMF0_COMMAND;
  guint32 timestamp = 0;
  gsize bytes_written;

  /* Write basic header and message header (type 0 for first message) */
  guint8 basic_header = 0x03;  /* Chunk stream ID 3, type 0 */
  write_uint8 (ba, basic_header);
  write_uint24_be (ba, timestamp);
  
  /* Write message length (will update later) */
  gsize msg_start = ba->len;
  write_uint24_be (ba, 0);     /* message length placeholder */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);     /* message stream ID */

  /* Write AMF0 command response */
  if (client->supports_amf3) {
    /* AMF3 format selector byte */
    write_uint8 (ba, 0);       /* Format 0 = AMF0 */
  }

  rtmp2_enhanced_send_connect_result (ba, client->server_caps, client->connect_transaction_id, error);

  /* Update message length (message type + message stream ID + AMF data) */
guint32 msg_len = ba->len - msg_start - 8;  /* Exclude placeholder + message header bytes */
  ba->data[msg_start] = (msg_len >> 16) & 0xff;
  ba->data[msg_start + 1] = (msg_len >> 8) & 0xff;
  ba->data[msg_start + 2] = msg_len & 0xff;

  GST_DEBUG ("Sending connect result: %u bytes (message length=%u)",
      ba->len, msg_len);
  GST_DEBUG ("Connect result hex dump (first 32 bytes): %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
      ba->len > 0 ? ba->data[0] : 0, ba->len > 1 ? ba->data[1] : 0, ba->len > 2 ? ba->data[2] : 0, ba->len > 3 ? ba->data[3] : 0,
      ba->len > 4 ? ba->data[4] : 0, ba->len > 5 ? ba->data[5] : 0, ba->len > 6 ? ba->data[6] : 0, ba->len > 7 ? ba->data[7] : 0,
      ba->len > 8 ? ba->data[8] : 0, ba->len > 9 ? ba->data[9] : 0, ba->len > 10 ? ba->data[10] : 0, ba->len > 11 ? ba->data[11] : 0,
      ba->len > 12 ? ba->data[12] : 0, ba->len > 13 ? ba->data[13] : 0, ba->len > 14 ? ba->data[14] : 0, ba->len > 15 ? ba->data[15] : 0,
      ba->len > 16 ? ba->data[16] : 0, ba->len > 17 ? ba->data[17] : 0, ba->len > 18 ? ba->data[18] : 0, ba->len > 19 ? ba->data[19] : 0,
      ba->len > 20 ? ba->data[20] : 0, ba->len > 21 ? ba->data[21] : 0, ba->len > 22 ? ba->data[22] : 0, ba->len > 23 ? ba->data[23] : 0,
      ba->len > 24 ? ba->data[24] : 0, ba->len > 25 ? ba->data[25] : 0, ba->len > 26 ? ba->data[26] : 0, ba->len > 27 ? ba->data[27] : 0,
      ba->len > 28 ? ba->data[28] : 0, ba->len > 29 ? ba->data[29] : 0, ba->len > 30 ? ba->data[30] : 0, ba->len > 31 ? ba->data[31] : 0);
  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  if (!ret) {
    GST_WARNING ("Failed to send connect result: %s",
        error && *error ? (*error)->message : "Unknown");
  } else {
    GST_DEBUG ("Connect result sent successfully: %zu bytes written", bytes_written);
  }
  g_byte_array_free (ba, TRUE);

  if (ret)
    client->state = RTMP2_CLIENT_STATE_CONNECTED;

  return ret;
}

gboolean
rtmp2_client_send_create_stream_result (Rtmp2Client * client, gdouble transaction_id, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 basic_header = 0x03;  /* Chunk stream ID 3, type 0 */
  guint8 message_type = RTMP2_MESSAGE_AMF0_COMMAND;
  gsize bytes_written;

  /* Write basic header */
  write_uint8 (ba, basic_header);

  /* Write message header (type 0: 11 bytes) */
  write_uint24_be (ba, 0);     /* timestamp */
  
  /* Write message length (will update later) */
  gsize msg_start = ba->len;
  write_uint24_be (ba, 0);     /* message length placeholder */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, 0);     /* message stream ID */

  /* Write AMF0 command response: "_result", transaction_id, null, stream_id */
  guint8 amf0_string = RTMP2_AMF0_STRING;
  guint8 amf0_number = RTMP2_AMF0_NUMBER;
  guint8 amf0_null = RTMP2_AMF0_NULL;
  
  /* "_result" */
  g_byte_array_append (ba, &amf0_string, 1);
  rtmp2_amf0_write_string (ba, "_result");
  
  /* Transaction ID */
  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, transaction_id);
  
  /* Null (command info) */
  g_byte_array_append (ba, &amf0_null, 1);
  
  /* Stream ID (1.0) */
  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, 1.0);

  /* Update message length */
  guint32 msg_len = ba->len - msg_start - 8;
  ba->data[msg_start] = (msg_len >> 16) & 0xff;
  ba->data[msg_start + 1] = (msg_len >> 8) & 0xff;
  ba->data[msg_start + 2] = msg_len & 0xff;

  GST_DEBUG ("Sending createStream result: %u bytes (message length=%u)",
      ba->len, msg_len);
  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);

  if (ret && G_IS_OUTPUT_STREAM (client->output_stream)) {
    g_output_stream_flush (client->output_stream, NULL, NULL);
  }

  return ret;
}

gboolean
rtmp2_client_send_on_status (Rtmp2Client * client, guint8 chunk_stream_id,
    guint32 message_stream_id, const gchar * level, const gchar * code,
    const gchar * description, GError ** error)
{
  GByteArray *ba = g_byte_array_new ();
  guint8 message_type = RTMP2_MESSAGE_AMF0_COMMAND;
  guint32 timestamp = 0;
  gsize bytes_written;

  /* Chunk type 0 header so that message stream ID is always defined */
  guint8 basic_header = chunk_stream_id & 0x3f;
  write_uint8 (ba, basic_header);
  write_uint24_be (ba, timestamp);

  gsize msg_start = ba->len;
  write_uint24_be (ba, 0);     /* placeholder */
  write_uint8 (ba, message_type);
  write_uint32_le (ba, message_stream_id);

  guint8 amf0_string = RTMP2_AMF0_STRING;
  guint8 amf0_number = RTMP2_AMF0_NUMBER;
  guint8 amf0_null = RTMP2_AMF0_NULL;
  guint8 amf0_object = RTMP2_AMF0_OBJECT;

  g_byte_array_append (ba, &amf0_string, 1);
  rtmp2_amf0_write_string (ba, "onStatus");

  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, 0.0);

  g_byte_array_append (ba, &amf0_null, 1);
  g_byte_array_append (ba, &amf0_object, 1);

  rtmp2_amf0_write_object_property (ba, "level", level);
  rtmp2_amf0_write_object_property (ba, "code", code);
  rtmp2_amf0_write_object_property (ba, "description", description);

  rtmp2_amf0_write_object_end (ba);

  guint32 msg_len = ba->len - msg_start - 8;
  ba->data[msg_start] = (msg_len >> 16) & 0xff;
  ba->data[msg_start + 1] = (msg_len >> 8) & 0xff;
  ba->data[msg_start + 2] = msg_len & 0xff;

  GST_DEBUG ("Sending onStatus: %u bytes (message length=%u)",
      ba->len, msg_len);
  gboolean ret = g_output_stream_write_all (client->output_stream, ba->data,
      ba->len, &bytes_written, NULL, error);
  g_byte_array_free (ba, TRUE);
  return ret;
}

gboolean
rtmp2_client_send_publish_result (Rtmp2Client * client, GError ** error)
{
  if (client->stream_id == 0)
    client->stream_id = 1;

  if (!rtmp2_client_send_user_control (client,
          RTMP2_USER_CONTROL_STREAM_BEGIN, client->stream_id, error)) {
    GST_WARNING ("Failed to send StreamBegin user control message");
    return FALSE;
  }

  if (!rtmp2_client_send_on_status (client, 5, client->stream_id,
          "status", "NetStream.Publish.Start",
          "Publishing started.", error)) {
    GST_WARNING ("Failed to send NetStream publish status");
    return FALSE;
  }

  client->state = RTMP2_CLIENT_STATE_PUBLISHING;
  if (G_IS_OUTPUT_STREAM (client->output_stream)) {
    g_output_stream_flush (client->output_stream, NULL, NULL);
  }
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
          &client->connect_transaction_id, error)) {
    return FALSE;
  }
  GST_DEBUG ("Connect transaction ID: %.3f", client->connect_transaction_id);

  client->supports_amf3 = client->client_caps->supports_amf3;
  client->server_caps->supports_amf3 = client->supports_amf3;

  if (src && src->application) {
    client->application = g_strdup (src->application);
  }

  client->connect_received = TRUE;
  
  /* Send window ack size, peer bandwidth, and set chunk size first */
  GST_DEBUG ("Sending window ack size");
  if (!rtmp2_client_send_window_ack_size (client, 2500000, error)) {
    GST_WARNING ("Failed to send window ack size: %s",
        error && *error ? (*error)->message : "Unknown");
  } else {
    GST_DEBUG ("Window ack size sent successfully");
  }
  GST_DEBUG ("Sending peer bandwidth");
  if (!rtmp2_client_send_peer_bandwidth (client, 2500000, error)) {
    GST_WARNING ("Failed to send peer bandwidth: %s",
        error && *error ? (*error)->message : "Unknown");
  } else {
    GST_DEBUG ("Peer bandwidth sent successfully");
  }
  GST_DEBUG ("Sending set chunk size");
  if (!rtmp2_client_send_set_chunk_size (client, 4096, error)) {
    GST_WARNING ("Failed to send set chunk size: %s",
        error && *error ? (*error)->message : "Unknown");
  } else {
    GST_DEBUG ("Set chunk size sent successfully");
  }

  /* Now send the connect result so FFmpeg sees the control packets first */
  if (!rtmp2_client_send_connect_result (client, error)) {
    GST_WARNING ("Failed to send connect result");
    return FALSE;
  }
  /* Flush after connect result */
  if (G_IS_OUTPUT_STREAM (client->output_stream)) {
    g_output_stream_flush (client->output_stream, NULL, NULL);
  }
  
  /* Notify bandwidth check complete */
  if (!rtmp2_client_send_on_bw_done (client, error)) {
    GST_WARNING ("Failed to send onBWDone message");
  }

  /* Send onStatus message after all control messages */
  if (!rtmp2_client_send_on_status (client, 3, 0,
          "status", "NetConnection.Connect.Success",
          "Connection succeeded.", error)) {
    GST_WARNING ("Failed to send onStatus message");
  } else {
    GST_DEBUG ("onStatus sent successfully");
  }

  /* Signal stream begin (stream ID 0 for the NetConnection) */
  if (!rtmp2_client_send_user_control (client,
          RTMP2_USER_CONTROL_STREAM_BEGIN, 0, error)) {
    GST_WARNING ("Failed to send StreamBegin for NetConnection");
  } else {
    GST_DEBUG ("StreamBegin sent successfully");
  }
  
  /* Final flush */
  if (G_IS_OUTPUT_STREAM (client->output_stream)) {
    GError *flush_error = NULL;
    if (!g_output_stream_flush (client->output_stream, NULL, &flush_error)) {
      GST_WARNING ("Failed to flush output stream: %s",
          flush_error ? flush_error->message : "Unknown");
      if (flush_error)
        g_error_free (flush_error);
    } else {
      GST_DEBUG ("Output stream flushed successfully");
    }
  }

  GST_DEBUG ("parse_connect completed, returning TRUE");
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
  if (!rtmp2_client_send_publish_result (client, error)) {
    return FALSE;
  }

  return TRUE;
}

