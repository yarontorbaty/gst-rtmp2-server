#!/bin/bash
cd /Users/yarontorbaty/gst-rtmp2-server

killall -9 gst-launch-1.0 ffmpeg 2>/dev/null
sleep 2
rm -f TEST_CLEAR.flv

export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2client:5

echo "=== Starting Server with Stale Chunk Clearing ===" 
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=TEST_CLEAR.flv 2>TEST_CLEAR_server.log &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"
sleep 5

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server died!"
    exit 1
fi

echo "=== Streaming 2 seconds (60 frames) ===" 
ffmpeg -v info -stats -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test 2>&1 | tee TEST_CLEAR_ffmpeg.log

echo "FFmpeg exit: $?"
sleep 3

kill -INT $SERVER_PID 2>/dev/null
sleep 2
if kill -0 $SERVER_PID 2>/dev/null; then
    kill -9 $SERVER_PID 2>/dev/null
fi

echo ""
echo "========== RESULTS =========="
echo "Video frames:" && grep -c "type=9" TEST_CLEAR_server.log
echo "Chunk clears:" && grep "Clearing.*stale" TEST_CLEAR_server.log | wc -l | xargs echo "Times cleared:"
echo "Publishing:" && grep "state=4.*publishing" TEST_CLEAR_server.log | head -1
echo ""
FRAMES=$(grep -c "type=9" TEST_CLEAR_server.log)
echo "Capture: $FRAMES / 60 frames = $(($FRAMES * 100 / 60))%"
echo ""
ls -lh TEST_CLEAR.flv

