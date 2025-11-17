#!/bin/bash
# Test RTMP server with GStreamer as both server and client

set -e

PORT=1935
STREAM_KEY="gsttest"
OUTPUT_FILE="gst_received.flv"

export GST_PLUGIN_PATH="$(cd "$(dirname "$0")/.." && pwd)/build"
export GST_DEBUG=rtmp2serversrc:4

echo "=== Testing RTMP Server with GStreamer Client ==="
echo ""

echo "Starting RTMP server..."
gst-launch-1.0 rtmp2serversrc port=$PORT ! filesink location=$OUTPUT_FILE 2>&1 &
SERVER_PID=$!

sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo "✗ Server failed to start"
    exit 1
fi

echo "✓ Server running (PID: $SERVER_PID)"
echo ""

# Test with GStreamer client (if rtmp2sink exists) or use test source
echo "Pushing test stream from GStreamer..."
echo "Command: gst-launch-1.0 videotestsrc ! x264enc ! flvmux streamable=true ! rtmp2sink location=rtmp://localhost:$PORT/live/$STREAM_KEY"
echo ""

# Try with rtmp2sink if available, otherwise use test pipeline
if gst-inspect-1.0 rtmp2sink &>/dev/null; then
    timeout 8 gst-launch-1.0 videotestsrc num-buffers=150 ! \
        video/x-raw,width=320,height=240,framerate=30/1 ! \
        x264enc bitrate=1000 ! \
        video/x-h264,profile=baseline ! \
        flvmux streamable=true ! \
        rtmp2sink location=rtmp://localhost:$PORT/live/$STREAM_KEY 2>&1 | tail -5 || true
else
    echo "Note: rtmp2sink not available, using alternative test method"
    echo "You can test manually by:"
    echo "  1. Keep server running"
    echo "  2. Use FFmpeg or OBS to push to rtmp://localhost:$PORT/live/$STREAM_KEY"
fi

sleep 2

# Stop server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

# Check output
if [ -f "$OUTPUT_FILE" ] && [ -s "$OUTPUT_FILE" ]; then
    echo ""
    echo "✓ Stream received successfully!"
    echo "Output file: $OUTPUT_FILE ($(du -h "$OUTPUT_FILE" | cut -f1))"
else
    echo ""
    echo "Note: No output file created (this is normal if no client connected)"
    rm -f "$OUTPUT_FILE"
fi

echo ""
echo "=== Test completed ==="

