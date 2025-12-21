#!/bin/bash
cd /Users/yarontorbaty/gst-rtmp2-server/build

echo "Rebuilding with detailed parser logging..."

cc -c ../gst/rtmp2chunk_v2.c -o libgstrtmp2server.dylib.p/gst_rtmp2chunk_v2.c.o \
  -Ilibgstrtmp2server.dylib.p -I. -I.. \
  -I/opt/homebrew/Cellar/gstreamer/1.26.7_1/include/gstreamer-1.0 \
  -I/opt/homebrew/Cellar/glib/2.86.1/include \
  -I/Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk/usr/include/ffi \
  -I/opt/homebrew/Cellar/glib/2.86.1/include/glib-2.0 \
  -I/opt/homebrew/Cellar/glib/2.86.1/lib/glib-2.0/include \
  -I/opt/homebrew/opt/gettext/include \
  -I/opt/homebrew/Cellar/pcre2/10.47/include \
  -I/opt/homebrew/Cellar/orc/0.4.41/include/orc-0.4 \
  -fdiagnostics-color=always -Wall -Winvalid-pch -O2 -g -DHAVE_CONFIG_H

cc -Wl,-dead_strip_dylibs -Wl,-headerpad_max_install_names \
  -shared -install_name @rpath/libgstrtmp2server.dylib \
  -o libgstrtmp2server.dylib libgstrtmp2server.dylib.p/*.o \
  -Wl,-rpath,/opt/homebrew/Cellar/glib/2.86.1/lib \
  -Wl,-rpath,/opt/homebrew/opt/gettext/lib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstreamer-1.0.dylib \
  /opt/homebrew/Cellar/glib/2.86.1/lib/libgobject-2.0.dylib \
  /opt/homebrew/Cellar/glib/2.86.1/lib/libglib-2.0.dylib \
  /opt/homebrew/opt/gettext/lib/libintl.dylib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstbase-1.0.dylib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstapp-1.0.dylib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstvideo-1.0.dylib \
  /opt/homebrew/Cellar/gstreamer/1.26.7_1/lib/libgstaudio-1.0.dylib \
  /opt/homebrew/Cellar/glib/2.86.1/lib/libgio-2.0.dylib

echo ""
echo "Done - now run with GST_DEBUG=rtmp2chunk_v2:6 to see detailed parsing"

