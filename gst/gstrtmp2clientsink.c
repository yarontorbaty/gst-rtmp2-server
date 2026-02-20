/* GStreamer Enhanced RTMP Client Sink Element
 * Copyright (C) 2026 Yaron Torbaty <yarontorbaty@gmail.com>
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
 * SECTION:element-ertmp2sink
 * @title: ertmp2sink
 * @short_description: Enhanced RTMP client sink with HEVC support
 *
 * ertmp2sink pushes audio/video over Enhanced RTMP (E-RTMP) to a remote
 * server. It negotiates E-RTMP capabilities (capsEx, videoFourCcInfoMap)
 * during the RTMP connect handshake, enabling HEVC/H.265 streaming to
 * ingest points that support Enhanced RTMP.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 filesrc location=input.flv ! flvdemux name=d \
 *   d.video ! queue ! h265parse ! flvmux is-live=true name=mux ! ertmp2sink location=rtmp://server/live/key \
 *   d.audio ! queue ! aacparse ! mux.
 * ]|
 * Push an HEVC stream via Enhanced RTMP.
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtmp2clientsink.h"
#include "rtmp/rtmpclient.h"
#include "rtmp/rtmpconnection.h"
#include "rtmp/rtmpmessage.h"

#include <gst/base/gstbasesink.h>
#include <string.h>
#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (gst_ertmp2_client_sink_debug);
#define GST_CAT_DEFAULT gst_ertmp2_client_sink_debug

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_TIMEOUT,
};

#define DEFAULT_LOCATION NULL
#define DEFAULT_TIMEOUT 30

struct _GstErtmp2ClientSink
{
  GstBaseSink parent;

  /* Properties */
  gchar *location;
  gint timeout;

  /* Parsed from location */
  GstRtmpLocation rtmp_location;

  /* Connection state */
  GstRtmpConnection *connection;
  guint32 stream_id;
  GMainContext *context;
  GMainLoop *loop;
  GThread *loop_thread;
  GMutex lock;
  GCond cond;
  gboolean connected;
  gboolean publish_started;
  GError *connect_error;

  /* FLV parsing state */
  gboolean header_sent;
  guint32 base_ts;
  gboolean base_ts_set;

};

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-flv"));

G_DEFINE_TYPE (GstErtmp2ClientSink, gst_ertmp2_client_sink,
    GST_TYPE_BASE_SINK);

static void gst_ertmp2_client_sink_finalize (GObject * object);
static void gst_ertmp2_client_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ertmp2_client_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_ertmp2_client_sink_start (GstBaseSink * sink);
static gboolean gst_ertmp2_client_sink_stop (GstBaseSink * sink);
static GstFlowReturn gst_ertmp2_client_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static gboolean gst_ertmp2_client_sink_event (GstBaseSink * sink,
    GstEvent * event);

/* Forward declarations for async connection flow */
static void ertmp_connect_done (GObject * source, GAsyncResult * result,
    gpointer user_data);
static void ertmp_publish_done (GObject * source, GAsyncResult * result,
    gpointer user_data);
static gpointer ertmp_loop_thread_func (gpointer user_data);
static gboolean parse_rtmp_location (GstErtmp2ClientSink * self);

static void
gst_ertmp2_client_sink_class_init (GstErtmp2ClientSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->finalize = gst_ertmp2_client_sink_finalize;
  gobject_class->set_property = gst_ertmp2_client_sink_set_property;
  gobject_class->get_property = gst_ertmp2_client_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "RTMP URL to push to (e.g. rtmp://server/live/streamkey)",
          DEFAULT_LOCATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_int ("timeout", "Timeout",
          "Connection timeout in seconds", 0, 120, DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Enhanced RTMP Client Sink",
      "Sink/Network",
      "Push audio/video via Enhanced RTMP (E-RTMP) with HEVC support",
      "Yaron Torbaty <yarontorbaty@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  basesink_class->start = gst_ertmp2_client_sink_start;
  basesink_class->stop = gst_ertmp2_client_sink_stop;
  basesink_class->render = gst_ertmp2_client_sink_render;
  basesink_class->event = gst_ertmp2_client_sink_event;

  GST_DEBUG_CATEGORY_INIT (gst_ertmp2_client_sink_debug, "ertmp2sink", 0,
      "Enhanced RTMP Client Sink");
}

static void
gst_ertmp2_client_sink_init (GstErtmp2ClientSink * self)
{
  self->location = NULL;
  self->timeout = DEFAULT_TIMEOUT;
  self->connection = NULL;
  self->stream_id = 0;
  self->context = NULL;
  self->loop = NULL;
  self->loop_thread = NULL;
  self->connected = FALSE;
  self->publish_started = FALSE;
  self->connect_error = NULL;
  self->header_sent = FALSE;
  self->base_ts = 0;
  self->base_ts_set = FALSE;
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);

  gst_base_sink_set_sync (GST_BASE_SINK (self), FALSE);
  gst_base_sink_set_async_enabled (GST_BASE_SINK (self), FALSE);
}

static void
gst_ertmp2_client_sink_finalize (GObject * object)
{
  GstErtmp2ClientSink *self = GST_ERTMP2_CLIENT_SINK (object);

  g_free (self->location);
  gst_rtmp_location_clear (&self->rtmp_location);
  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);
  g_clear_error (&self->connect_error);

  G_OBJECT_CLASS (gst_ertmp2_client_sink_parent_class)->finalize (object);
}

static void
gst_ertmp2_client_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstErtmp2ClientSink *self = GST_ERTMP2_CLIENT_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_free (self->location);
      self->location = g_value_dup_string (value);
      break;
    case PROP_TIMEOUT:
      self->timeout = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ertmp2_client_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstErtmp2ClientSink *self = GST_ERTMP2_CLIENT_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, self->location);
      break;
    case PROP_TIMEOUT:
      g_value_set_int (value, self->timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Parse "rtmp://host:port/app/stream" into GstRtmpLocation */
static gboolean
parse_rtmp_location (GstErtmp2ClientSink * self)
{
  GstUri *uri;
  const gchar *path;
  gchar *app = NULL, *stream = NULL;

  if (!self->location || self->location[0] == '\0') {
    GST_ERROR_OBJECT (self, "No location set");
    return FALSE;
  }

  gst_rtmp_location_clear (&self->rtmp_location);
  memset (&self->rtmp_location, 0, sizeof (self->rtmp_location));

  uri = gst_uri_from_string (self->location);
  if (!uri) {
    GST_ERROR_OBJECT (self, "Failed to parse URI: %s", self->location);
    return FALSE;
  }

  /* Determine scheme */
  const gchar *scheme = gst_uri_get_scheme (uri);
  if (scheme && g_str_equal (scheme, "rtmps")) {
    self->rtmp_location.scheme = GST_RTMP_SCHEME_RTMPS;
  } else {
    self->rtmp_location.scheme = GST_RTMP_SCHEME_RTMP;
  }

  self->rtmp_location.host = g_strdup (gst_uri_get_host (uri));
  self->rtmp_location.port = gst_uri_get_port (uri);
  if (self->rtmp_location.port == GST_URI_NO_PORT) {
    self->rtmp_location.port =
        gst_rtmp_scheme_get_default_port (self->rtmp_location.scheme);
  }

  path = gst_uri_get_path (uri);
  if (path && path[0] == '/')
    path++;

  if (path) {
    const gchar *slash = strchr (path, '/');
    if (slash) {
      app = g_strndup (path, slash - path);
      stream = g_strdup (slash + 1);
    } else {
      app = g_strdup (path);
      stream = g_strdup ("");
    }
  }

  self->rtmp_location.application = app ? app : g_strdup ("live");
  self->rtmp_location.stream = stream ? stream : g_strdup ("");
  self->rtmp_location.publish = TRUE;
  self->rtmp_location.timeout = self->timeout;

  /* E-RTMP: Use FMLE-style flash version to indicate enhanced support */
  self->rtmp_location.flash_ver = g_strdup ("FMLE/3.0 (compatible; FMSc/1.0)");

  gst_uri_unref (uri);

  GST_INFO_OBJECT (self, "Parsed: host=%s port=%u app=%s stream=%s scheme=%s",
      self->rtmp_location.host, self->rtmp_location.port,
      self->rtmp_location.application, self->rtmp_location.stream,
      gst_rtmp_scheme_to_string (self->rtmp_location.scheme));

  return TRUE;
}

/* Idle callback: fires on the GMainLoop thread to initiate the connect */
static gboolean
ertmp_connect_idle (gpointer user_data)
{
  GstErtmp2ClientSink *self = user_data;
  GST_INFO_OBJECT (self, "Initiating E-RTMP connect from loop thread");
  gst_rtmp_client_connect_async (&self->rtmp_location, NULL,
      ertmp_connect_done, self);
  return G_SOURCE_REMOVE;
}

/* GMainLoop thread for async I/O */
static gpointer
ertmp_loop_thread_func (gpointer user_data)
{
  GstErtmp2ClientSink *self = user_data;
  g_main_context_push_thread_default (self->context);
  g_main_loop_run (self->loop);
  g_main_context_pop_thread_default (self->context);
  return NULL;
}

/* Idle callback to start publish on the GMainLoop thread */
static gboolean
ertmp_start_publish_idle (gpointer user_data)
{
  GstErtmp2ClientSink *self = user_data;
  GST_INFO_OBJECT (self, "Starting publish from loop thread");
  gst_rtmp_client_start_publish_async (self->connection,
      self->rtmp_location.stream, NULL, ertmp_publish_done, self);
  return G_SOURCE_REMOVE;
}

/* Called when connect finishes */
static void
ertmp_connect_done (GObject * source, GAsyncResult * result,
    gpointer user_data)
{
  GstErtmp2ClientSink *self = user_data;
  GError *error = NULL;

  self->connection = gst_rtmp_client_connect_finish (result, &error);

  if (!self->connection) {
    GST_ERROR_OBJECT (self, "E-RTMP connect failed: %s",
        error ? error->message : "unknown error");
    g_mutex_lock (&self->lock);
    self->connect_error = error;
    self->connected = FALSE;
    g_cond_signal (&self->cond);
    g_mutex_unlock (&self->lock);
    return;
  }

  GST_INFO_OBJECT (self, "E-RTMP connected, starting publish");

  gst_rtmp_client_start_publish_async (self->connection,
      self->rtmp_location.stream, NULL, ertmp_publish_done, self);
}

/* Called when publish (createStream + publish) finishes */
static void
ertmp_publish_done (GObject * source, GAsyncResult * result,
    gpointer user_data)
{
  GstErtmp2ClientSink *self = user_data;
  GError *error = NULL;

  if (!gst_rtmp_client_start_publish_finish (self->connection, result,
          &self->stream_id, &error)) {
    GST_ERROR_OBJECT (self, "E-RTMP publish failed: %s",
        error ? error->message : "unknown error");
    g_mutex_lock (&self->lock);
    self->connect_error = error;
    self->publish_started = FALSE;
    g_cond_signal (&self->cond);
    g_mutex_unlock (&self->lock);
    return;
  }

  GST_INFO_OBJECT (self, "E-RTMP publish started, stream_id=%u",
      self->stream_id);

  /* Set a larger chunk size for efficiency */
  gst_rtmp_connection_set_chunk_size (self->connection, 4096);

  g_mutex_lock (&self->lock);
  self->connected = TRUE;
  self->publish_started = TRUE;
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);
}

static gboolean
gst_ertmp2_client_sink_start (GstBaseSink * sink)
{
  GstErtmp2ClientSink *self = GST_ERTMP2_CLIENT_SINK (sink);

  if (!parse_rtmp_location (self)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Invalid RTMP URL"), ("Could not parse location: %s",
            self->location ? self->location : "(null)"));
    return FALSE;
  }

  /* Reset connection state from any previous session */
  self->connected = FALSE;
  self->publish_started = FALSE;
  g_clear_error (&self->connect_error);
  self->stream_id = 0;

  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);

  /* Attach the connect idle source BEFORE starting the loop thread.
   * This ensures the source is queued when the loop starts iterating. */
  GSource *idle = g_idle_source_new ();
  g_source_set_callback (idle, ertmp_connect_idle, self, NULL);
  g_source_attach (idle, self->context);
  g_source_unref (idle);

  /* Start the GMainLoop thread. All RTMP operations (connect, handshake,
   * createStream, publish, I/O) happen on this thread which owns the
   * GMainContext as thread-default. */
  self->loop_thread =
      g_thread_new ("ertmp-io", ertmp_loop_thread_func, self);

  /* Wait for publish to complete or timeout */
  g_mutex_lock (&self->lock);
  gint64 end_time =
      g_get_monotonic_time () + (gint64) self->timeout * G_TIME_SPAN_SECOND;
  while (!self->publish_started && !self->connect_error) {
    if (!g_cond_wait_until (&self->cond, &self->lock, end_time)) {
      g_mutex_unlock (&self->lock);
      GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE,
          ("Connection timeout"), ("Timed out connecting to %s",
              self->location));
      return FALSE;
    }
  }

  if (self->connect_error) {
    GError *err = self->connect_error;
    self->connect_error = NULL;
    g_mutex_unlock (&self->lock);
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE,
        ("E-RTMP connection failed"), ("%s", err->message));
    g_error_free (err);
    return FALSE;
  }
  g_mutex_unlock (&self->lock);

  self->header_sent = FALSE;
  self->base_ts_set = FALSE;

  GST_INFO_OBJECT (self, "E-RTMP sink started successfully");
  return TRUE;
}

static gboolean
gst_ertmp2_client_sink_stop (GstBaseSink * sink)
{
  GstErtmp2ClientSink *self = GST_ERTMP2_CLIENT_SINK (sink);

  if (self->connection && self->publish_started) {
    gst_rtmp_client_stop_publish (self->connection,
        self->rtmp_location.stream, GST_RTMP_DEFAULT_STOP_COMMANDS);
  }

  if (self->connection) {
    gst_rtmp_connection_close (self->connection);
    g_clear_object (&self->connection);
  }

  if (self->loop) {
    g_main_loop_quit (self->loop);
  }
  if (self->loop_thread) {
    g_thread_join (self->loop_thread);
    self->loop_thread = NULL;
  }
  if (self->loop) {
    g_main_loop_unref (self->loop);
    self->loop = NULL;
  }
  if (self->context) {
    g_main_context_unref (self->context);
    self->context = NULL;
  }

  self->connected = FALSE;
  self->publish_started = FALSE;
  self->stream_id = 0;
  self->header_sent = FALSE;
  self->base_ts_set = FALSE;

  GST_INFO_OBJECT (self, "E-RTMP sink stopped");
  return TRUE;
}

/*
 * Parse an FLV tag from a buffer and send it as an RTMP message.
 *
 * FLV tag format (from flvmux):
 *  - byte 0: tag type (8=audio, 9=video, 18=script/metadata)
 *  - bytes 1-3: data size (24-bit BE)
 *  - bytes 4-6: timestamp low 24 bits (BE)
 *  - byte 7: timestamp high 8 bits (extension)
 *  - bytes 8-10: stream ID (always 0 in FLV files)
 *  - bytes 11..: tag data
 *
 * We skip the 9-byte FLV file header and 4-byte PreviousTagSize entries
 * that flvmux produces when streamable=true.
 */
static GstFlowReturn
send_flv_data (GstErtmp2ClientSink * self, GstMapInfo * map)
{
  const guint8 *data = map->data;
  gsize size = map->size;
  gsize offset = 0;

  /* Skip FLV file header if present (first buffer) */
  if (!self->header_sent && size >= 9 && data[0] == 'F' && data[1] == 'L'
      && data[2] == 'V') {
    guint32 header_size = GST_READ_UINT32_BE (data + 5);
    offset = header_size;
    /* Skip PreviousTagSize0 */
    if (offset + 4 <= size)
      offset += 4;
    self->header_sent = TRUE;
    GST_DEBUG_OBJECT (self, "Skipped FLV header (%u bytes)", header_size);
  }

  while (offset + 11 <= size) {
    guint8 tag_type = data[offset];
    guint32 data_size = GST_READ_UINT24_BE (data + offset + 1);
    guint32 ts_low = GST_READ_UINT24_BE (data + offset + 4);
    guint8 ts_ext = data[offset + 7];
    guint32 timestamp = ts_low | ((guint32) ts_ext << 24);

    guint32 tag_total = 11 + data_size;
    if (offset + tag_total > size) {
      GST_WARNING_OBJECT (self, "Truncated FLV tag at offset %zu", offset);
      break;
    }

    GstRtmpMessageType msg_type;
    guint32 chunk_stream;
    switch (tag_type) {
      case 8:
        msg_type = GST_RTMP_MESSAGE_TYPE_AUDIO;
        chunk_stream = 4;
        break;
      case 9:
        msg_type = GST_RTMP_MESSAGE_TYPE_VIDEO;
        chunk_stream = 6;
        break;
      case 18:
        msg_type = GST_RTMP_MESSAGE_TYPE_DATA_AMF0;
        chunk_stream = 4;
        break;
      default:
        GST_DEBUG_OBJECT (self, "Unknown FLV tag type %u, skipping", tag_type);
        offset += tag_total;
        if (offset + 4 <= size)
          offset += 4;
        continue;
    }

    GstBuffer *msg_buf;

    if (tag_type == 18) {
      /*
       * Metadata: the FLV tag body already contains @setDataFrame + onMetaData
       * from eflvmux, so pass it through as-is.
       */
      msg_buf = gst_rtmp_message_new_wrapped (msg_type, chunk_stream,
          self->stream_id,
          g_memdup2 (data + offset + 11, data_size), data_size);
      GST_BUFFER_DTS (msg_buf) = 0;
    } else {
      msg_buf = gst_rtmp_message_new_wrapped (msg_type, chunk_stream,
          self->stream_id,
          g_memdup2 (data + offset + 11, data_size), data_size);
      GST_BUFFER_DTS (msg_buf) =
          (GstClockTime) timestamp * GST_MSECOND;
    }

    gst_rtmp_connection_queue_message (self->connection, msg_buf);

    offset += tag_total;
    /* Skip PreviousTagSize (4 bytes after each tag) */
    if (offset + 4 <= size)
      offset += 4;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_ertmp2_client_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstErtmp2ClientSink *self = GST_ERTMP2_CLIENT_SINK (sink);
  GstMapInfo map;
  GstFlowReturn ret;

  if (!self->connected || !self->connection) {
    GST_WARNING_OBJECT (self, "Not connected, dropping buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  ret = send_flv_data (self, &map);

  gst_buffer_unmap (buffer, &map);
  return ret;
}

static gboolean
gst_ertmp2_client_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GstErtmp2ClientSink *self = GST_ERTMP2_CLIENT_SINK (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_INFO_OBJECT (self, "EOS received, closing connection");
      if (self->connection && self->publish_started) {
        gst_rtmp_client_stop_publish (self->connection,
            self->rtmp_location.stream, GST_RTMP_DEFAULT_STOP_COMMANDS);
        self->publish_started = FALSE;
      }
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (gst_ertmp2_client_sink_parent_class)->event
      (sink, event);
}
