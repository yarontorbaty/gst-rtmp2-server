#!/bin/bash
# Complete async reading test

cd /Users/yarontorbaty/gst-rtmp2-server

# Setup
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2serversrc:4,rtmp2client:5

# Clean
killall -9 gst-launch-1.0 ffmpeg 2>/dev/null
sleep 1
rm -f async_complete.flv async_complete.log

echo "=== Starting RTMP Server with Async Reading ==="
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=async_complete.flv 2>async_complete.log &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Wait for server to be ready
echo "Waiting for server to start..."
sleep 4

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server failed to start!"
    cat async_complete.log
    exit 1
fi

echo ""
echo "=== Streaming 2 seconds at 30fps (60 expected frames) ==="
ffmpeg -v error -stats -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test

FFMPEG_EXIT=$?
echo ""
echo "FFmpeg exit code: $FFMPEG_EXIT"

# Wait for data to flush
sleep 2

# Stop server gracefully
echo "Stopping server..."
kill -INT $SERVER_PID 2>/dev/null
sleep 2

# Force kill if needed
if kill -0 $SERVER_PID 2>/dev/null; then
    kill -9 $SERVER_PID 2>/dev/null
fi

echo ""
echo "========================================="
echo "           RESULTS"
echo "========================================="
echo ""

# File info
if [ -f async_complete.flv ]; then
    SIZE=$(stat -f%z async_complete.flv 2>/dev/null || stat -c%s async_complete.flv 2>/dev/null)
    echo "File: async_complete.flv"
    echo "Size: $SIZE bytes"
    ls -lh async_complete.flv
    echo ""
    
    # Frame analysis
    VIDEO_FRAMES=$(grep -c "type=9" async_complete.log 2>/dev/null || echo "0")
    FLV_TAGS=$(grep -c "Returning FLV" async_complete.log 2>/dev/null || echo "0")
    ASYNC_CALLBACKS=$(grep -c "Async read callback" async_complete.log 2>/dev/null || echo "0")
    PUBLISHING=$(grep -c "Client started publishing" async_complete.log 2>/dev/null || echo "0")
    
    echo "Video frames received (type=9): $VIDEO_FRAMES / 60 expected"
    echo "FLV tags returned: $FLV_TAGS"
    echo "Async callbacks triggered: $ASYNC_CALLBACKS"
    echo "Publishing state reached: $PUBLISHING"
    echo ""
    
    # Calculate percentage
    if [ $VIDEO_FRAMES -gt 0 ]; then
        PERCENT=$((VIDEO_FRAMES * 100 / 60))
        echo "Capture rate: $PERCENT%"
        echo ""
        
        if [ $PERCENT -ge 90 ]; then
            echo "✅ EXCELLENT! Async reading working perfectly!"
        elif [ $PERCENT -ge 50 ]; then
            echo "✅ GOOD! Major improvement from 20% baseline"
        elif [ $PERCENT -ge 25 ]; then
            echo "⚠️  IMPROVED from 20% baseline but needs more work"
        else
            echo "❌ No improvement over baseline (20%)"
        fi
    else
        echo "❌ No frames captured"
    fi
    
    # Video analysis if file has content
    if [ $SIZE -gt 1000 ]; then
        echo ""
        echo "Video analysis:"
        ffprobe async_complete.flv 2>&1 | grep -E "Duration|Stream|frame" || echo "Could not analyze"
    fi
else
    echo "❌ No output file created"
fi

echo ""
echo "Baseline comparison: 12 frames (20%)"
echo "Log file: async_complete.log"

