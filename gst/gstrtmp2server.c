/*
 * GStreamer
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

#include "gstrtmp2server.h"
#include "gstrtmp2serversrc.h"
#include "gstrtmp2clientsink.h"
#include "gsteflvmux.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  ret &= gst_element_register (plugin, "rtmp2serversrc",
      GST_RANK_NONE, GST_TYPE_RTMP2_SERVER_SRC);
  ret &= gst_element_register (plugin, "ertmp2sink",
      GST_RANK_NONE, GST_TYPE_ERTMP2_CLIENT_SINK);
  ret &= gst_element_register (plugin, "eflvmux",
      GST_RANK_NONE, GST_TYPE_EFLVMUX);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rtmp2server,
    "RTMP2 Server and Enhanced RTMP Client Plugin",
    plugin_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)

