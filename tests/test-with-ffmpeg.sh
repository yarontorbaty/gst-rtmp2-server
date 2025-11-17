#!/bin/bash
# Test RTMP server with FFmpeg as client

set -e

PORT=1935
STREAM_KEY="teststream"
OUTPUT_FILE="received_stream.flv"

export GST_PLUGIN_PATH="$(cd "$(dirname "$0")/.." && pwd)/build"
export GST_DEBUG=rtmp2serversrc:4

echo "=== Testing RTMP Server with FFmpeg Client ==="
echo ""

# Check if FFmpeg is available
if ! command -v ffmpeg &> /dev/null; then
    echo "FFmpeg not found. Please install FFmpeg to run this test."
    exit 1
fi

echo "Starting GStreamer RTMP server..."
gst-launch-1.0 rtmp2serversrc port=$PORT ! filesink location=$OUTPUT_FILE 2>&1 &
SERVER_PID=$!

sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo "✗ Server failed to start"
    exit 1
fi

echo "✓ Server running (PID: $SERVER_PID)"
echo ""

# Test with FFmpeg
echo "Pushing test stream from FFmpeg..."
echo "Command: ffmpeg -f lavfi -i testsrc=duration=5:size=320x240:rate=30 -f lavfi -i sine=frequency=1000:duration=5 -c:v libx264 -preset ultrafast -tune zerolatency -c:a aac -f flv rtmp://localhost:$PORT/live/$STREAM_KEY"
echo ""

timeout 8 ffmpeg -f lavfi -i testsrc=duration=5:size=320x240:rate=30 \
       -f lavfi -i sine=frequency=1000:duration=5 \
       -c:v libx264 -preset ultrafast -tune zerolatency \
       -c:a aac -f flv rtmp://localhost:$PORT/live/$STREAM_KEY 2>&1 | grep -E "(error|Error|ERROR)" || true

sleep 2

# Stop server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

# Check output
if [ -f "$OUTPUT_FILE" ] && [ -s "$OUTPUT_FILE" ]; then
    echo ""
    echo "✓ Stream received successfully!"
    echo "Output file: $OUTPUT_FILE ($(du -h "$OUTPUT_FILE" | cut -f1))"
    echo ""
    echo "You can play it with:"
    echo "  gst-play-1.0 $OUTPUT_FILE"
    echo "  or"
    echo "  ffplay $OUTPUT_FILE"
else
    echo ""
    echo "✗ No output file created or file is empty"
    rm -f "$OUTPUT_FILE"
    exit 1
fi

echo ""
echo "=== Test completed successfully ==="

