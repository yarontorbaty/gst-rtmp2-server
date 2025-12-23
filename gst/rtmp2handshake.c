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

#include "rtmp2handshake.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>

static void
generate_random_bytes (guint8 * data, gsize size)
{
  gsize i;
  for (i = 0; i < size; i++) {
    data[i] = (guint8) (rand () % 256);
  }
}

void
rtmp2_handshake_init (Rtmp2Handshake * handshake)
{
  memset (handshake, 0, sizeof (Rtmp2Handshake));
  handshake->state = RTMP2_HANDSHAKE_STATE_C0;
  handshake->version = 3;
  handshake->timestamp = (guint32) time (NULL);
  generate_random_bytes ((guint8 *) handshake->random, sizeof (handshake->random));
}

gboolean
rtmp2_handshake_process_c0 (Rtmp2Handshake * handshake, const guint8 * data,
    gsize size)
{
  if (size < 1)
    return FALSE;

  handshake->version = data[0];
  if (handshake->version != 3) {
    return FALSE;
  }

  handshake->state = RTMP2_HANDSHAKE_STATE_C1;
  return TRUE;
}

gboolean
rtmp2_handshake_process_c1 (Rtmp2Handshake * handshake, const guint8 * data,
    gsize size)
{
  if (size < RTMP2_HANDSHAKE_SIZE)
    return FALSE;

  memcpy (handshake->c1, data, RTMP2_HANDSHAKE_SIZE);
  handshake->state = RTMP2_HANDSHAKE_STATE_C2;
  return TRUE;
}

gboolean
rtmp2_handshake_process_c2 (Rtmp2Handshake * handshake, const guint8 * data,
    gsize size)
{
  if (size < RTMP2_HANDSHAKE_SIZE)
    return FALSE;

  handshake->state = RTMP2_HANDSHAKE_STATE_COMPLETE;
  return TRUE;
}

gboolean
rtmp2_handshake_generate_s0 (Rtmp2Handshake * handshake, guint8 * out)
{
  out[0] = 3;
  return TRUE;
}

gboolean
rtmp2_handshake_generate_s1 (Rtmp2Handshake * handshake, guint8 * out)
{
  guint32 timestamp = (guint32) time (NULL);
  guint32 zero = 0;

  /* S1 format: timestamp (4) + zero (4) + random (1528) */
  memcpy (out, &timestamp, 4);
  memcpy (out + 4, &zero, 4);
  generate_random_bytes (out + 8, RTMP2_HANDSHAKE_SIZE - 8);

  memcpy (handshake->s1, out, RTMP2_HANDSHAKE_SIZE);
  return TRUE;
}

gboolean
rtmp2_handshake_generate_s2 (Rtmp2Handshake * handshake, const guint8 * c1,
    guint8 * out)
{
  guint32 timestamp = (guint32) time (NULL);
  guint32 timestamp2 = 0;

  /* S2 format: timestamp (4) + timestamp2 (4) + random (1528) */
  if (c1) {
    memcpy (&timestamp2, c1, 4);
  }

  memcpy (out, &timestamp, 4);
  memcpy (out + 4, &timestamp2, 4);
  generate_random_bytes (out + 8, RTMP2_HANDSHAKE_SIZE - 8);

  memcpy (handshake->s2, out, RTMP2_HANDSHAKE_SIZE);
  return TRUE;
}

