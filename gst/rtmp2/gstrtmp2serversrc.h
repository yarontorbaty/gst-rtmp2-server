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

#ifndef __GST_RTMP2_SERVER_SRC_H__
#define __GST_RTMP2_SERVER_SRC_H__

#include <gst/gst.h>
#include <gio/gio.h>
#include "gstrtmp2elements.h"
#include "rtmp/rtmpconnection.h"
#include "rtmp/rtmpserver.h"
#include "rtmp/rtmpflv.h"

G_BEGIN_DECLS

#define GST_TYPE_RTMP2_SERVER_SRC \
  (gst_rtmp2_server_src_get_type())
#define GST_RTMP2_SERVER_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTMP2_SERVER_SRC,GstRtmp2ServerSrc))
#define GST_RTMP2_SERVER_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTMP2_SERVER_SRC,GstRtmp2ServerSrcClass))
#define GST_IS_RTMP2_SERVER_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTMP2_SERVER_SRC))
#define GST_IS_RTMP2_SERVER_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTMP2_SERVER_SRC))

typedef struct _GstRtmp2ServerSrc GstRtmp2ServerSrc;
typedef struct _GstRtmp2ServerSrcClass GstRtmp2ServerSrcClass;

/* Server session state */
typedef enum {
  SERVER_SESSION_STATE_NEW = 0,
  SERVER_SESSION_STATE_CONNECTED,
  SERVER_SESSION_STATE_PUBLISHING,
  SERVER_SESSION_STATE_DISCONNECTED,
  SERVER_SESSION_STATE_ERROR,
} ServerSessionState;

/* Server session - represents one connected RTMP client */
typedef struct {
  GSocketConnection *socket_connection;  /* Keep original socket connection */
  GstRtmpConnection *connection;
  ServerSessionState state;
  GstRtmpEnhancedCaps enhanced_caps;
  gchar *app_name;
  gchar *stream_key;
  guint32 stream_id;
  
  /* FLV tag queue */
  GQueue *tag_queue;
  GMutex queue_lock;
  
  /* Timestamp tracking - ts_delta needs to be accumulated per-stream */
  guint32 video_timestamp;
  guint32 audio_timestamp;
  guint32 data_timestamp;
  
  /* Back pointer to element */
  GstRtmp2ServerSrc *src;
} ServerSession;

/**
 * GstRtmp2ServerSrc:
 *
 * RTMP server source element that listens for incoming RTMP connections
 * and outputs the received audio/video stream.
 *
 * Since: 1.26
 */
struct _GstRtmp2ServerSrc {
  GstElement parent;

  /* Properties */
  gchar *host;
  guint port;
  gchar *application;
  gchar *stream_key;
  guint timeout;
  gboolean loop;

  /* Server state */
  GSocketService *service;
  GMainContext *context;
  GThread *thread;
  gboolean running;

  /* Startup synchronization */
  GMutex start_lock;
  GCond start_cond;
  gboolean start_complete;
  gboolean start_error;
  
  /* Sessions */
  GList *sessions;
  GMutex sessions_lock;
  ServerSession *active_session;
  
  /* Source pad (always present - outputs raw FLV data) */
  GstPad *srcpad;
  gboolean srcpad_started;
  gint64 eos_wait_start;
  
  /* Task for pushing data */
  GstTask *task;
  GRecMutex task_lock;
  
  /* Stream info */
  gboolean have_video;
  gboolean have_audio;
  guint stream_count;
};

struct _GstRtmp2ServerSrcClass {
  GstElementClass parent_class;
};

GType gst_rtmp2_server_src_get_type (void);

G_END_DECLS

#endif /* __GST_RTMP2_SERVER_SRC_H__ */
