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

#ifndef __RTMP2_HANDSHAKE_H__
#define __RTMP2_HANDSHAKE_H__

#include <glib.h>

G_BEGIN_DECLS

#define RTMP2_HANDSHAKE_SIZE 1536

typedef enum {
  RTMP2_HANDSHAKE_STATE_C0,
  RTMP2_HANDSHAKE_STATE_C1,
  RTMP2_HANDSHAKE_STATE_C2,
  RTMP2_HANDSHAKE_STATE_COMPLETE
} Rtmp2HandshakeState;

typedef struct {
  Rtmp2HandshakeState state;
  guint8 version;
  guint8 c1[RTMP2_HANDSHAKE_SIZE];
  guint8 s1[RTMP2_HANDSHAKE_SIZE];
  guint8 s2[RTMP2_HANDSHAKE_SIZE];
  guint32 timestamp;
  guint32 random[4];
  /* Accumulator for partial reads during handshake */
  guint8 read_buffer[RTMP2_HANDSHAKE_SIZE];
  gsize read_buffer_len;
} Rtmp2Handshake;

void rtmp2_handshake_init (Rtmp2Handshake *handshake);
gboolean rtmp2_handshake_process_c0 (Rtmp2Handshake *handshake, const guint8 *data, gsize size);
gboolean rtmp2_handshake_process_c1 (Rtmp2Handshake *handshake, const guint8 *data, gsize size);
gboolean rtmp2_handshake_process_c2 (Rtmp2Handshake *handshake, const guint8 *data, gsize size);
gboolean rtmp2_handshake_generate_s0 (Rtmp2Handshake *handshake, guint8 *out);
gboolean rtmp2_handshake_generate_s1 (Rtmp2Handshake *handshake, guint8 *out);
gboolean rtmp2_handshake_generate_s2 (Rtmp2Handshake *handshake, const guint8 *c1, guint8 *out);

G_END_DECLS

#endif /* __RTMP2_HANDSHAKE_H__ */

