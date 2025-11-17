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

#include "rtmp2enhanced.h"
#include "rtmp2amf.h"
#include <string.h>

Rtmp2EnhancedCapabilities *
rtmp2_enhanced_capabilities_new (void)
{
  Rtmp2EnhancedCapabilities *caps = g_new0 (Rtmp2EnhancedCapabilities, 1);
  caps->video_fourcc_info_map = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_free);
  return caps;
}

void
rtmp2_enhanced_capabilities_free (Rtmp2EnhancedCapabilities * caps)
{
  if (!caps)
    return;

  if (caps->video_fourcc_info_map)
    g_hash_table_destroy (caps->video_fourcc_info_map);

  g_free (caps);
}

/* static gboolean */
/* parse_video_fourcc_info_map (const guint8 ** data, gsize * size, */
/*     GHashTable * map, GError ** error) */
/* { */
/*   Rtmp2AmfValue value; */

/*   if (!rtmp2_amf0_parse (data, size, &value, error)) */
/*     return FALSE; */

/*   if (value.amf0_type != RTMP2_AMF0_ECMA_ARRAY) { */
/*     rtmp2_amf_value_free (&value); */
/*     return FALSE; */
/*   } */

/*   /\* Simplified - full implementation would parse ECMA array *\/ */
/*   rtmp2_amf_value_free (&value); */
/*   return TRUE; */
/* } */

gboolean
rtmp2_enhanced_parse_connect (const guint8 * data, gsize size,
    Rtmp2EnhancedCapabilities * client_caps, gdouble * transaction_id, GError ** error)
{
  const guint8 *ptr = data;
  gsize remaining = size;
  Rtmp2AmfValue value;
  gchar *command_name = NULL;
  GHashTable *command_object = NULL;

  if (transaction_id) {
    *transaction_id = 1.0;  /* Default if not found */
  }

  /* Parse command name */
  if (!rtmp2_amf0_parse (&ptr, &remaining, &value, error))
    return FALSE;
  if (value.amf0_type == RTMP2_AMF0_STRING) {
    command_name = g_strdup (value.value.string);
    g_free (value.value.string);
  } else {
    return FALSE;
  }

  if (g_strcmp0 (command_name, "connect") != 0) {
    g_free (command_name);
    return FALSE;
  }

  /* Parse transaction ID */
  if (!rtmp2_amf0_parse (&ptr, &remaining, &value, error)) {
    g_free (command_name);
    return FALSE;
  }
  if (value.amf0_type == RTMP2_AMF0_NUMBER && transaction_id) {
    *transaction_id = value.value.number;
  }

  /* Parse command object */
  if (!rtmp2_amf0_parse (&ptr, &remaining, &value, error)) {
    g_free (command_name);
    return FALSE;
  }
  if (value.amf0_type == RTMP2_AMF0_OBJECT) {
    command_object = value.value.object;

    /* Check for objectEncoding (AMF3 support) */
    Rtmp2AmfValue *obj_encoding =
        g_hash_table_lookup (command_object, "objectEncoding");
    if (obj_encoding && obj_encoding->amf0_type == RTMP2_AMF0_NUMBER) {
      if (obj_encoding->value.number == 3.0) {
        client_caps->supports_amf3 = TRUE;
      }
    }

    /* Check for capsEx */
    Rtmp2AmfValue *caps_ex = g_hash_table_lookup (command_object, "capsEx");
    if (caps_ex && caps_ex->amf0_type == RTMP2_AMF0_NUMBER) {
      client_caps->caps_ex = (guint8) caps_ex->value.number;
      client_caps->supports_reconnect =
          (client_caps->caps_ex & RTMP2_CAPS_RECONNECT) != 0;
      client_caps->supports_multitrack =
          (client_caps->caps_ex & RTMP2_CAPS_MULTITRACK) != 0;
      client_caps->supports_timestamp_nano_offset =
          (client_caps->caps_ex & RTMP2_CAPS_TIMESTAMP_NANO_OFFSET) != 0;
    }

    /* Check for videoFourCcInfoMap */
    Rtmp2AmfValue *fourcc_map =
        g_hash_table_lookup (command_object, "videoFourCcInfoMap");
    if (fourcc_map && fourcc_map->amf0_type == RTMP2_AMF0_OBJECT) {
      /* Parse video codec support */
      /* Simplified - full implementation would parse the map */
    }
  }

  g_free (command_name);
  /* Don't free command_object as it's part of the value structure */

  return TRUE;
}

gboolean
rtmp2_enhanced_send_connect_result (GByteArray * ba,
    Rtmp2EnhancedCapabilities * server_caps, gdouble transaction_id, GError ** error)
{
  guint8 amf0_string = RTMP2_AMF0_STRING;
  guint8 amf0_number = RTMP2_AMF0_NUMBER;
  guint8 amf0_object = RTMP2_AMF0_OBJECT;

  /* Write "_result" */
  g_byte_array_append (ba, &amf0_string, 1);
  rtmp2_amf0_write_string (ba, "_result");

  /* Write transaction ID (from connect command) */
  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, transaction_id);

  /* Write command object */
  g_byte_array_append (ba, &amf0_object, 1);

  /* Write fmsVer */
  rtmp2_amf0_write_object_property (ba, "fmsVer", "FMS/3,0,1,123");

  /* Write capabilities (as number, not string) */
  rtmp2_amf0_write_string (ba, "capabilities");
  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba, 31.0);

  /* Write capsEx if Enhanced RTMP is supported */
  if (server_caps) {
    rtmp2_amf0_write_string (ba, "capsEx");
    g_byte_array_append (ba, &amf0_number, 1);
    rtmp2_amf0_write_number (ba, (gdouble) server_caps->caps_ex);
  }

  /* Write objectEncoding if AMF3 is supported */
  if (server_caps && server_caps->supports_amf3) {
    rtmp2_amf0_write_string (ba, "objectEncoding");
    g_byte_array_append (ba, &amf0_number, 1);
    rtmp2_amf0_write_number (ba, 3.0);
  }

  /* Write videoFourCcInfoMap if Enhanced RTMP is supported */
  if (server_caps && server_caps->video_fourcc_info_map &&
      g_hash_table_size (server_caps->video_fourcc_info_map) > 0) {
    rtmp2_amf0_write_string (ba, "videoFourCcInfoMap");
    g_byte_array_append (ba, &amf0_object, 1);
    /* Write supported codecs */
    /* Simplified - full implementation would write the map */
    rtmp2_amf0_write_object_end (ba);
  }

  /* End object */
  rtmp2_amf0_write_object_end (ba);

  /* Information object */
  g_byte_array_append (ba, &amf0_object, 1);
  rtmp2_amf0_write_object_property (ba, "level", "status");
  rtmp2_amf0_write_object_property (ba, "code",
      "NetConnection.Connect.Success");
  rtmp2_amf0_write_object_property (ba, "description",
      "Connection succeeded.");

  rtmp2_amf0_write_string (ba, "objectEncoding");
  g_byte_array_append (ba, &amf0_number, 1);
  rtmp2_amf0_write_number (ba,
      (server_caps && server_caps->supports_amf3) ? 3.0 : 0.0);

  rtmp2_amf0_write_object_end (ba);

  return TRUE;
}

