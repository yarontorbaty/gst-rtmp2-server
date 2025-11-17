#!/bin/bash
# End-to-end test: RTMP input → SRT output
set -e

cd "$(dirname "$0")/.."
export GST_PLUGIN_PATH="$(pwd)/build"

echo "=== RTMP to SRT End-to-End Test ==="
echo ""

# Start RTMP server that outputs to SRT
echo "Starting RTMP→SRT bridge on port 1935 (RTMP) → port 8888 (SRT)..."
gst-launch-1.0 -v rtmp2serversrc port=1935 ! \
    queue ! \
    decodebin ! videoconvert ! x264enc tune=zerolatency ! \
    mpegtsmux ! srtsink uri="srt://:8888?mode=listener" 2>&1 &
SERVER_PID=$!

sleep 4

if ! ps -p $SERVER_PID > /dev/null; then
    echo "✗ Server failed to start"
    exit 1
fi
echo "✓ RTMP→SRT server running (PID: $SERVER_PID)"
echo ""

# Start SRT receiver that saves to file
echo "Starting SRT receiver on port 8888..."
timeout 15 gst-launch-1.0 -v srtsrc uri="srt://localhost:8888" ! \
    filesink location=received_srt.ts 2>&1 &
RECEIVER_PID=$!
sleep 2

# Publish RTMP stream
echo "Publishing RTMP stream..."
echo ""
timeout 10 ffmpeg -re -f lavfi -i testsrc=duration=5:size=320x240:rate=30 \
    -c:v libx264 -preset ultrafast -t 5 \
    -f flv rtmp://localhost:1935/live/test 2>&1 | \
    grep "frame=" | tail -3

sleep 2

# Cleanup
kill $SERVER_PID $RECEIVER_PID 2>/dev/null
wait 2>/dev/null

echo ""
echo "=== Results ==="
if [ -f received_srt.ts ] && [ -s received_srt.ts ]; then
    SIZE=$(ls -lh received_srt.ts | awk '{print $5}')
    echo "✓ SRT file created: received_srt.ts ($SIZE)"
    echo ""
    echo "✓✓✓ END-TO-END SUCCESS!"
    echo "RTMP server received stream and output to SRT successfully!"
else
    echo "✗ No SRT output file created"
    ls -lh received_srt.ts 2>/dev/null || echo "(file doesn't exist)"
fi

