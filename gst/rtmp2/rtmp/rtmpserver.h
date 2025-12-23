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

#ifndef _GST_RTMP_SERVER_H_
#define _GST_RTMP_SERVER_H_

#include "rtmpconnection.h"

G_BEGIN_DECLS

/* Enhanced RTMP capabilities flags */
#define GST_RTMP_CAPS_RECONNECT             (1 << 0)
#define GST_RTMP_CAPS_MULTITRACK            (1 << 1)
#define GST_RTMP_CAPS_TIMESTAMP_NANO_OFFSET (1 << 2)

/* Enhanced RTMP video FourCC codes */
typedef enum {
  GST_RTMP_VIDEO_FOURCC_AV1  = 0x61763031,  /* 'av01' */
  GST_RTMP_VIDEO_FOURCC_VP9  = 0x76703039,  /* 'vp09' */
  GST_RTMP_VIDEO_FOURCC_HEVC = 0x68766331,  /* 'hvc1' */
} GstRtmpVideoFourCC;

/* Server session state */
typedef enum {
  GST_RTMP_SERVER_STATE_NEW = 0,
  GST_RTMP_SERVER_STATE_HANDSHAKE_DONE,
  GST_RTMP_SERVER_STATE_CONNECTED,
  GST_RTMP_SERVER_STATE_PUBLISHING,
  GST_RTMP_SERVER_STATE_DISCONNECTED,
  GST_RTMP_SERVER_STATE_ERROR,
} GstRtmpServerState;

/* Enhanced RTMP client capabilities (parsed from connect command) */
typedef struct {
  guint8 caps_ex;
  gboolean supports_reconnect;
  gboolean supports_multitrack;
  gboolean supports_timestamp_nano_offset;
  gboolean supports_hevc;
  gboolean supports_vp9;
  gboolean supports_av1;
} GstRtmpEnhancedCaps;

/* Callback for when publishing starts */
typedef void (*GstRtmpServerPublishCallback) (GstRtmpConnection * connection,
    const gchar * app, const gchar * stream_key, gpointer user_data);

/* Callback for incoming media data */
typedef void (*GstRtmpServerMediaCallback) (GstRtmpConnection * connection,
    GstBuffer * buffer, gpointer user_data);

/* Accept and handshake incoming connection */
void gst_rtmp_server_accept_async (GSocketConnection * socket_connection,
    gboolean strict_handshake,
    GCancellable * cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

GstRtmpConnection * gst_rtmp_server_accept_finish (GAsyncResult * result,
    GError ** error);

/* Set up server to handle incoming RTMP commands */
void gst_rtmp_server_setup_handlers (GstRtmpConnection * connection,
    GstRtmpServerPublishCallback publish_callback,
    GstRtmpServerMediaCallback media_callback,
    gpointer user_data,
    GDestroyNotify user_data_destroy);

/* Send connect result (_result response to 'connect' command) */
void gst_rtmp_server_send_connect_result (GstRtmpConnection * connection,
    gdouble transaction_id,
    const GstRtmpEnhancedCaps * client_caps);

/* Send createStream result */
void gst_rtmp_server_send_create_stream_result (GstRtmpConnection * connection,
    gdouble transaction_id,
    guint32 stream_id);

/* Send onStatus for publish */
void gst_rtmp_server_send_publish_start (GstRtmpConnection * connection,
    guint32 stream_id);

/* Send releaseStream result */
void gst_rtmp_server_send_release_stream_result (GstRtmpConnection * connection,
    gdouble transaction_id);

/* Send FCPublish result */
void gst_rtmp_server_send_fcpublish_result (GstRtmpConnection * connection,
    gdouble transaction_id);

/* Parse Enhanced RTMP capabilities from connect command object */
gboolean gst_rtmp_enhanced_caps_parse (const GstAmfNode * command_object,
    GstRtmpEnhancedCaps * out);

G_END_DECLS

#endif

