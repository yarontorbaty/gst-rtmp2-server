#!/bin/bash
# Test multiple simultaneous RTMP clients

set -e

PORT=1935

echo "Starting multiple client test..."

# Start server
gst-launch-1.0 rtmp2serversrc port=$PORT ! fakesink &
SERVER_PID=$!

sleep 2

# Start multiple FFmpeg clients if available
if command -v ffmpeg &> /dev/null; then
    echo "Starting multiple FFmpeg clients..."
    
    for i in 1 2 3; do
        ffmpeg -f lavfi -i testsrc=duration=2:size=320x240:rate=30 \
               -f lavfi -i sine=frequency=$((1000 + i * 100)):duration=2 \
               -c:v libx264 -preset ultrafast -tune zerolatency \
               -c:a aac -f flv rtmp://localhost:$PORT/live/stream$i &
        echo "Started client $i (PID: $!)"
    done
    
    sleep 5
    
    # Cleanup all clients
    pkill -f "ffmpeg.*rtmp://localhost:$PORT" || true
else
    echo "FFmpeg not found, skipping test"
fi

# Cleanup
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "Multiple client test completed"

