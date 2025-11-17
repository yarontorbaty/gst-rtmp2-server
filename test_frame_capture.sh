#!/bin/bash
# Test frame capture optimization

cd /Users/yarontorbaty/gst-rtmp2-server

# Clean up
lsof -ti:1935 | xargs kill -9 2>/dev/null
rm -f test_optimized.flv
sleep 1

# Export paths
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2serversrc:5,rtmp2client:5

echo "=== Starting RTMP server ==="
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=test_optimized.flv 2>&1 | tee server_test.log &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"
sleep 3

echo ""
echo "=== Publishing 2-second test stream ==="
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test 2>&1 | tee ffmpeg_test.log

echo ""
echo "=== Waiting for server to finish writing ==="
sleep 2

echo ""
echo "=== Stopping server ==="
kill -INT $SERVER_PID 2>/dev/null
sleep 2

echo ""
echo "=== Results ==="
if [ -f test_optimized.flv ]; then
    ls -lh test_optimized.flv
    echo ""
    echo "File analysis:"
    ffprobe test_optimized.flv 2>&1 | grep -E "(Duration|Stream|bitrate|frame)"
    
    echo ""
    echo "Frame count from logs:"
    grep "Created FLV tag" server_test.log 2>/dev/null | wc -l | xargs echo "Created FLV tags:"
    grep "Returning FLV" server_test.log 2>/dev/null | wc -l | xargs echo "Returned FLV tags:"
else
    echo "ERROR: test_optimized.flv not created!"
fi

echo ""
echo "=== Expected: ~60 frames (2 sec × 30 fps) ==="
echo "=== File size should be ~50-100 KB ==="

