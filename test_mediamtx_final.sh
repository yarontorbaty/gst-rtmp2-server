#!/bin/bash
# Final test of MediaMTX pattern: blocking socket + buffered input + dedicated thread

cd /Users/yarontorbaty/gst-rtmp2-server

# Cleanup
killall -9 gst-launch-1.0 ffmpeg 2>/dev/null
sleep 2
rm -f MEDIAMTX_FINAL.flv

# Setup
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2client:5

echo "=== Testing MediaMTX Pattern ==="
echo "Blocking socket + GBufferedInputStream + Dedicated read thread"
echo ""

gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=MEDIAMTX_FINAL.flv 2>mediamtx_final.log &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"
sleep 5

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server died!"
    cat mediamtx_final.log | tail -30
    exit 1
fi

echo "Streaming 2 seconds (60 frames)..."
ffmpeg -v error -stats -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test

FFMPEG_EXIT=$?
echo "FFmpeg exit: $FFMPEG_EXIT"
sleep 3

kill -INT $SERVER_PID 2>/dev/null
sleep 2
if kill -0 $SERVER_PID 2>/dev/null; then
    kill -9 $SERVER_PID 2>/dev/null
fi

echo ""
echo "============================================"
echo "   MEDIAMTX PATTERN TEST RESULTS"
echo "============================================"
echo ""

# Check if blocking mode was set
echo "1. Socket switched to BLOCKING:"
grep -c "BLOCKING mode" mediamtx_final.log

echo ""
echo "2. Read thread started:"
grep "Client read thread started" mediamtx_final.log | head -1

echo ""
echo "3. Commands processed:"
grep "Received command:" mediamtx_final.log | awk '{print "  -", $NF}'

echo ""
echo "4. Publishing state:"
grep "state=4.*publishing" mediamtx_final.log | head -1

echo ""
echo "5. Video frames captured:"
FRAMES=$(grep -c "type=9" mediamtx_final.log)
echo "   $FRAMES frames"

echo ""
echo "6. FLV tags returned:"
grep -c "Returning FLV" mediamtx_final.log

echo ""
echo "7. File size:"
ls -lh MEDIAMTX_FINAL.flv

echo ""
echo "============================================"
if [ $FRAMES -gt 0 ]; then
    PERCENT=$((FRAMES * 100 / 60))
    echo "CAPTURE RATE: $FRAMES / 60 = $PERCENT%"
    echo ""
    
    if [ $PERCENT -ge 90 ]; then
        echo "🎉🎉🎉 SUCCESS! 90%+ capture achieved!"
        echo "MediaMTX pattern working perfectly!"
    elif [ $PERCENT -ge 70 ]; then
        echo "✅ EXCELLENT! 70%+ capture"
        echo "Much better than baseline 20%"
    elif [ $PERCENT -ge 50 ]; then
        echo "✅ GOOD! 50%+ capture"  
        echo "Significant improvement over baseline"
    elif [ $PERCENT -ge 25 ]; then
        echo "⚠️  IMPROVED over baseline (20%)"
        echo "But still room for optimization"
    else
        echo "❌ Same as baseline or worse"
        echo "MediaMTX pattern needs more work"
    fi
else
    echo "❌ FAILED: No frames captured"
    echo "Check mediamtx_final.log for errors"
fi

echo ""
echo "Log file: mediamtx_final.log ($(wc -l < mediamtx_final.log) lines)"

