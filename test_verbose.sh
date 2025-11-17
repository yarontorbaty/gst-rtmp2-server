#!/bin/bash
cd /Users/yarontorbaty/gst-rtmp2-server

killall -9 gst-launch-1.0 ffmpeg 2>/dev/null
rm -f verbose_test.flv
sleep 1

export GST_PLUGIN_PATH=$(pwd)/build  
export GST_DEBUG=rtmp2serversrc:5,rtmp2client:6

echo "=== Starting server ===" 
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=verbose_test.flv 2>&1 | tee verbose.log &
SERVER_PID=$!
sleep 4

echo "=== FFmpeg streaming with verbose output ===" 
ffmpeg -v verbose -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test 2>&1 | tee ffmpeg_verbose.log

sleep 2
kill -INT $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=== Analysis ==="
ls -lh verbose_test.flv 2>/dev/null
echo "Video frames (type=9):" && grep -c "type=9" verbose.log 2>/dev/null || echo "0"
echo "FLV tags:" && grep -c "Returning FLV" verbose.log 2>/dev/null || echo "0"
echo ""
echo "Async callbacks:" && grep -c "rtmp2_client_read_cb" verbose.log 2>/dev/null || echo "0"
echo "Publishing state:" && grep -i "publishing" verbose.log | tail -3

