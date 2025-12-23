/* GStreamer RTMP2 Server Source Element
 * Copyright (C) 2025 Yaron Torbaty <yarontorbaty@gmail.com>
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

/**
 * SECTION:element-rtmp2serversrc
 * @title: rtmp2serversrc
 * @short_description: RTMP server source element
 *
 * rtmp2serversrc is a source element that listens for incoming RTMP
 * connections on a configurable port. When a client connects and starts
 * publishing, the element outputs the received audio/video data as FLV.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv
 * ]|
 * Save incoming RTMP stream to file.
 *
 * |[
 * gst-launch-1.0 rtmp2serversrc port=1935 ! flvdemux name=demux \
 *   demux.video ! queue ! h264parse ! mpegtsmux name=mux ! srtsink uri="srt://:9000" \
 *   demux.audio ! queue ! aacparse ! mux.
 * ]|
 * Re-stream RTMP to SRT.
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtmp2serversrc.h"
#include "rtmp/rtmphandshake.h"
#include "rtmp/rtmpmessage.h"
#include "rtmp/amf.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_rtmp2_server_src_debug);
#define GST_CAT_DEFAULT gst_rtmp2_server_src_debug

enum {
  PROP_0,
  PROP_HOST,
  PROP_PORT,
  PROP_APPLICATION,
  PROP_STREAM_KEY,
  PROP_TIMEOUT,
  PROP_LOOP,
};

/* Always pad template - raw FLV output */
static GstStaticPadTemplate src_template = 
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-flv"));

/* Forward declarations */
static void gst_rtmp2_server_src_finalize (GObject *object);
static void gst_rtmp2_server_src_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_rtmp2_server_src_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_rtmp2_server_src_change_state (GstElement *element,
    GstStateChange transition);
static void gst_rtmp2_server_src_loop (gpointer user_data);
static gboolean on_incoming_connection (GSocketService *service,
    GSocketConnection *connection, GObject *source_object, gpointer user_data);

#define gst_rtmp2_server_src_parent_class parent_class
G_DEFINE_TYPE (GstRtmp2ServerSrc, gst_rtmp2_server_src, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtmp2serversrc, "rtmp2serversrc",
    GST_RANK_NONE, GST_TYPE_RTMP2_SERVER_SRC, rtmp2_element_init (plugin));

/* Session management */
static ServerSession *
server_session_new (GstRtmp2ServerSrc *src, GSocketConnection *socket_connection)
{
  ServerSession *session = g_new0 (ServerSession, 1);
  session->socket_connection = g_object_ref (socket_connection);
  session->state = SERVER_SESSION_STATE_NEW;
  session->stream_id = 1;
  session->tag_queue = g_queue_new ();
  g_mutex_init (&session->queue_lock);
  session->src = src;
  return session;
}

static void
server_session_free (ServerSession *session)
{
  if (!session)
    return;

  if (session->connection) {
    gst_rtmp_connection_close (session->connection);
    g_object_unref (session->connection);
  }

  g_clear_object (&session->socket_connection);

  g_free (session->app_name);
  g_free (session->stream_key);

  g_mutex_lock (&session->queue_lock);
  while (!g_queue_is_empty (session->tag_queue)) {
    Rtmp2FlvTag *tag = g_queue_pop_head (session->tag_queue);
    rtmp2_flv_tag_free (tag);
  }
  g_queue_free (session->tag_queue);
  g_mutex_unlock (&session->queue_lock);
  g_mutex_clear (&session->queue_lock);

  g_free (session);
}

/* Command handlers */
static void
on_connect_command (const gchar *command_name, GPtrArray *args, gpointer user_data)
{
  ServerSession *session = user_data;
  const GstAmfNode *command_obj;
  gdouble transaction_id = 1.0;

  GST_DEBUG ("Received connect command");

  if (args && args->len > 0) {
    command_obj = g_ptr_array_index (args, 0);
    gst_rtmp_enhanced_caps_parse (command_obj, &session->enhanced_caps);

    /* Extract app name */
    const GstAmfNode *app_node = gst_amf_node_get_field (command_obj, "app");
    if (app_node && gst_amf_node_get_type (app_node) == GST_AMF_TYPE_STRING) {
      g_free (session->app_name);
      session->app_name = gst_amf_node_get_string (app_node, NULL);
    }
  }

  /* Send connect result */
  gst_rtmp_server_send_connect_result (session->connection, transaction_id,
      &session->enhanced_caps);

  session->state = SERVER_SESSION_STATE_CONNECTED;
  GST_INFO ("Client connected, app=%s", session->app_name ? session->app_name : "");
}

static void
on_create_stream_command (const gchar *command_name, GPtrArray *args, gpointer user_data)
{
  ServerSession *session = user_data;
  gdouble transaction_id = 4.0; /* Default */
  
  GST_DEBUG ("Received createStream command");

  /* Transaction ID is the first arg in command args */
  /* Actually for expected commands, args contains the objects after command name */
  /* Transaction ID comes before the args passed here, extracted by connection */
  
  /* Send createStream result with stream ID 1 */
  gst_rtmp_server_send_create_stream_result (session->connection, transaction_id, session->stream_id);
  GST_INFO ("Sent createStream result with stream_id=%u", session->stream_id);
}

static void
on_release_stream_command (const gchar *command_name, GPtrArray *args, gpointer user_data)
{
  ServerSession *session = user_data;
  gdouble transaction_id = 2.0;

  GST_INFO ("Received releaseStream command");

  /* Extract transaction ID from args if available */
  /* Send _result response to acknowledge */
  gst_rtmp_server_send_release_stream_result (session->connection, transaction_id);
  GST_INFO ("Sent releaseStream result");
}

static void
on_fcpublish_command (const gchar *command_name, GPtrArray *args, gpointer user_data)
{
  ServerSession *session = user_data;
  gdouble transaction_id = 3.0;

  GST_INFO ("Received FCPublish command");

  /* Send _result to acknowledge - some clients wait for this */
  gst_rtmp_server_send_fcpublish_result (session->connection, transaction_id);
  GST_INFO ("Sent FCPublish result");
}

static void
on_publish_command (const gchar *command_name, GPtrArray *args, gpointer user_data)
{
  ServerSession *session = user_data;
  
  GST_DEBUG ("Received publish command");

  if (args && args->len > 0) {
    const GstAmfNode *stream_name = g_ptr_array_index (args, 0);
    if (stream_name && gst_amf_node_get_type (stream_name) == GST_AMF_TYPE_STRING) {
      g_free (session->stream_key);
      session->stream_key = gst_amf_node_get_string (stream_name, NULL);
    }
  }

  /* Send publish start */
  gst_rtmp_server_send_publish_start (session->connection, session->stream_id);

  session->state = SERVER_SESSION_STATE_PUBLISHING;
  GST_INFO ("Client publishing, stream=%s", session->stream_key ? session->stream_key : "");
}

/* Media message handler */
static void
on_media_message (GstRtmpConnection *connection, GstBuffer *buffer, gpointer user_data)
{
  ServerSession *session = user_data;
  GstRtmpMeta *meta;
  Rtmp2FlvTag *tag;
  GstMapInfo map;
  GstClockTime dts;
  guint32 timestamp_ms;

  meta = gst_buffer_get_rtmp_meta (buffer);
  if (!meta) {
    GST_DEBUG ("Media message without RTMP meta");
    return;
  }

  /* Get the absolute timestamp from buffer DTS (set by rtmpchunkstream) */
  dts = GST_BUFFER_DTS (buffer);
  if (GST_CLOCK_TIME_IS_VALID (dts)) {
    timestamp_ms = (guint32) (dts / GST_MSECOND);
  } else {
    timestamp_ms = meta->ts_delta;  /* fallback to delta as absolute */
  }

  GST_LOG ("Received media message type=%d dts=%" GST_TIME_FORMAT " timestamp_ms=%u size=%" G_GSIZE_FORMAT,
      meta->type, GST_TIME_ARGS(dts), timestamp_ms, gst_buffer_get_size (buffer));

  /* Only process video and audio messages */
  if (meta->type != GST_RTMP_MESSAGE_TYPE_VIDEO &&
      meta->type != GST_RTMP_MESSAGE_TYPE_AUDIO &&
      meta->type != GST_RTMP_MESSAGE_TYPE_DATA_AMF0) {
    return;
  }

  /* Create FLV tag from RTMP message */
  tag = rtmp2_flv_tag_new ();
  
  if (meta->type == GST_RTMP_MESSAGE_TYPE_VIDEO) {
    tag->tag_type = RTMP2_FLV_TAG_VIDEO;
  } else if (meta->type == GST_RTMP_MESSAGE_TYPE_AUDIO) {
    tag->tag_type = RTMP2_FLV_TAG_AUDIO;
  } else {
    tag->tag_type = RTMP2_FLV_TAG_SCRIPT;
  }
  
  /* Use absolute timestamp from buffer DTS */
  tag->timestamp = timestamp_ms;
  tag->data_size = gst_buffer_get_size (buffer);
  tag->data = gst_buffer_ref (buffer);

  /* Parse video/audio codec info from first byte */
  if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    if (map.size > 0) {
      guint8 first_byte = map.data[0];
      
      if (tag->tag_type == RTMP2_FLV_TAG_VIDEO) {
        tag->video_keyframe = ((first_byte >> 4) & 0x0F) == 1;
        tag->video_codec = first_byte & 0x0F;
      } else if (tag->tag_type == RTMP2_FLV_TAG_AUDIO) {
        tag->audio_codec = (first_byte >> 4) & 0x0F;
      }
    }
    gst_buffer_unmap (buffer, &map);
  }

  /* Queue the tag */
  g_mutex_lock (&session->queue_lock);
  g_queue_push_tail (session->tag_queue, tag);
  g_mutex_unlock (&session->queue_lock);

  GST_LOG ("Queued %s tag, timestamp=%u, size=%u",
      tag->tag_type == RTMP2_FLV_TAG_VIDEO ? "video" :
      tag->tag_type == RTMP2_FLV_TAG_AUDIO ? "audio" : "data",
      tag->timestamp, tag->data_size);
}

/* Connection error handler */
static void
on_connection_error (GstRtmpConnection *connection, GError *error, gpointer user_data)
{
  ServerSession *session = user_data;
  
  GST_WARNING ("Connection error: %s", error->message);
  session->state = SERVER_SESSION_STATE_DISCONNECTED;
}

/* Handshake complete callback */
static void
on_handshake_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GIOStream *stream = G_IO_STREAM (source);
  ServerSession *session = user_data;
  GstRtmp2ServerSrc *src = session->src;
  GError *error = NULL;
  gboolean success;

  success = gst_rtmp_server_handshake_finish (stream, result, &error);
  if (!success) {
    GST_WARNING_OBJECT (src, "Handshake failed: %s", error->message);
    g_error_free (error);
    
    g_mutex_lock (&src->sessions_lock);
    src->sessions = g_list_remove (src->sessions, session);
    g_mutex_unlock (&src->sessions_lock);
    
    server_session_free (session);
    return;
  }

  GST_INFO_OBJECT (src, "Handshake completed");

  /* Create GstRtmpConnection from socket connection we stored earlier */
  session->connection = gst_rtmp_connection_new (session->socket_connection, NULL);

  /* Set up message handlers */
  gst_rtmp_connection_set_input_handler (session->connection,
      on_media_message, session, NULL);

  g_signal_connect (session->connection, "error",
      G_CALLBACK (on_connection_error), session);

  GST_INFO ("Setting up expected commands");

  /* Expect connect command */
  gst_rtmp_connection_expect_command (session->connection,
      on_connect_command, session, 0, "connect");

  /* Also expect releaseStream, FCPublish, createStream and publish 
   * Note: These all come on stream 0, and publish comes on stream 1 */
  gst_rtmp_connection_expect_command (session->connection,
      on_release_stream_command, session, 0, "releaseStream");
  gst_rtmp_connection_expect_command (session->connection,
      on_fcpublish_command, session, 0, "FCPublish");
  gst_rtmp_connection_expect_command (session->connection,
      on_create_stream_command, session, 0, "createStream");
  gst_rtmp_connection_expect_command (session->connection,
      on_publish_command, session, 1, "publish");

  GST_INFO ("All expected commands registered");

  /* Set as active session if none */
  g_mutex_lock (&src->sessions_lock);
  if (!src->active_session) {
    src->active_session = session;
  }
  g_mutex_unlock (&src->sessions_lock);
}

/* Incoming connection handler */
static gboolean
on_incoming_connection (GSocketService *service, GSocketConnection *connection,
    GObject *source_object, gpointer user_data)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (user_data);
  ServerSession *session;
  GIOStream *stream;

  GST_INFO_OBJECT (src, "New incoming connection");

  session = server_session_new (src, connection);
  
  g_mutex_lock (&src->sessions_lock);
  src->sessions = g_list_append (src->sessions, session);
  g_mutex_unlock (&src->sessions_lock);

  /* Start server handshake */
  stream = G_IO_STREAM (connection);
  gst_rtmp_server_handshake (stream, FALSE, NULL, on_handshake_done, session);

  return TRUE;
}

/* Task loop - pushes FLV data to srcpad */
static void
gst_rtmp2_server_src_loop (gpointer user_data)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (user_data);
  ServerSession *session;
  Rtmp2FlvTag *tag = NULL;
  GstBuffer *buffer;
  GstFlowReturn ret;

  g_mutex_lock (&src->sessions_lock);
  session = src->active_session;
  g_mutex_unlock (&src->sessions_lock);

  if (!session) {
    g_usleep (10000);  /* 10ms */
    return;
  }

  /* Push FLV file header on first data */
  if (!src->srcpad_started) {
    guint8 flv_header[13] = {
      'F', 'L', 'V',              /* Signature */
      0x01,                       /* Version */
      0x05,                       /* Flags: audio + video */
      0x00, 0x00, 0x00, 0x09,     /* Header size */
      0x00, 0x00, 0x00, 0x00      /* Previous tag size */
    };
    gchar *stream_id;

    src->stream_count++;
    stream_id = g_strdup_printf ("rtmp-stream-%u", src->stream_count);
    
    GST_INFO_OBJECT (src, "Starting new stream: %s", stream_id);
    
    gst_pad_push_event (src->srcpad, gst_event_new_stream_start (stream_id));
    gst_pad_push_event (src->srcpad, gst_event_new_caps (
        gst_caps_new_empty_simple ("video/x-flv")));

    {
      GstSegment segment;
      gst_segment_init (&segment, GST_FORMAT_BYTES);
      gst_pad_push_event (src->srcpad, gst_event_new_segment (&segment));
    }

    buffer = gst_buffer_new_allocate (NULL, 13, NULL);
    gst_buffer_fill (buffer, 0, flv_header, 13);
    gst_pad_push (src->srcpad, buffer);
    
    src->srcpad_started = TRUE;
    g_free (stream_id);
    GST_DEBUG_OBJECT (src, "Pushed FLV header");
  }

  /* Get next tag from queue */
  g_mutex_lock (&session->queue_lock);
  tag = g_queue_pop_head (session->tag_queue);
  g_mutex_unlock (&session->queue_lock);

  if (!tag) {
    /* Check for EOS */
    if (session->state == SERVER_SESSION_STATE_DISCONNECTED) {
      gint64 now = g_get_monotonic_time ();
      
      if (src->eos_wait_start == 0) {
        src->eos_wait_start = now;
        g_usleep (10000);
        return;
      }
      
      if ((now - src->eos_wait_start) < 100000) {  /* 100ms grace */
        g_usleep (10000);
        return;
      }

      if (src->loop) {
        /* Loop mode: reset and wait for new connection */
        GST_INFO_OBJECT (src, "Client disconnected, waiting for new connection (loop=true)");
        
        /* Send flush events to reset downstream state */
        gst_pad_push_event (src->srcpad, gst_event_new_flush_start ());
        gst_pad_push_event (src->srcpad, gst_event_new_flush_stop (TRUE));
        
        /* Clean up old session */
        g_mutex_lock (&src->sessions_lock);
        if (src->active_session) {
          src->sessions = g_list_remove (src->sessions, src->active_session);
          server_session_free (src->active_session);
          src->active_session = NULL;
        }
        g_mutex_unlock (&src->sessions_lock);
        
        /* Reset state for next connection */
        src->srcpad_started = FALSE;
        src->eos_wait_start = 0;
        src->have_video = FALSE;
        src->have_audio = FALSE;
        
        g_usleep (50000);  /* 50ms before checking for new connections */
        return;
      }

      GST_INFO_OBJECT (src, "Client disconnected, sending EOS");
      gst_pad_push_event (src->srcpad, gst_event_new_eos ());
      gst_task_pause (src->task);
      return;
    }
    
    src->eos_wait_start = 0;
    g_usleep (5000);  /* 5ms */
    return;
  }

  src->eos_wait_start = 0;

  /* Build FLV tag buffer */
  {
  GstMapInfo map;
    guint8 tag_header[11];
    guint32 prev_tag_size;
    GstBuffer *flv_buffer;
    gsize data_size;

    if (!gst_buffer_map (tag->data, &map, GST_MAP_READ)) {
      rtmp2_flv_tag_free (tag);
      return;
    }

    data_size = map.size;
    
    /* FLV tag header */
    tag_header[0] = tag->tag_type;
    tag_header[1] = (data_size >> 16) & 0xFF;
    tag_header[2] = (data_size >> 8) & 0xFF;
    tag_header[3] = data_size & 0xFF;
    tag_header[4] = (tag->timestamp >> 16) & 0xFF;
    tag_header[5] = (tag->timestamp >> 8) & 0xFF;
    tag_header[6] = tag->timestamp & 0xFF;
    tag_header[7] = (tag->timestamp >> 24) & 0xFF;  /* Extended timestamp */
    tag_header[8] = 0;  /* Stream ID (always 0) */
    tag_header[9] = 0;
    tag_header[10] = 0;

    prev_tag_size = 11 + data_size;

    /* Create buffer: header + data + prev_tag_size */
    flv_buffer = gst_buffer_new_allocate (NULL, 11 + data_size + 4, NULL);
    gst_buffer_fill (flv_buffer, 0, tag_header, 11);
    gst_buffer_fill (flv_buffer, 11, map.data, data_size);
    
    /* Previous tag size (big endian) */
    guint8 pts_bytes[4] = {
      (prev_tag_size >> 24) & 0xFF,
      (prev_tag_size >> 16) & 0xFF,
      (prev_tag_size >> 8) & 0xFF,
      prev_tag_size & 0xFF
    };
    gst_buffer_fill (flv_buffer, 11 + data_size, pts_bytes, 4);

    gst_buffer_unmap (tag->data, &map);

    /* Set buffer timestamp */
    GST_BUFFER_PTS (flv_buffer) = tag->timestamp * GST_MSECOND;

    ret = gst_pad_push (src->srcpad, flv_buffer);
  if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (src, "Pad push returned %s", gst_flow_get_name (ret));
    }
  }
  
  rtmp2_flv_tag_free (tag);
}

/* Event loop thread function - creates and runs the socket service */
static gpointer
event_loop_thread_func (gpointer user_data)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (user_data);
  GError *error = NULL;
  GInetAddress *addr;
  GSocketAddress *saddr;
  
  GST_INFO_OBJECT (src, "Event loop thread started");
  
  /* Push our context as thread default - socket service will use this */
  g_main_context_push_thread_default (src->context);

  /* Create socket service in THIS thread */
  src->service = g_socket_service_new ();

  addr = g_inet_address_new_from_string (src->host);
  if (!addr) {
    addr = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
  }

  saddr = g_inet_socket_address_new (addr, src->port);
  g_object_unref (addr);

  if (!g_socket_listener_add_address (G_SOCKET_LISTENER (src->service),
          saddr, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL, NULL, &error)) {
    GST_ERROR_OBJECT (src, "Failed to bind: %s", error->message);
    g_clear_error (&error);
    g_object_unref (saddr);
    g_clear_object (&src->service);
    src->start_error = TRUE;
    g_cond_signal (&src->start_cond);
    g_main_context_pop_thread_default (src->context);
    return NULL;
  }
  g_object_unref (saddr);

  g_signal_connect (src->service, "incoming",
      G_CALLBACK (on_incoming_connection), src);

  /* Start the service - signals will be delivered on this thread */
  g_socket_service_start (src->service);

  GST_INFO_OBJECT (src, "Server listening on %s:%u", src->host, src->port);

  /* Signal that startup is complete */
  g_mutex_lock (&src->start_lock);
  src->start_complete = TRUE;
  g_cond_signal (&src->start_cond);
  g_mutex_unlock (&src->start_lock);

  /* Run the event loop */
  while (src->running) {
    g_main_context_iteration (src->context, TRUE);
  }

  /* Cleanup */
  if (src->service) {
    g_socket_service_stop (src->service);
    g_clear_object (&src->service);
  }

  g_main_context_pop_thread_default (src->context);

  GST_INFO_OBJECT (src, "Event loop thread stopping");
  return NULL;
}

/* State change */
static gboolean
gst_rtmp2_server_src_start (GstRtmp2ServerSrc *src)
{
  GST_DEBUG_OBJECT (src, "Starting server on %s:%u", src->host, src->port);

  /* Create main context for socket service */
  src->context = g_main_context_new ();

  /* Initialize startup synchronization */
  src->start_complete = FALSE;
  src->start_error = FALSE;

  src->running = TRUE;

  /* Start event loop thread - it will create the socket service */
  src->thread = g_thread_new ("rtmp-event-loop", event_loop_thread_func, src);
  if (!src->thread) {
    GST_ERROR_OBJECT (src, "Failed to create event loop thread");
    g_main_context_unref (src->context);
    src->context = NULL;
    return FALSE;
  }

  /* Wait for the thread to finish startup */
  g_mutex_lock (&src->start_lock);
  while (!src->start_complete && !src->start_error) {
    g_cond_wait (&src->start_cond, &src->start_lock);
  }
  g_mutex_unlock (&src->start_lock);

  if (src->start_error) {
    GST_ERROR_OBJECT (src, "Failed to start server");
    src->running = FALSE;
    g_thread_join (src->thread);
    src->thread = NULL;
    g_main_context_unref (src->context);
    src->context = NULL;
    return FALSE;
  }

  GST_INFO_OBJECT (src, "Server started successfully");

  /* Start task */
  gst_task_start (src->task);

  return TRUE;
}

static gboolean
gst_rtmp2_server_src_stop (GstRtmp2ServerSrc *src)
{
  GST_DEBUG_OBJECT (src, "Stopping server");

  src->running = FALSE;

  /* Stop task */
  gst_task_stop (src->task);
  gst_task_join (src->task);

  /* Wake up event loop and wait for thread to finish */
  if (src->context)
    g_main_context_wakeup (src->context);
  if (src->thread) {
    g_thread_join (src->thread);
    src->thread = NULL;
  }

  /* Service is cleaned up by the event loop thread */
  src->service = NULL;

  /* Free context */
  if (src->context) {
    g_main_context_unref (src->context);
    src->context = NULL;
  }

  /* Free sessions */
  g_mutex_lock (&src->sessions_lock);
  g_list_free_full (src->sessions, (GDestroyNotify) server_session_free);
  src->sessions = NULL;
  src->active_session = NULL;
  g_mutex_unlock (&src->sessions_lock);

  src->srcpad_started = FALSE;

  return TRUE;
}

static GstStateChangeReturn
gst_rtmp2_server_src_change_state (GstElement *element, GstStateChange transition)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_rtmp2_server_src_start (src))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtmp2_server_src_stop (src);
      break;
    default:
      break;
  }

  return ret;
}

/* GObject methods */
static void
gst_rtmp2_server_src_class_init (GstRtmp2ServerSrcClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_rtmp2_server_src_set_property;
  gobject_class->get_property = gst_rtmp2_server_src_get_property;
  gobject_class->finalize = gst_rtmp2_server_src_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rtmp2_server_src_change_state);

  g_object_class_install_property (gobject_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "Address to bind to", "0.0.0.0",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_uint ("port", "Port",
          "TCP port to listen on", 1, 65535, 1935,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_APPLICATION,
      g_param_spec_string ("application", "Application",
          "RTMP application name", "live",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STREAM_KEY,
      g_param_spec_string ("stream-key", "Stream Key",
          "If set, only accept this stream key", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "Timeout",
          "Client timeout in seconds", 1, 3600, 30,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOOP,
      g_param_spec_boolean ("loop", "Loop",
          "Keep listening for new connections after client disconnects", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTMP2 Server Source",
      "Source/Network",
      "Receive audio/video stream from an RTMP client",
      "Yaron Torbaty <yarontorbaty@gmail.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  GST_DEBUG_CATEGORY_INIT (gst_rtmp2_server_src_debug, "rtmp2serversrc", 0,
      "RTMP2 Server Source");
}

static void
gst_rtmp2_server_src_init (GstRtmp2ServerSrc *src)
{
  src->host = g_strdup ("0.0.0.0");
  src->port = 1935;
  src->application = g_strdup ("live");
  src->stream_key = NULL;
  src->timeout = 30;

  src->service = NULL;
  src->context = NULL;
  src->thread = NULL;
    src->running = FALSE;

  /* Startup synchronization */
  g_mutex_init (&src->start_lock);
  g_cond_init (&src->start_cond);
  src->start_complete = FALSE;
  src->start_error = FALSE;

  src->sessions = NULL;
  g_mutex_init (&src->sessions_lock);
  src->active_session = NULL;

  src->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (src->srcpad);
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);
  src->srcpad_started = FALSE;
  src->eos_wait_start = 0;

  g_rec_mutex_init (&src->task_lock);
  src->task = gst_task_new ((GstTaskFunction) gst_rtmp2_server_src_loop, src, NULL);
  gst_task_set_lock (src->task, &src->task_lock);

  src->have_video = FALSE;
  src->have_audio = FALSE;
}

static void
gst_rtmp2_server_src_finalize (GObject *object)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (object);

  g_free (src->host);
  g_free (src->application);
  g_free (src->stream_key);

  g_mutex_clear (&src->sessions_lock);
  g_mutex_clear (&src->start_lock);
  g_cond_clear (&src->start_cond);
  g_rec_mutex_clear (&src->task_lock);

  if (src->task) {
    gst_object_unref (src->task);
    src->task = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtmp2_server_src_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (object);

  switch (prop_id) {
    case PROP_HOST:
      g_free (src->host);
      src->host = g_value_dup_string (value);
      break;
    case PROP_PORT:
      src->port = g_value_get_uint (value);
      break;
    case PROP_APPLICATION:
      g_free (src->application);
      src->application = g_value_dup_string (value);
      break;
    case PROP_STREAM_KEY:
      g_free (src->stream_key);
      src->stream_key = g_value_dup_string (value);
      break;
    case PROP_TIMEOUT:
      src->timeout = g_value_get_uint (value);
      break;
    case PROP_LOOP:
      src->loop = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtmp2_server_src_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, src->host);
      break;
    case PROP_PORT:
      g_value_set_uint (value, src->port);
      break;
    case PROP_APPLICATION:
      g_value_set_string (value, src->application);
      break;
    case PROP_STREAM_KEY:
      g_value_set_string (value, src->stream_key);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint (value, src->timeout);
      break;
    case PROP_LOOP:
      g_value_set_boolean (value, src->loop);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
          }
}
