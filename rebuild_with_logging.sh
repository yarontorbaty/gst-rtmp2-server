#!/bin/bash
# Rebuild with detailed logging

cd /Users/yarontorbaty/gst-rtmp2-server/build

echo "Compiling gstrtmp2serversrc.c with logging..."
cc -c ../gst/gstrtmp2serversrc.c -o libgstrtmp2server.dylib.p/gst_gstrtmp2serversrc.c.o \
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

if [ $? -ne 0 ]; then
  echo "Compilation failed!"
  exit 1
fi

echo "Linking..."
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

if [ $? -ne 0 ]; then
  echo "Linking failed!"
  exit 1
fi

echo ""
echo "✅ Rebuild complete with detailed logging"
echo ""
echo "Test with:"
echo "  cd /Users/yarontorbaty/gst-rtmp2-server"
echo "  export GST_PLUGIN_PATH=\$(pwd)/build"
echo "  export GST_DEBUG=rtmp2serversrc:5,rtmp2client:4"
echo "  gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=test.flv"

