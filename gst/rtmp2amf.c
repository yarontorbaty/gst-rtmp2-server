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

#include "rtmp2amf.h"
#include <string.h>

static guint8
read_uint8 (const guint8 ** data, gsize * size)
{
  guint8 val;
  if (*size < 1)
    return 0;
  val = (*data)[0];
  (*data)++;
  (*size)--;
  return val;
}

static guint16
read_uint16_be (const guint8 ** data, gsize * size)
{
  guint16 val = 0;
  if (*size < 2)
    return 0;
  val = ((*data)[0] << 8) | (*data)[1];
  *data += 2;
  *size -= 2;
  return val;
}

static guint32
read_uint32_be (const guint8 ** data, gsize * size)
{
  guint32 val = 0;
  if (*size < 4)
    return 0;
  val = ((*data)[0] << 24) | ((*data)[1] << 16) | ((*data)[2] << 8) | (*data)[3];
  *data += 4;
  *size -= 4;
  return val;
}

static gdouble
read_double (const guint8 ** data, gsize * size)
{
  union {
    gdouble d;
    guint64 i;
  } u;
  if (*size < 8)
    return 0.0;
  u.i = ((guint64) (*data)[0] << 56) | ((guint64) (*data)[1] << 48) |
      ((guint64) (*data)[2] << 40) | ((guint64) (*data)[3] << 32) |
      ((guint64) (*data)[4] << 24) | ((guint64) (*data)[5] << 16) |
      ((guint64) (*data)[6] << 8) | (*data)[7];
  *data += 8;
  *size -= 8;
  return u.d;
}

static gchar *
read_string (const guint8 ** data, gsize * size)
{
  guint16 len;
  gchar *str;

  if (*size < 2)
    return NULL;
  len = read_uint16_be (data, size);
  if (*size < len)
    return NULL;

  str = g_malloc (len + 1);
  memcpy (str, *data, len);
  str[len] = '\0';
  *data += len;
  *size -= len;

  return str;
}

gboolean
rtmp2_amf0_parse (const guint8 ** data, gsize * size, Rtmp2AmfValue * value,
    GError ** error)
{
  guint8 type;

  if (!data || !*data || !size || *size < 1 || !value)
    return FALSE;

  type = read_uint8 (data, size);
  value->amf0_type = (Rtmp2Amf0Type) type;
  value->is_amf3 = FALSE;

  switch (type) {
    case RTMP2_AMF0_NUMBER:
      if (*size < 8)
        return FALSE;
      value->value.number = read_double (data, size);
      break;

    case RTMP2_AMF0_BOOLEAN:
      if (*size < 1)
        return FALSE;
      value->value.boolean = read_uint8 (data, size) != 0;
      break;

    case RTMP2_AMF0_STRING:
      value->value.string = read_string (data, size);
      if (!value->value.string)
        return FALSE;
      break;

    case RTMP2_AMF0_NULL:
    case RTMP2_AMF0_UNDEFINED:
      break;

    case RTMP2_AMF0_OBJECT:
      value->value.object = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, (GDestroyNotify) rtmp2_amf_value_free);
      while (*size > 0) {
        gchar *key = read_string (data, size);
        if (!key)
          break;
        if (key[0] == '\0') {
          g_free (key);
          break;              /* Object end marker */
        }
        Rtmp2AmfValue *prop_value = g_new0 (Rtmp2AmfValue, 1);
        if (!rtmp2_amf0_parse (data, size, prop_value, error)) {
          g_free (key);
          g_free (prop_value);
          return FALSE;
        }
        g_hash_table_insert (value->value.object, key, prop_value);
      }
      break;

    case RTMP2_AMF0_AVMPLUS_OBJECT:
      /* Switch to AMF3 */
      value->is_amf3 = TRUE;
      return rtmp2_amf3_parse (data, size, value, error);

    default:
      g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
          "Unsupported AMF0 type: %d", type);
      return FALSE;
  }

  return TRUE;
}

gboolean
rtmp2_amf3_parse (const guint8 ** data, gsize * size, Rtmp2AmfValue * value,
    GError ** error)
{
  guint8 type;
  guint32 u29;

  if (!data || !*data || !size || *size < 1 || !value)
    return FALSE;

  type = read_uint8 (data, size);
  value->amf3_type = (Rtmp2Amf3Type) type;
  value->is_amf3 = TRUE;

  switch (type) {
    case RTMP2_AMF3_INTEGER:
      /* U29 encoding */
      if (*size < 1)
        return FALSE;
      u29 = read_uint8 (data, size);
      if (u29 < 0x80) {
        value->value.number = u29;
      } else if (u29 < 0xC0) {
        if (*size < 1)
          return FALSE;
        value->value.number = ((u29 & 0x7f) << 7) | read_uint8 (data, size);
      } else if (u29 < 0xE0) {
        if (*size < 2)
          return FALSE;
        value->value.number = ((u29 & 0x1f) << 14) |
            (read_uint8 (data, size) << 7) | read_uint8 (data, size);
      } else {
        if (*size < 4)
          return FALSE;
        value->value.number = ((u29 & 0x0f) << 24) |
            (read_uint8 (data, size) << 16) |
            (read_uint8 (data, size) << 8) | read_uint8 (data, size);
      }
      break;

    case RTMP2_AMF3_DOUBLE:
      if (*size < 8)
        return FALSE;
      value->value.number = read_double (data, size);
      break;

    case RTMP2_AMF3_STRING:
      /* U29 length + string */
      if (*size < 1)
        return FALSE;
      {
        guint32 len = read_uint8 (data, size);
        if (len & 0x01) {
          len = len >> 1;
          if (*size < len)
            return FALSE;
          value->value.string = g_malloc (len + 1);
          memcpy (value->value.string, *data, len);
          value->value.string[len] = '\0';
          *data += len;
          *size -= len;
        } else {
          /* Reference - simplified */
          return FALSE;
        }
      }
      break;

    case RTMP2_AMF3_FALSE:
      value->value.boolean = FALSE;
      break;

    case RTMP2_AMF3_TRUE:
      value->value.boolean = TRUE;
      break;

    case RTMP2_AMF3_NULL:
      break;

    default:
      g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
          "Unsupported AMF3 type: %d", type);
      return FALSE;
  }

  return TRUE;
}

gboolean
rtmp2_amf0_write_string (GByteArray * ba, const gchar * str)
{
  guint16 len = str ? strlen (str) : 0;
  guint8 data[2];
  data[0] = (len >> 8) & 0xff;
  data[1] = len & 0xff;
  g_byte_array_append (ba, data, 2);
  if (len > 0)
    g_byte_array_append (ba, (guint8 *) str, len);
  return TRUE;
}

gboolean
rtmp2_amf0_write_number (GByteArray * ba, gdouble num)
{
  union {
    gdouble d;
    guint64 i;
  } u;
  u.d = num;
  
  /* AMF0 numbers are 8-byte IEEE 754 doubles in big-endian (network) byte order */
  guint8 b[8];
  b[0] = (u.i >> 56) & 0xff;
  b[1] = (u.i >> 48) & 0xff;
  b[2] = (u.i >> 40) & 0xff;
  b[3] = (u.i >> 32) & 0xff;
  b[4] = (u.i >> 24) & 0xff;
  b[5] = (u.i >> 16) & 0xff;
  b[6] = (u.i >> 8) & 0xff;
  b[7] = u.i & 0xff;
  
  g_byte_array_append (ba, b, 8);
  return TRUE;
}

gboolean
rtmp2_amf0_write_boolean (GByteArray * ba, gboolean val)
{
  guint8 b = val ? 1 : 0;
  g_byte_array_append (ba, &b, 1);
  return TRUE;
}

gboolean
rtmp2_amf0_write_object_start (GByteArray * ba)
{
  /* Object type marker will be written by caller */
  return TRUE;
}

gboolean
rtmp2_amf0_write_object_property (GByteArray * ba, const gchar * name,
    const gchar * value)
{
  rtmp2_amf0_write_string (ba, name);
  rtmp2_amf0_write_string (ba, value);
  return TRUE;
}

gboolean
rtmp2_amf0_write_object_end (GByteArray * ba)
{
  guint8 end[3] = { 0, 0, RTMP2_AMF0_OBJECT_END };
  g_byte_array_append (ba, end, 3);
  return TRUE;
}

gboolean
rtmp2_amf0_write_null (GByteArray * ba)
{
  guint8 type = RTMP2_AMF0_NULL;
  g_byte_array_append (ba, &type, 1);
  return TRUE;
}

void
rtmp2_amf_value_free (Rtmp2AmfValue * value)
{
  if (!value)
    return;

  switch (value->is_amf3 ? 0 : value->amf0_type) {
    case RTMP2_AMF0_STRING:
      g_free (value->value.string);
      break;
    case RTMP2_AMF0_OBJECT:
      if (value->value.object)
        g_hash_table_destroy (value->value.object);
      break;
    default:
      break;
  }

  /* Only free the value struct if it was heap-allocated (not used in this codebase) */
  /* For stack-allocated values, caller should just let them go out of scope */
  /* g_free (value); */
}

