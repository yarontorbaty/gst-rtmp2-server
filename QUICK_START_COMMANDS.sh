#!/bin/bash
# Quick start commands for RTMP server

cd /Users/yarontorbaty/gst-rtmp2-server

echo "=== Cleaning up any existing processes on port 1935 ==="
lsof -ti:1935 | xargs kill -9 2>/dev/null || echo "No processes found on port 1935"
sleep 2

echo ""
echo "=== Setting up environment ==="
export GST_PLUGIN_PATH="$(pwd)/build"
echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"

echo ""
echo "=== Verifying plugin loads ==="
gst-inspect-1.0 rtmp2serversrc | head -15

echo ""
echo "=== Starting RTMP → SRT Server ==="
echo "RTMP input: localhost:1935"
echo "SRT output: localhost:8888"
echo ""
echo "Run this command:"
echo ""
echo "gst-launch-1.0 rtmp2serversrc port=1935 ! \\"
echo "    video/x-flv ! flvdemux name=demux \\"
echo "    demux.video ! queue ! h264parse ! mpegtsmux name=mux ! \\"
echo "    srtsink uri=\"srt://:8888?mode=listener\" \\"
echo "    demux.audio ! queue ! aacparse ! mux."
echo ""
echo "Then publish with:"
echo "  ffmpeg -re -f lavfi -i testsrc -c:v libx264 -t 10 -f flv rtmp://localhost:1935/live/test"
echo ""
echo "And receive with:"
echo "  ffplay srt://localhost:8888"

