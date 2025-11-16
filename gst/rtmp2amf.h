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

#ifndef __RTMP2_AMF_H__
#define __RTMP2_AMF_H__

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum {
  RTMP2_AMF0_NUMBER = 0,
  RTMP2_AMF0_BOOLEAN = 1,
  RTMP2_AMF0_STRING = 2,
  RTMP2_AMF0_OBJECT = 3,
  RTMP2_AMF0_NULL = 5,
  RTMP2_AMF0_UNDEFINED = 6,
  RTMP2_AMF0_REFERENCE = 7,
  RTMP2_AMF0_ECMA_ARRAY = 8,
  RTMP2_AMF0_OBJECT_END = 9,
  RTMP2_AMF0_STRICT_ARRAY = 10,
  RTMP2_AMF0_DATE = 11,
  RTMP2_AMF0_LONG_STRING = 12,
  RTMP2_AMF0_XML_DOCUMENT = 15,
  RTMP2_AMF0_TYPED_OBJECT = 16,
  RTMP2_AMF0_AVMPLUS_OBJECT = 17  /* Switch to AMF3 */
} Rtmp2Amf0Type;

typedef enum {
  RTMP2_AMF3_UNDEFINED = 0,
  RTMP2_AMF3_NULL = 1,
  RTMP2_AMF3_FALSE = 2,
  RTMP2_AMF3_TRUE = 3,
  RTMP2_AMF3_INTEGER = 4,
  RTMP2_AMF3_DOUBLE = 5,
  RTMP2_AMF3_STRING = 6,
  RTMP2_AMF3_XML_DOCUMENT = 7,
  RTMP2_AMF3_DATE = 8,
  RTMP2_AMF3_ARRAY = 9,
  RTMP2_AMF3_OBJECT = 10,
  RTMP2_AMF3_XML = 11,
  RTMP2_AMF3_BYTE_ARRAY = 12
} Rtmp2Amf3Type;

typedef struct _Rtmp2AmfValue Rtmp2AmfValue;

struct _Rtmp2AmfValue {
  Rtmp2Amf0Type amf0_type;
  Rtmp2Amf3Type amf3_type;
  gboolean is_amf3;
  
  union {
    gdouble number;
    gboolean boolean;
    gchar *string;
    GHashTable *object;
    GArray *array;
    GByteArray *byte_array;
  } value;
};

typedef struct {
  gboolean supports_amf3;
  guint8 object_encoding;  /* 0 = AMF0 only, 3 = AMF0 + AMF3 */
} Rtmp2AmfContext;

gboolean rtmp2_amf0_parse (const guint8 ** data, gsize * size, Rtmp2AmfValue * value, GError ** error);
gboolean rtmp2_amf3_parse (const guint8 ** data, gsize * size, Rtmp2AmfValue * value, GError ** error);
gboolean rtmp2_amf0_write_string (GByteArray * ba, const gchar * str);
gboolean rtmp2_amf0_write_number (GByteArray * ba, gdouble num);
gboolean rtmp2_amf0_write_boolean (GByteArray * ba, gboolean val);
gboolean rtmp2_amf0_write_object_start (GByteArray * ba);
gboolean rtmp2_amf0_write_object_property (GByteArray * ba, const gchar * name, const gchar * value);
gboolean rtmp2_amf0_write_object_end (GByteArray * ba);
gboolean rtmp2_amf0_write_null (GByteArray * ba);
void rtmp2_amf_value_free (Rtmp2AmfValue * value);

G_END_DECLS

#endif /* __RTMP2_AMF_H__ */

