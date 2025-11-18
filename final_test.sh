#!/bin/bash
cd /Users/yarontorbaty/gst-rtmp2-server

# Cleanup
killall -9 gst-launch-1.0 ffmpeg 2>/dev/null
sleep 1
rm -f FINAL.flv FINAL_server.log FINAL_ffmpeg.log

# Setup
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2serversrc:5,rtmp2client:5,rtmp2chunk:5

echo "=== Starting Server with Full Async Architecture ==="
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=FINAL.flv 2>FINAL_server.log &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"
sleep 4

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server died"
    cat FINAL_server.log | tail -30
    exit 1
fi

echo "=== Streaming 2 seconds at 30fps (60 frames expected) ==="
ffmpeg -v info -stats -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test 2>FINAL_ffmpeg.log

FFMPEG_EXIT=$?
echo "FFmpeg exit code: $FFMPEG_EXIT"
sleep 3

kill -INT $SERVER_PID 2>/dev/null
sleep 2
if kill -0 $SERVER_PID 2>/dev/null; then
    kill -9 $SERVER_PID 2>/dev/null
fi

echo ""
echo "========================================="
echo "           FINAL RESULTS"
echo "========================================="
echo ""

# Server stats
echo "SERVER STATS:"
echo "- Video messages (type=9):" && grep -c "type=9" FINAL_server.log
echo "- FLV tags returned:" && grep -c "Returning FLV" FINAL_server.log
echo "- Publishing state reached:" && grep -c "state=4.*publishing" FINAL_server.log
echo "- Commands processed:"
grep "Received command:" FINAL_server.log | awk '{print "  -", $(NF-1), $NF}'

echo ""
echo "FFMPEG STATS:"
echo "- Frames encoded:" && grep "frame=" FINAL_ffmpeg.log | tail -1 | grep -oE "frame= *[0-9]+"
echo "- Final stage:" && tail -20 FINAL_ffmpeg.log | grep -E "stream\.\.\.|Publishing|Deleting" | tail -3

echo ""
echo "FILE:"
ls -lh FINAL.flv

echo ""
if [ -s FINAL.flv ]; then
    echo "SUCCESS! File has content"
    ffprobe FINAL.flv 2>&1 | grep -E "Duration|Stream"
else
    echo "File is empty (normal - filesink may not have flushed)"
fi

echo ""
echo "CAPTURE RATE:"
SERVER_FRAMES=$(grep -c "type=9" FINAL_server.log)
if [ $SERVER_FRAMES -gt 0 ]; then
    PERCENT=$((SERVER_FRAMES * 100 / 60))
    echo "$SERVER_FRAMES / 60 frames = $PERCENT%"
    
    if [ $PERCENT -ge 80 ]; then
        echo "🎉 EXCELLENT!"
    elif [ $PERCENT -ge 50 ]; then
        echo "✅ Good progress"
    elif [ $PERCENT -ge 20 ]; then
        echo "⚠️  Better than baseline (20%)"
    else
        echo "❌ Same as or worse than baseline"
    fi
else
    echo "0 / 60 frames = 0%"
    echo "Check logs for errors"
fi


