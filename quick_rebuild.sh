#!/bin/bash
cd /Users/yarontorbaty/gst-rtmp2-server

# Create build directory if needed
mkdir -p build/libgstrtmp2server.dylib.p
cd build

# Get flags from pkg-config
CFLAGS=$(pkg-config --cflags gstreamer-1.0 gstreamer-base-1.0 glib-2.0 gio-2.0)
LIBS=$(pkg-config --libs gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-audio-1.0 glib-2.0 gio-2.0)

if [ -z "$CFLAGS" ]; then
  echo "❌ pkg-config failed - GStreamer not found"
  exit 1
fi

# Compile all source files
for src in rtmp2chunk_v2.c gstrtmp2serversrc.c rtmp2client.c rtmp2chunk.c rtmp2amf.c rtmp2handshake.c rtmp2flv.c rtmp2enhanced.c gstrtmp2server.c; do
  if [ -f "../gst/$src" ]; then
    echo "Compiling $src..."
    cc -c "../gst/$src" -o "libgstrtmp2server.dylib.p/gst_${src%.c}.o" \
      -I. -I.. -I../gst -I../build \
      $CFLAGS \
      -fdiagnostics-color=always -Wall -O2 -g -DHAVE_CONFIG_H 2>&1
  fi
done

# Check if we have object files
if ! ls libgstrtmp2server.dylib.p/*.o 1>/dev/null 2>&1; then
  echo "❌ No object files created"
  exit 1
fi

# Link
echo "Linking..."
cc -Wl,-dead_strip_dylibs \
  -shared -install_name @rpath/libgstrtmp2server.dylib \
  -o libgstrtmp2server.dylib libgstrtmp2server.dylib.p/*.o \
  $LIBS

if [ -f libgstrtmp2server.dylib ]; then
  echo "✅ Build complete: build/libgstrtmp2server.dylib"
else
  echo "❌ Build failed"
  exit 1
fi
