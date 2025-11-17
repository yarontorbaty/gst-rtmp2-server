#!/bin/bash
cd /Users/yarontorbaty/gst-rtmp2-server

# Cleanup
killall -9 gst-launch-1.0 ffmpeg 2>/dev/null
rm -f optimized_test.flv
sleep 1

# Setup
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2serversrc:4,rtmp2client:4

echo "=== Testing Frame Capture with Optimizations ==="
echo ""

# Start server
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=optimized_test.flv 2>server.log &
SERVER_PID=$!
echo "Server started (PID: $SERVER_PID)"
sleep 3

# Stream 2 seconds of video
echo "Streaming 2 seconds of 30fps video..."
ffmpeg -loglevel error -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 -f flv rtmp://localhost:1935/live/test
FFMPEG_EXIT=$?

echo "FFmpeg exit code: $FFMPEG_EXIT"
sleep 3

# Stop server gracefully
kill -INT $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=== Results ==="
if [ -f optimized_test.flv ]; then
    SIZE=$(stat -f%z optimized_test.flv)
    echo "File: optimized_test.flv"
    echo "Size: $SIZE bytes"
    ls -lh optimized_test.flv
    
    if [ $SIZE -gt 1000 ]; then
        echo ""
        ffprobe -v quiet -show_entries format=duration,size,bit_rate -show_entries stream=codec_name,codec_type,nb_frames optimized_test.flv 2>/dev/null || ffprobe optimized_test.flv 2>&1 | grep -E "Duration|Stream|frame"
        
        echo ""
        FLV_TAGS=$(grep "Returning FLV tag" server.log | wc -l | tr -d ' ')
        echo "FLV tags returned: $FLV_TAGS"
        
        if [ $SIZE -gt 50000 ]; then
            echo ""
            echo "✅ SUCCESS! Captured significant video data"
            echo "Expected ~60 frames, file size indicates good capture"
        elif [ $SIZE -gt 10000 ]; then
            echo ""
            echo "⚠️ PARTIAL: Some frames captured but less than expected"
            echo "Expected ~50-100KB, got $(($SIZE/1024))KB"
        else
            echo ""
            echo "⚠️ LIMITED: File created but small"
        fi
    else
        echo "❌ File too small (likely header only)"
    fi
else
    echo "❌ No output file created"
fi

