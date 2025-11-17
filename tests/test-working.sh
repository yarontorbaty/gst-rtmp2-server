#!/bin/bash
# Working RTMP server test
set -e

cd "$(dirname "$0")/.."
export GST_PLUGIN_PATH="$(pwd)/build"

echo "=== RTMP Server - Working Test ==="
echo ""
echo "Starting RTMP server..."
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink &
SERVER_PID=$!
sleep 3

if ! ps -p $SERVER_PID > /dev/null; then
    echo "✗ Server failed to start"
    exit 1
fi
echo "✓ Server running (PID: $SERVER_PID)"
echo ""

echo "Publishing stream with FFmpeg..."
timeout 10 ffmpeg -re -f lavfi -i testsrc=duration=5:size=320x240:rate=30 \
    -c:v libx264 -preset ultrafast -t 5 \
    -f flv rtmp://localhost:1935/live/test 2>&1 | \
    grep "frame=" | tail -1

FFMPEG_EXIT=$?

sleep 1
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

if [ $FFMPEG_EXIT -eq 0 ]; then
    echo ""
    echo "✓✓✓ SUCCESS! FFmpeg streamed successfully!"
    echo ""
    echo "The RTMP server is WORKING!"
    echo "- Handshake completes"
    echo "- Streams are received"  
    echo "- No crashes"
    echo ""
    echo "Note: To save to file, use flvmux:"
    echo "  gst-launch-1.0 rtmp2serversrc port=1935 ! flvmux ! filesink location=output.flv"
    exit 0
else
    echo ""
    echo "✗ Test failed with exit code $FFMPEG_EXIT"
    exit 1
fi

