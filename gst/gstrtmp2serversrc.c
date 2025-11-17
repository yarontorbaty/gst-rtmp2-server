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

#include "gstrtmp2server.h"
#include "gstrtmp2serversrc.h"
#include "rtmp2chunk.h"

#include <string.h>
#include <sys/socket.h>
#include <gio/gio.h>
#include <gio/giotypes.h>

GST_DEBUG_CATEGORY_STATIC (gst_rtmp2_server_src_debug);
#define GST_CAT_DEFAULT gst_rtmp2_server_src_debug

enum
{
  PROP_0,
  PROP_HOST,
  PROP_PORT,
  PROP_APPLICATION,
  PROP_STREAM_KEY,
  PROP_TIMEOUT,
  PROP_TLS,
  PROP_CERTIFICATE,
  PROP_PRIVATE_KEY
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean gst_rtmp2_server_src_start (GstBaseSrc * bsrc);
static gboolean gst_rtmp2_server_src_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_rtmp2_server_src_create (GstPushSrc * psrc,
    GstBuffer ** buf);
static void gst_rtmp2_server_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtmp2_server_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtmp2_server_src_finalize (GObject * object);

static gboolean server_accept_cb (GSocket * socket, GIOCondition condition,
    gpointer user_data);

#define gst_rtmp2_server_src_parent_class parent_class
G_DEFINE_TYPE (GstRtmp2ServerSrc, gst_rtmp2_server_src, GST_TYPE_PUSH_SRC);

static void
gst_rtmp2_server_src_class_init (GstRtmp2ServerSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_rtmp2_server_src_set_property;
  gobject_class->get_property = gst_rtmp2_server_src_get_property;
  gobject_class->finalize = gst_rtmp2_server_src_finalize;

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_rtmp2_server_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_rtmp2_server_src_stop);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_rtmp2_server_src_create);

  g_object_class_install_property (gobject_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "Address to bind to (default: 0.0.0.0)",
          "0.0.0.0", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_uint ("port", "Port",
          "TCP port to listen on (default: 1935)",
          1, 65535, 1935, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_APPLICATION,
      g_param_spec_string ("application", "Application",
          "RTMP application name (default: live)",
          "live", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STREAM_KEY,
      g_param_spec_string ("stream-key", "Stream Key",
          "If set, only accept this stream key (optional)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "Timeout",
          "Client timeout in seconds (default: 30)",
          1, 3600, 30, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TLS,
      g_param_spec_boolean ("tls", "TLS",
          "Enable TLS/SSL encryption (E-RTMP/RTMPS) (default: false)",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CERTIFICATE,
      g_param_spec_string ("certificate", "Certificate",
          "Path to TLS certificate file (PEM format)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PRIVATE_KEY,
      g_param_spec_string ("private-key", "Private Key",
          "Path to TLS private key file (PEM format)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "RTMP2 Server Source",
      "Source/Network",
      "Receives RTMP push streams from clients",
      "Your Name <your.email@example.com>");

  GST_DEBUG_CATEGORY_INIT (gst_rtmp2_server_src_debug, "rtmp2serversrc",
      0, "RTMP2 Server Source element");
  
  /* Initialize client debug category */
  rtmp2_client_debug_init ();
  
  /* Initialize chunk parser debug category */
  rtmp2_chunk_debug_init ();
}

static void
gst_rtmp2_server_src_init (GstRtmp2ServerSrc * src)
{
  src->host = g_strdup ("0.0.0.0");
  src->port = 1935;
  src->application = g_strdup ("live");
  src->stream_key = NULL;
  src->timeout = 30;
  src->tls = FALSE;
  src->certificate = NULL;
  src->private_key = NULL;

  src->server_socket = NULL;
  src->server_source = NULL;
  src->context = NULL;
  src->clients = NULL;
  g_mutex_init (&src->clients_lock);
  src->tls_cert = NULL;
  src->tls_key = NULL;

  src->active_client = NULL;
  src->have_video = FALSE;
  src->have_audio = FALSE;
  src->video_caps = NULL;
  src->audio_caps = NULL;
  src->sent_flv_header = FALSE;

  /* Configure as live async source - we wait for client connections and stream in real-time */
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (src), TRUE);
}

static void
gst_rtmp2_server_src_finalize (GObject * object)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (object);

  g_free (src->host);
  g_free (src->application);
  g_free (src->stream_key);
  g_free (src->certificate);
  g_free (src->private_key);

  if (src->tls_cert)
    g_object_unref (src->tls_cert);
  if (src->tls_key)
    g_object_unref (src->tls_key);

  if (src->video_caps)
    gst_caps_unref (src->video_caps);
  if (src->audio_caps)
    gst_caps_unref (src->audio_caps);

  g_mutex_clear (&src->clients_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtmp2_server_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
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
    case PROP_TLS:
      src->tls = g_value_get_boolean (value);
      break;
    case PROP_CERTIFICATE:{
      g_free (src->certificate);
      src->certificate = g_value_dup_string (value);
      if (src->certificate) {
        GError *error = NULL;
        if (src->tls_cert)
          g_object_unref (src->tls_cert);
        src->tls_cert = g_tls_certificate_new_from_file (src->certificate, &error);
        if (error) {
          GST_WARNING_OBJECT (src, "Failed to load certificate: %s",
              error->message);
          g_error_free (error);
        }
      }
      break;
    }
    case PROP_PRIVATE_KEY:{
      g_free (src->private_key);
      src->private_key = g_value_dup_string (value);
      /* Note: GTlsCertificate can load both cert and key from same file */
      if (src->private_key && src->certificate) {
        GError *error = NULL;
        if (src->tls_cert)
          g_object_unref (src->tls_cert);
        src->tls_cert = g_tls_certificate_new_from_files (src->certificate,
            src->private_key, &error);
        if (error) {
          GST_WARNING_OBJECT (src, "Failed to load certificate/key: %s",
              error->message);
          g_error_free (error);
        }
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtmp2_server_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
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
    case PROP_TLS:
      g_value_set_boolean (value, src->tls);
      break;
    case PROP_CERTIFICATE:
      g_value_set_string (value, src->certificate);
      break;
    case PROP_PRIVATE_KEY:
      g_value_set_string (value, src->private_key);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
server_accept_cb (GSocket * socket, GIOCondition condition, gpointer user_data)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (user_data);
  GSocketConnection *connection;
  GIOStream *tls_stream = NULL;
  GError *error = NULL;
  Rtmp2Client *client;

  GSocket *client_socket = g_socket_accept (socket, NULL, &error);
  if (!client_socket) {
    GST_WARNING_OBJECT (src, "Failed to accept connection: %s",
        error ? error->message : "Unknown error");
    if (error)
      g_error_free (error);
    return G_SOURCE_CONTINUE;
  }

  /* Set client socket to non-blocking for async I/O */
  g_socket_set_blocking (client_socket, FALSE);

  /* Increase socket receive buffer to prevent kernel-level drops during streaming */
  /* 256KB buffer can hold ~16 video frames worth of data */
  if (!g_socket_set_option (client_socket, SOL_SOCKET, SO_RCVBUF, 262144, &error)) {
    GST_WARNING_OBJECT (src, "Failed to set SO_RCVBUF: %s",
        error ? error->message : "Unknown error");
    g_clear_error (&error);
    /* Non-fatal, continue anyway */
  } else {
    GST_DEBUG_OBJECT (src, "Set socket receive buffer to 256KB");
  }

  connection = g_socket_connection_factory_create_connection (client_socket);
  g_object_unref (client_socket);

  if (!connection) {
    GST_WARNING_OBJECT (src, "Failed to create connection");
    return G_SOURCE_CONTINUE;
  }

  /* Wrap with TLS if enabled */
  if (src->tls && src->tls_cert) {
    GIOStream *tls_io_stream;
    GTlsConnection *tls_connection;

    tls_io_stream = g_tls_server_connection_new (G_IO_STREAM (connection),
        src->tls_cert, &error);
    if (!tls_io_stream) {
      GST_WARNING_OBJECT (src, "Failed to create TLS connection: %s",
          error ? error->message : "Unknown error");
      if (error)
        g_error_free (error);
      g_object_unref (connection);
      return G_SOURCE_CONTINUE;
    }

    tls_connection = G_TLS_CONNECTION (tls_io_stream);

    tls_stream = tls_io_stream;
    g_object_unref (connection);
    connection = NULL;

    /* Perform TLS handshake */
    if (!g_tls_connection_handshake (tls_connection, NULL, &error)) {
      GST_WARNING_OBJECT (src, "TLS handshake failed: %s",
          error ? error->message : "Unknown error");
      if (error)
        g_error_free (error);
      g_object_unref (tls_stream);
      return G_SOURCE_CONTINUE;
    }

    GST_INFO_OBJECT (src, "TLS handshake completed");
  }

  client = rtmp2_client_new (connection, tls_stream ? tls_stream : NULL);
  if (!client) {
    if (tls_stream)
      g_object_unref (tls_stream);
    if (connection)
      g_object_unref (connection);
    return G_SOURCE_CONTINUE;
  }

  client->timeout_seconds = src->timeout;
  client->user_data = src;

  g_mutex_lock (&src->clients_lock);
  src->clients = g_list_append (src->clients, client);
  gint client_count = g_list_length (src->clients);
  g_mutex_unlock (&src->clients_lock);

  GST_INFO_OBJECT (src, "New client connected%s (total clients: %d)",
      src->tls ? " (TLS)" : "", client_count);

  return G_SOURCE_CONTINUE;
}

static gboolean
gst_rtmp2_server_src_start (GstBaseSrc * bsrc)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (bsrc);
  GSocketAddress *address;
  GInetAddress *inet_address;
  GError *error = NULL;
  gboolean ret;

  /* Use default main context - GStreamer will poll it */
  src->context = g_main_context_default ();

  src->server_socket = g_socket_new (G_SOCKET_FAMILY_IPV4,
      G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
  if (!src->server_socket) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Failed to create socket: %s", error->message), (NULL));
    g_error_free (error);
    return FALSE;
  }

  g_socket_set_blocking (src->server_socket, FALSE);

  inet_address = g_inet_address_new_from_string (src->host);
  if (!inet_address) {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        ("Invalid host address: %s", src->host), (NULL));
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
    return FALSE;
  }

  address = g_inet_socket_address_new (inet_address, src->port);
  g_object_unref (inet_address);

  ret = g_socket_bind (src->server_socket, address, TRUE, &error);
  g_object_unref (address);

  if (!ret) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Failed to bind to %s:%u: %s", src->host, src->port,
            error->message), (NULL));
    g_error_free (error);
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
    return FALSE;
  }

  ret = g_socket_listen (src->server_socket, &error);
  if (!ret) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Failed to listen: %s", error->message), (NULL));
    g_error_free (error);
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
    return FALSE;
  }

  src->server_source = g_socket_create_source (src->server_socket,
      G_IO_IN, NULL);
  g_source_set_callback (src->server_source,
      (GSourceFunc) server_accept_cb, src, NULL);
  g_source_attach (src->server_source, src->context);

  /* Set caps early for proper negotiation */
  GstCaps *caps = gst_caps_new_empty_simple ("video/x-flv");
  gst_base_src_set_caps (GST_BASE_SRC (src), caps);
  gst_caps_unref (caps);

  GST_INFO_OBJECT (src, "RTMP server listening on %s:%u", src->host, src->port);

  return TRUE;
}

static gboolean
gst_rtmp2_server_src_stop (GstBaseSrc * bsrc)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (bsrc);
  GList *l;

  if (src->server_source) {
    g_source_destroy (src->server_source);
    g_source_unref (src->server_source);
    src->server_source = NULL;
  }

  if (src->server_socket) {
    g_object_unref (src->server_socket);
    src->server_socket = NULL;
  }

  g_mutex_lock (&src->clients_lock);
  for (l = src->clients; l; l = l->next) {
    Rtmp2Client *client = (Rtmp2Client *) l->data;
    rtmp2_client_free (client);
  }
  g_list_free (src->clients);
  src->clients = NULL;
  src->active_client = NULL;
  src->sent_flv_header = FALSE;
  g_mutex_unlock (&src->clients_lock);

  /* Don't unref default context */
  if (src->context && src->context != g_main_context_default ()) {
    g_main_context_unref (src->context);
  }
  src->context = NULL;

  return TRUE;
}

static GstFlowReturn
gst_rtmp2_server_src_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstRtmp2ServerSrc *src = GST_RTMP2_SERVER_SRC (psrc);
  GstFlowReturn ret = GST_FLOW_OK;
  GList *l;
  GList *flv_tags = NULL;
  Rtmp2FlvTag *tag;

  /* Send FLV header as the very first buffer */
  if (!src->sent_flv_header) {
    /* FLV file header: 13 bytes total */
    /* 'FLV' signature + version + flags + header size + previous tag size 0 */
    static const guint8 flv_header[13] = {
      'F', 'L', 'V',        /* Signature */
      0x01,                 /* Version 1 */
      0x05,                 /* Flags: audio (0x04) + video (0x01) */
      0x00, 0x00, 0x00, 0x09, /* Header size: 9 bytes */
      0x00, 0x00, 0x00, 0x00  /* Previous tag size 0 (for first tag) */
    };
    
    *buf = gst_buffer_new_allocate (NULL, 13, NULL);
    GstMapInfo map;
    if (gst_buffer_map (*buf, &map, GST_MAP_WRITE)) {
      memcpy (map.data, flv_header, 13);
      gst_buffer_unmap (*buf, &map);
      src->sent_flv_header = TRUE;
      GST_INFO_OBJECT (src, "Sent FLV file header (13 bytes)");
      GST_BUFFER_PTS (*buf) = 0;
      GST_BUFFER_DTS (*buf) = 0;
      return GST_FLOW_OK;
    } else {
      gst_buffer_unref (*buf);
      GST_ERROR_OBJECT (src, "Failed to map FLV header buffer");
      return GST_FLOW_ERROR;
    }
  }

  /* Process pending accept events */
  while (g_main_context_pending (src->context)) {
    g_main_context_iteration (src->context, FALSE);
  }

  g_mutex_lock (&src->clients_lock);

  gint client_count = g_list_length (src->clients);
  GST_DEBUG_OBJECT (src, "Processing %d clients in create", client_count);

  /* Process all clients - read multiple times to drain socket buffers */
  for (l = src->clients; l; l = l->next) {
    Rtmp2Client *client = (Rtmp2Client *) l->data;
    GError *error = NULL;
    gint read_attempts = 0;
    const gint max_reads = 100; /* Read up to 100 times (100 * 16KB = 1.6MB burst capacity) */

    GST_DEBUG_OBJECT (src, "Client state=%d, handshake_complete=%d",
        client->state, client->handshake_complete);

    if (client->state == RTMP2_CLIENT_STATE_DISCONNECTED ||
        client->state == RTMP2_CLIENT_STATE_ERROR) {
      GST_DEBUG_OBJECT (src, "Skipping disconnected/errored client");
      continue;
    }

    /* Read multiple times to drain socket buffer aggressively */
    for (read_attempts = 0; read_attempts < max_reads; read_attempts++) {
      gboolean processed = rtmp2_client_process_data (client, &error);
      
      if (error) {
        /* Real error occurred */
        GST_WARNING_OBJECT (src, "Client error: %s", error->message);
        g_error_free (error);
        error = NULL;
        break;
      }

      if (!processed) {
        /* No more data available or client disconnected */
        if (client->state == RTMP2_CLIENT_STATE_DISCONNECTED ||
            client->state == RTMP2_CLIENT_STATE_ERROR) {
          GST_DEBUG_OBJECT (src, "Client disconnected or errored");
        }
        break; /* Stop reading this client */
      }
    }

    /* If client is publishing, make it active */
    if (client->state == RTMP2_CLIENT_STATE_PUBLISHING &&
        !src->active_client) {
      src->active_client = client;
      GST_INFO_OBJECT (src, "Client started publishing");
    }
  }

  /* Try to get data from active client - be patient and wait for real data */
  gint retry_count = 0;
  /* Wait longer - up to 60 seconds for client connection and first data */
  const gint max_retries = 60000; /* 60 seconds (enough time even with minimal sleep) */
  
  while (retry_count < max_retries) {
    if (retry_count % 100 == 0) {
      GST_DEBUG_OBJECT (src, "Retry %d: active_client=%p, client_count=%d", 
          retry_count, src->active_client, g_list_length(src->clients));
    }
    
    if (src->active_client && src->active_client->state == RTMP2_CLIENT_STATE_PUBLISHING) {
      /* Check for FLV tags in the parser */
      flv_tags = src->active_client->flv_parser.pending_tags;
      gint pending_count = g_list_length (flv_tags);
      if (pending_count > 0) {
        GST_INFO_OBJECT (src, "Found %d pending FLV tags", pending_count);
      } else if (retry_count % 100 == 0) {
        GST_DEBUG_OBJECT (src, "Waiting for FLV tags... (retry %d, active_client=%p, state=%d)", 
            retry_count, src->active_client, src->active_client ? src->active_client->state : -1);
      }
      
      if (flv_tags) {
        tag = (Rtmp2FlvTag *) flv_tags->data;
        src->active_client->flv_parser.pending_tags =
            g_list_remove_link (src->active_client->flv_parser.pending_tags,
            flv_tags);

        if (tag->data && gst_buffer_get_size (tag->data) > 0) {
          gsize buf_size = gst_buffer_get_size (tag->data);
          GST_INFO_OBJECT (src, "Returning FLV tag with %zu bytes (type=%d)", 
              buf_size, tag->tag_type);
          *buf = gst_buffer_ref (tag->data);

          /* Set timestamp */
          GST_BUFFER_PTS (*buf) = tag->timestamp * GST_MSECOND;
          GST_BUFFER_DTS (*buf) = GST_BUFFER_PTS (*buf);

          rtmp2_flv_tag_free (tag);
          g_list_free (flv_tags);
          ret = GST_FLOW_OK;
          g_mutex_unlock (&src->clients_lock);
          return ret;  /* Return immediately with data */
        } else {
          /* Empty tag - skip it and continue waiting */
          rtmp2_flv_tag_free (tag);
          g_list_free (flv_tags);
        }
      }
    }
    
    /* No data available - unlock, process events, sleep, then retry */
    g_mutex_unlock (&src->clients_lock);
    
    /* Process any pending network events */
    while (g_main_context_pending (src->context)) {
      g_main_context_iteration (src->context, FALSE);
    }
    
    /* Adaptive sleep - minimal during active streaming to maximize frame capture */
    if (src->active_client && src->active_client->state == RTMP2_CLIENT_STATE_PUBLISHING) {
      /* Client is actively publishing - tiny sleep to avoid spinning but maximize throughput */
      g_usleep (100);  /* 100μs = 0.1ms, fast enough for 30fps (33ms per frame) */
    } else if (retry_count < 100) {
      /* Waiting for connection: minimal sleep */
      g_usleep (1000);  /* 1ms */
    } else {
      /* Extended wait: longer sleep to reduce CPU */
      g_usleep (10000);  /* 10ms */
    }
    retry_count++;
    
    g_mutex_lock (&src->clients_lock);
    
    /* Reprocess clients to get new data - drain socket buffers */
    for (l = src->clients; l; l = l->next) {
      Rtmp2Client *client = (Rtmp2Client *) l->data;
      GError *error = NULL;
      gint read_attempts = 0;
      const gint max_reads = 100; /* Read up to 100 times per cycle - balanced throughput/responsiveness */
      
      if (client->state != RTMP2_CLIENT_STATE_DISCONNECTED &&
          client->state != RTMP2_CLIENT_STATE_ERROR) {
        /* Read multiple times to drain socket buffer */
        for (read_attempts = 0; read_attempts < max_reads; read_attempts++) {
          gboolean processed = rtmp2_client_process_data (client, &error);
          if (error) {
            g_error_free (error);
            error = NULL;
            break; /* Stop on error */
          }
          if (!processed) {
            break; /* Stop if no more data */
          }
        }
        
        /* Check if this client started publishing and should become active */
        if (client->state == RTMP2_CLIENT_STATE_PUBLISHING &&
            !src->active_client) {
          src->active_client = client;
          GST_INFO_OBJECT (src, "Client started publishing (detected in retry loop)");
        }
      }
    }
  }

  /* Timeout after 60 seconds - likely no client or stream ended */
  GST_WARNING_OBJECT (src, "No data after %d retries (60 seconds)", max_retries);
  g_mutex_unlock (&src->clients_lock);
  return GST_FLOW_EOS; /* Signal end of stream */
}

