#!/bin/bash
# Basic test script for rtmp2serversrc

set -e

PORT=1935
TEST_STREAM_KEY="test123"

echo "Starting RTMP server test..."

# Start server in background
gst-launch-1.0 rtmp2serversrc port=$PORT ! fakesink &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Test with FFmpeg if available
if command -v ffmpeg &> /dev/null; then
    echo "Testing with FFmpeg..."
    # Create a test video (1 second)
    ffmpeg -f lavfi -i testsrc=duration=1:size=320x240:rate=30 \
           -f lavfi -i sine=frequency=1000:duration=1 \
           -c:v libx264 -preset ultrafast -tune zerolatency \
           -c:a aac -f flv rtmp://localhost:$PORT/live/$TEST_STREAM_KEY &
    FFMPEG_PID=$!
    
    sleep 3
    
    # Cleanup
    kill $FFMPEG_PID 2>/dev/null || true
else
    echo "FFmpeg not found, skipping FFmpeg test"
fi

# Cleanup
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "Test completed"

