/* GStreamer RTMP Library
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtmpserver.h"
#include "rtmphandshake.h"
#include "rtmpmessage.h"
#include "amf.h"
#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (gst_rtmp_server_debug_category);
#define GST_CAT_DEFAULT gst_rtmp_server_debug_category

static void
init_debug (void)
{
  static gsize done = 0;
  if (g_once_init_enter (&done)) {
    GST_DEBUG_CATEGORY_INIT (gst_rtmp_server_debug_category, "rtmpserver",
        0, "debug category for RTMP server");
    g_once_init_leave (&done, 1);
  }
}

/* ========== Accept connection ========== */

typedef struct {
  GSocketConnection *socket_connection;
  gboolean strict_handshake;
} AcceptData;

static void
accept_data_free (gpointer ptr)
{
  AcceptData *data = ptr;
  g_clear_object (&data->socket_connection);
  g_free (data);
}

static void
server_accept_handshake_done (GObject * source, GAsyncResult * result,
    gpointer user_data)
{
  GIOStream *stream = G_IO_STREAM (source);
  GTask *task = user_data;
  AcceptData *data = g_task_get_task_data (task);
  GError *error = NULL;
  gboolean res;
  GstRtmpConnection *connection;

  res = gst_rtmp_server_handshake_finish (stream, result, &error);
  if (!res) {
    GST_ERROR ("Server handshake failed: %s", error->message);
    g_task_return_error (task, error);
    g_object_unref (task);
    return;
  }

  GST_INFO ("Server handshake completed, creating connection");

  connection = gst_rtmp_connection_new (data->socket_connection,
      g_task_get_cancellable (task));

  g_task_return_pointer (task, connection, g_object_unref);
  g_object_unref (task);
}

void
gst_rtmp_server_accept_async (GSocketConnection * socket_connection,
    gboolean strict_handshake,
    GCancellable * cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GTask *task;
  AcceptData *data;
  GIOStream *stream;

  g_return_if_fail (G_IS_SOCKET_CONNECTION (socket_connection));

  init_debug ();
  GST_INFO ("Accepting new RTMP connection");

  task = g_task_new (NULL, cancellable, callback, user_data);

  data = g_new0 (AcceptData, 1);
  data->socket_connection = g_object_ref (socket_connection);
  data->strict_handshake = strict_handshake;
  g_task_set_task_data (task, data, accept_data_free);

  stream = G_IO_STREAM (socket_connection);
  gst_rtmp_server_handshake (stream, strict_handshake, cancellable,
      server_accept_handshake_done, task);
}

GstRtmpConnection *
gst_rtmp_server_accept_finish (GAsyncResult * result, GError ** error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);
  return g_task_propagate_pointer (G_TASK (result), error);
}

/* ========== Enhanced RTMP parsing ========== */

gboolean
gst_rtmp_enhanced_caps_parse (const GstAmfNode * command_object,
    GstRtmpEnhancedCaps * out)
{
  const GstAmfNode *caps_ex_node;
  const GstAmfNode *fourcc_map_node;

  g_return_val_if_fail (out != NULL, FALSE);

  memset (out, 0, sizeof (*out));

  if (!command_object) {
    return FALSE;
  }

  /* Parse capsEx */
  caps_ex_node = gst_amf_node_get_field (command_object, "capsEx");
  if (caps_ex_node && gst_amf_node_get_type (caps_ex_node) == GST_AMF_TYPE_NUMBER) {
    out->caps_ex = (guint8) gst_amf_node_get_number (caps_ex_node);
    out->supports_reconnect = (out->caps_ex & GST_RTMP_CAPS_RECONNECT) != 0;
    out->supports_multitrack = (out->caps_ex & GST_RTMP_CAPS_MULTITRACK) != 0;
    out->supports_timestamp_nano_offset =
        (out->caps_ex & GST_RTMP_CAPS_TIMESTAMP_NANO_OFFSET) != 0;
    GST_DEBUG ("Client capsEx: 0x%02x", out->caps_ex);
  }

  /* Parse videoFourCcInfoMap */
  fourcc_map_node = gst_amf_node_get_field (command_object, "videoFourCcInfoMap");
  if (fourcc_map_node) {
    GstAmfType type = gst_amf_node_get_type (fourcc_map_node);
    if (type == GST_AMF_TYPE_OBJECT || type == GST_AMF_TYPE_ECMA_ARRAY) {
      const GstAmfNode *hevc_node, *vp9_node, *av1_node;

      hevc_node = gst_amf_node_get_field (fourcc_map_node, "hvc1");
      if (hevc_node) {
        out->supports_hevc = TRUE;
        GST_DEBUG ("Client supports HEVC (hvc1)");
      }

      vp9_node = gst_amf_node_get_field (fourcc_map_node, "vp09");
      if (vp9_node) {
        out->supports_vp9 = TRUE;
        GST_DEBUG ("Client supports VP9 (vp09)");
      }

      av1_node = gst_amf_node_get_field (fourcc_map_node, "av01");
      if (av1_node) {
        out->supports_av1 = TRUE;
        GST_DEBUG ("Client supports AV1 (av01)");
      }
    }
  }

  return TRUE;
}

/* ========== Server command responses ========== */

void
gst_rtmp_server_send_connect_result (GstRtmpConnection * connection,
    gdouble transaction_id,
    const GstRtmpEnhancedCaps * client_caps)
{
  GstAmfNode *properties;
  GstAmfNode *info;
  GBytes *payload;
  guint8 *data;
  gsize size;
  GstBuffer *buffer;

  g_return_if_fail (GST_IS_RTMP_CONNECTION (connection));

  init_debug ();

  /* Build properties object */
  properties = gst_amf_node_new_object ();
  gst_amf_node_append_field_string (properties, "fmsVer", "FMS/3,0,1,123", -1);
  gst_amf_node_append_field_number (properties, "capabilities", 31);

  /* Build info object */
  info = gst_amf_node_new_object ();
  gst_amf_node_append_field_string (info, "level", "status", -1);
  gst_amf_node_append_field_string (info, "code", "NetConnection.Connect.Success", -1);
  gst_amf_node_append_field_string (info, "description", "Connection succeeded.", -1);
  gst_amf_node_append_field_number (info, "objectEncoding", 0);

  /* Add Enhanced RTMP support if client requested it */
  if (client_caps && (client_caps->supports_hevc || client_caps->supports_vp9 ||
          client_caps->supports_av1)) {
    GstAmfNode *fourcc_map = gst_amf_node_new_object ();

    if (client_caps->supports_hevc) {
      GstAmfNode *hevc_info = gst_amf_node_new_object ();
      gst_amf_node_append_take_field (fourcc_map, "hvc1", hevc_info);
    }
    if (client_caps->supports_vp9) {
      GstAmfNode *vp9_info = gst_amf_node_new_object ();
      gst_amf_node_append_take_field (fourcc_map, "vp09", vp9_info);
    }
    if (client_caps->supports_av1) {
      GstAmfNode *av1_info = gst_amf_node_new_object ();
      gst_amf_node_append_take_field (fourcc_map, "av01", av1_info);
    }

    gst_amf_node_append_take_field (info, "videoFourCcInfoMap", fourcc_map);
    GST_DEBUG ("Sending Enhanced RTMP capabilities in connect result");
  }

  payload = gst_amf_serialize_command (transaction_id, "_result",
      properties, info, NULL);

  gst_amf_node_free (properties);
  gst_amf_node_free (info);

  data = g_bytes_unref_to_data (payload, &size);
  buffer = gst_rtmp_message_new_wrapped (GST_RTMP_MESSAGE_TYPE_COMMAND_AMF0,
      3, 0, data, size);

  GST_DEBUG ("Sending connect _result (transaction %.0f)", transaction_id);
  gst_rtmp_connection_queue_message (connection, buffer);
}

void
gst_rtmp_server_send_create_stream_result (GstRtmpConnection * connection,
    gdouble transaction_id,
    guint32 stream_id)
{
  GstAmfNode *null_node;
  GstAmfNode *stream_id_node;
  GBytes *payload;
  guint8 *data;
  gsize size;
  GstBuffer *buffer;

  g_return_if_fail (GST_IS_RTMP_CONNECTION (connection));

  init_debug ();

  null_node = gst_amf_node_new_null ();
  stream_id_node = gst_amf_node_new_number ((gdouble) stream_id);

  payload = gst_amf_serialize_command (transaction_id, "_result",
      null_node, stream_id_node, NULL);

  gst_amf_node_free (null_node);
  gst_amf_node_free (stream_id_node);

  data = g_bytes_unref_to_data (payload, &size);
  buffer = gst_rtmp_message_new_wrapped (GST_RTMP_MESSAGE_TYPE_COMMAND_AMF0,
      3, 0, data, size);

  GST_DEBUG ("Sending createStream _result (transaction %.0f, stream %u)",
      transaction_id, stream_id);
  gst_rtmp_connection_queue_message (connection, buffer);
}

void
gst_rtmp_server_send_publish_start (GstRtmpConnection * connection,
    guint32 stream_id)
{
  GstAmfNode *null_node;
  GstAmfNode *info;
  GBytes *payload;
  guint8 *data;
  gsize size;
  GstBuffer *buffer;
  GstRtmpUserControl uc;

  g_return_if_fail (GST_IS_RTMP_CONNECTION (connection));

  init_debug ();

  /* Send StreamBegin user control message */
  uc.type = GST_RTMP_USER_CONTROL_TYPE_STREAM_BEGIN;
  uc.param = stream_id;
  uc.param2 = 0;
  gst_rtmp_connection_queue_message (connection,
      gst_rtmp_message_new_user_control (&uc));

  /* Build onStatus info object */
  null_node = gst_amf_node_new_null ();
  info = gst_amf_node_new_object ();
  gst_amf_node_append_field_string (info, "level", "status", -1);
  gst_amf_node_append_field_string (info, "code", "NetStream.Publish.Start", -1);
  gst_amf_node_append_field_string (info, "description", "Publishing started.", -1);

  payload = gst_amf_serialize_command (0, "onStatus", null_node, info, NULL);

  gst_amf_node_free (null_node);
  gst_amf_node_free (info);

  data = g_bytes_unref_to_data (payload, &size);
  buffer = gst_rtmp_message_new_wrapped (GST_RTMP_MESSAGE_TYPE_COMMAND_AMF0,
      3, stream_id, data, size);

  GST_DEBUG ("Sending onStatus NetStream.Publish.Start (stream %u)", stream_id);
  gst_rtmp_connection_queue_message (connection, buffer);
}

/* ========== Server command handlers ========== */

typedef struct {
  GstRtmpServerPublishCallback publish_callback;
  GstRtmpServerMediaCallback media_callback;
  gpointer user_data;
  GDestroyNotify user_data_destroy;
  GstRtmpServerState state;
  GstRtmpEnhancedCaps client_caps;
  gchar *app_name;
  gchar *stream_key;
  guint32 stream_id;
} ServerHandlerData;

static void
server_handler_data_free (gpointer ptr)
{
  ServerHandlerData *data = ptr;
  if (data->user_data_destroy && data->user_data) {
    data->user_data_destroy (data->user_data);
  }
  g_free (data->app_name);
  g_free (data->stream_key);
  g_free (data);
}

static void
server_handle_input (GstRtmpConnection * connection, GstBuffer * buffer,
    gpointer user_data)
{
  ServerHandlerData *data = user_data;
  GstRtmpMeta *meta;

  meta = gst_buffer_get_rtmp_meta (buffer);
  if (!meta) {
    GST_WARNING ("Received buffer without RTMP meta");
    return;
  }

  /* Handle media messages */
  if (meta->type == GST_RTMP_MESSAGE_TYPE_VIDEO ||
      meta->type == GST_RTMP_MESSAGE_TYPE_AUDIO ||
      meta->type == GST_RTMP_MESSAGE_TYPE_DATA_AMF0) {
    if (data->media_callback) {
      data->media_callback (connection, buffer, data->user_data);
    }
  }
}

void
gst_rtmp_server_setup_handlers (GstRtmpConnection * connection,
    GstRtmpServerPublishCallback publish_callback,
    GstRtmpServerMediaCallback media_callback,
    gpointer user_data,
    GDestroyNotify user_data_destroy)
{
  ServerHandlerData *data;

  g_return_if_fail (GST_IS_RTMP_CONNECTION (connection));

  init_debug ();

  data = g_new0 (ServerHandlerData, 1);
  data->publish_callback = publish_callback;
  data->media_callback = media_callback;
  data->user_data = user_data;
  data->user_data_destroy = user_data_destroy;
  data->state = GST_RTMP_SERVER_STATE_HANDSHAKE_DONE;
  data->stream_id = 1;

  /* Set up input handler for media data */
  gst_rtmp_connection_set_input_handler (connection,
      server_handle_input, data, server_handler_data_free);

  GST_DEBUG ("Server handlers set up for connection %p", connection);
}

