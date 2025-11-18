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

#ifndef __RTMP2_CLIENT_H__
#define __RTMP2_CLIENT_H__

#include <gst/gst.h>
#include <gio/gio.h>
#include "rtmp2handshake.h"
#include "rtmp2chunk.h"
#include "rtmp2chunk_v2.h"
#include "rtmp2flv.h"
#include "rtmp2amf.h"
#include "rtmp2enhanced.h"

G_BEGIN_DECLS

typedef enum {
  RTMP2_CLIENT_STATE_DISCONNECTED,
  RTMP2_CLIENT_STATE_HANDSHAKE,
  RTMP2_CLIENT_STATE_CONNECTING,
  RTMP2_CLIENT_STATE_CONNECTED,
  RTMP2_CLIENT_STATE_PUBLISHING,
  RTMP2_CLIENT_STATE_ERROR
} Rtmp2ClientState;

typedef struct _Rtmp2Client {
  GSocket *socket;
  GSocketConnection *connection;
  GIOStream *io_stream;
  GInputStream *input_stream;
  GInputStream *buffered_input;  /* Buffered wrapper - like bufio.Reader */
  GOutputStream *output_stream;
  GSource *read_source;
  GSource *timeout_source;
  GThread *read_thread;  /* Dedicated read thread like gortmplib goroutine */
  gboolean thread_running;
  
  Rtmp2ClientState state;
  Rtmp2Handshake handshake;
  Rtmp2ChunkParserV2 chunk_parser;
  Rtmp2FlvParser flv_parser;
  
      gchar *application;
      gchar *stream_key;
      gchar *tc_url;
      
      gdouble connect_transaction_id;
      
      gboolean handshake_complete;
      gboolean connect_received;
      gboolean publish_received;
  
  /* Enhanced RTMP support */
  Rtmp2EnhancedCapabilities *client_caps;
  Rtmp2EnhancedCapabilities *server_caps;
  gboolean supports_amf3;
  guint64 timestamp_nano_offset;
  
  guint64 last_activity;
  guint timeout_seconds;
  guint32 stream_id;
  
  gpointer user_data;
} Rtmp2Client;

Rtmp2Client *rtmp2_client_new (GSocketConnection *connection, GIOStream *io_stream);
void rtmp2_client_free (Rtmp2Client *client);
gboolean rtmp2_client_process_data (Rtmp2Client *client, GError **error);
gboolean rtmp2_client_start_reading (Rtmp2Client *client, GMainContext *context);

void rtmp2_client_debug_init (void);
gboolean rtmp2_client_send_handshake (Rtmp2Client *client, GError **error);
gboolean rtmp2_client_send_ack (Rtmp2Client *client, guint32 bytes, GError **error);
gboolean rtmp2_client_send_window_ack_size (Rtmp2Client *client, guint32 size, GError **error);
gboolean rtmp2_client_send_peer_bandwidth (Rtmp2Client *client, guint32 size, GError **error);
gboolean rtmp2_client_send_connect_result (Rtmp2Client *client, GError **error);
gboolean rtmp2_client_send_on_status (Rtmp2Client *client, guint8 chunk_stream_id, guint32 message_stream_id, const gchar *level, const gchar *code, const gchar *description, GError **error);
gboolean rtmp2_client_send_create_stream_result (Rtmp2Client *client, gdouble transaction_id, GError **error);
gboolean rtmp2_client_send_publish_result (Rtmp2Client *client, GError **error);
gboolean rtmp2_client_send_release_stream_result (Rtmp2Client *client, gdouble transaction_id, GError **error);
gboolean rtmp2_client_send_on_fc_publish (Rtmp2Client *client, const gchar *stream_name, GError **error);
gboolean rtmp2_client_send_check_bw_result (Rtmp2Client *client, gdouble transaction_id, GError **error);
gboolean rtmp2_client_parse_connect (Rtmp2Client *client, const guint8 *data, gsize size, GError **error);
gboolean rtmp2_client_parse_publish (Rtmp2Client *client, const guint8 *data, gsize size, GError **error);

G_END_DECLS

#endif /* __RTMP2_CLIENT_H__ */

