#!/bin/bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

echo "Testing baseline audio+video (should get 39/60 frames)..."
rm -f baseline_av.flv

gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=baseline_av.flv &
PID=$!
sleep 3

ffmpeg -hide_banner -loglevel error -re \
  -f lavfi -i testsrc=duration=2:rate=30 \
  -f lavfi -i sine=duration=2 \
  -c:v libx264 -preset ultrafast -g 30 \
  -c:a aac -f flv rtmp://localhost:1935/live/test

sleep 1
kill $PID 2>/dev/null || true
sleep 1

VIDEO=$(ffprobe -v error -select_streams v:0 -count_frames -show_entries stream=nb_read_frames -of default=noprint_wrappers=1:nokey=1 baseline_av.flv 2>/dev/null)
echo "Result: $VIDEO/60 video frames"

if [ "$VIDEO" -ge 35 ]; then
  echo "SUCCESS - baseline working"
  exit 0
else
  echo "BROKEN - only got $VIDEO frames"
  exit 1
fi

