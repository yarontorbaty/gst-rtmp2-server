# Quick Start Guide

## Build and Test

```bash
# 1. Build the plugin
meson setup build
meson compile -C build

# 2. Set plugin path
export GST_PLUGIN_PATH="$(pwd)/build"

# 3. Verify plugin loads
gst-inspect-1.0 rtmp2serversrc

# 4. Start server (in one terminal)
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv

# 5. Push stream (in another terminal, if you have FFmpeg)
ffmpeg -f lavfi -i testsrc=duration=5:size=320x240:rate=30 \
       -f lavfi -i sine=frequency=1000:duration=5 \
       -c:v libx264 -preset ultrafast -tune zerolatency \
       -c:a aac -f flv rtmp://localhost:1935/live/teststream
```

## Test Scripts

```bash
# Run automated tests
./tests/test-gstreamer.sh
./tests/test-with-ffmpeg.sh
./tests/test-rtmps.sh
```

See `tests/TESTING.md` for detailed testing instructions.
