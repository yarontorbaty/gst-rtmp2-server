#!/bin/bash
# Simple frame capture test

cd /Users/yarontorbaty/gst-rtmp2-server

# Clean up
killall -9 gst-launch-1.0 ffmpeg 2>/dev/null
lsof -ti:1935 | xargs kill -9 2>/dev/null
rm -f test_optimized.flv
sleep 1

# Export paths
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2serversrc:4,rtmp2client:4

echo "=== Starting RTMP server in background ==="
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=test_optimized.flv > server_output.log 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Wait for server to be ready
echo "Waiting for server to start..."
sleep 5

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server failed to start"
    cat server_output.log
    exit 1
fi

echo ""
echo "=== Publishing 2-second test stream ==="
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test 2>&1 | grep -E "(frame=|error|Error)"

FFMPEG_STATUS=$?
echo "FFmpeg exit status: $FFMPEG_STATUS"

# Wait a moment for data to flush
sleep 2

# Stop server gracefully
echo ""
echo "=== Stopping server ==="
kill -INT $SERVER_PID 2>/dev/null
sleep 2

# Force kill if still running
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "Force killing server..."
    kill -9 $SERVER_PID 2>/dev/null
fi

echo ""
echo "=== Results ==="
if [ -f test_optimized.flv ]; then
    FILE_SIZE=$(ls -l test_optimized.flv | awk '{print $5}')
    echo "File size: $FILE_SIZE bytes"
    ls -lh test_optimized.flv
    
    if [ $FILE_SIZE -gt 1000 ]; then
        echo ""
        echo "File analysis:"
        ffprobe test_optimized.flv 2>&1 | grep -E "(Duration|Stream|bitrate|frame)"
        
        echo ""
        echo "Frame count from logs:"
        grep -i "Created FLV tag" server_output.log 2>/dev/null | wc -l | xargs echo "Created FLV tags:"
        grep -i "Returning FLV" server_output.log 2>/dev/null | wc -l | xargs echo "Returned FLV tags:"
        
        echo ""
        if [ $FILE_SIZE -gt 50000 ]; then
            echo "✅ SUCCESS! File size indicates good frame capture ($FILE_SIZE bytes)"
        else
            echo "⚠️  File size smaller than expected (expected ~50-100KB, got $FILE_SIZE bytes)"
        fi
    else
        echo "❌ FAILED: File too small ($FILE_SIZE bytes), likely only FLV header"
        echo ""
        echo "Server log:"
        tail -50 server_output.log
    fi
else
    echo "❌ ERROR: test_optimized.flv not created!"
    echo ""
    echo "Server log:"
    cat server_output.log
fi

