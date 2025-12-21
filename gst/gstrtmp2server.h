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

#ifndef __GST_RTMP2_SERVER_H__
#define __GST_RTMP2_SERVER_H__

#include <gst/gst.h>
#include <gio/gio.h>

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
typedef struct _Rtmp2Client Rtmp2Client;

#include "rtmp2client.h"
#include "rtmp2handshake.h"
#include "rtmp2chunk.h"
#include "rtmp2flv.h"

struct _GstRtmp2ServerSrc {
  GstElement parent;

  /* Properties */
  gchar *host;
  guint port;
  gchar *application;
  gchar *stream_key;
  guint timeout;
  gboolean tls;
  gchar *certificate;
  gchar *private_key;

  /* Server state */
  GSocket *server_socket;
  GSource *server_source;
  GMainContext *context;
  GThread *event_thread;
  gboolean running;
  GList *clients;
  GMutex clients_lock;
  GTlsCertificate *tls_cert;
  GTlsCertificate *tls_key;

  /* Current active client */
  Rtmp2Client *active_client;
  
  /* Source pad (always present - outputs raw FLV data) */
  GstPad *srcpad;
  gboolean srcpad_started;  /* TRUE after stream-start sent */
  gint64 eos_wait_start;    /* Monotonic time when queue became empty for EOS check */
  
  /* Stream pads (sometimes pads - created on first video/audio message) */
  GstPad *video_pad;
  GstPad *audio_pad;
  GMutex pad_lock;
  GstTask *task;
  GRecMutex task_lock;
  
  /* Stream info */
  gboolean have_video;
  gboolean have_audio;
  GstCaps *video_caps;
  GstCaps *audio_caps;
  GstBuffer *video_codec_data;
  GstBuffer *audio_codec_data;
};

struct _GstRtmp2ServerSrcClass {
  GstElementClass parent_class;
};

GType gst_rtmp2_server_src_get_type (void);

G_END_DECLS

#endif /* __GST_RTMP2_SERVER_H__ */

