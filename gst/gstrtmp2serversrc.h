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

#include "gstrtmp2server.h"

G_BEGIN_DECLS

/**
 * GstRtmp2ServerSrc:
 *
 * RTMP server source element that listens for incoming RTMP connections
 * and outputs the received audio/video stream.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv
 * ]|
 * This pipeline saves an incoming RTMP stream to a file.
 *
 * |[
 * gst-launch-1.0 rtmp2serversrc port=1935 ! flvdemux name=demux \
 *   demux.video ! queue ! h264parse ! mpegtsmux name=mux ! srtsink uri="srt://:9000?mode=listener" \
 *   demux.audio ! queue ! aacparse ! mux.
 * ]|
 * This pipeline re-streams RTMP to SRT.
 *
 * Since: 1.26
 */

G_END_DECLS

#endif /* __GST_RTMP2_SERVER_SRC_H__ */
