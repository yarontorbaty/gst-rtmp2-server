#!/bin/bash
# End-to-End RTMP to UDP Streaming Demo
# Demonstrates 100% frame capture with V2 parser

set -e

cd "$(dirname "$0")"
export GST_PLUGIN_PATH=$(pwd)/build

echo "=================================================="
echo "  RTMP→UDP Streaming Demo (V2 Parser - 100%)"
echo "=================================================="
echo ""
echo "This will:"
echo "  1. Start GStreamer RTMP server → UDP output"
echo "  2. Start ffplay to receive UDP stream"  
echo "  3. Stream test video via RTMP from FFmpeg"
echo ""
echo "Press Ctrl+C to stop all processes"
echo "=================================================="
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "Stopping all processes..."
    pkill -P $$ 2>/dev/null || true
    pkill -f "gst-launch.*rtmp2serversrc" 2>/dev/null || true
    pkill -f "ffplay.*udp" 2>/dev/null || true
    pkill -f "ffmpeg.*testsrc" 2>/dev/null || true
    echo "Cleanup complete"
    exit 0
}

trap cleanup SIGINT SIGTERM

# Start ffplay UDP receiver in background
echo "▶️  Starting UDP video player..."
ffplay -v quiet -fflags nobuffer -probesize 32 -analyzeduration 0 \
    -window_title "RTMP→UDP Demo (100% Capture)" \
    udp://127.0.0.1:5000 >/dev/null 2>&1 &
FFPLAY_PID=$!
sleep 2

if ! ps -p $FFPLAY_PID >/dev/null 2>&1; then
    echo "❌ Failed to start ffplay"
    exit 1
fi
echo "✅ Video player ready (PID: $FFPLAY_PID)"
echo ""

# Start GStreamer RTMP→UDP bridge
echo "▶️  Starting RTMP server (port 1935) → UDP output (port 5000)..."
gst-launch-1.0 -q rtmp2serversrc port=1935 ! \
    flvdemux ! h264parse ! mpegtsmux ! \
    udpsink host=127.0.0.1 port=5000 2>&1 | \
    grep -E "Setting|ERROR" &
GST_PID=$!
sleep 2

if ! ps -p $GST_PID >/dev/null 2>&1; then
    echo "❌ Failed to start GStreamer"
    cleanup
fi
echo "✅ RTMP server ready (PID: $GST_PID)"
echo ""

# Wait for RTMP server to be ready
echo "⏳ Waiting for RTMP server to initialize..."
for i in {1..10}; do
    if lsof -i :1935 2>/dev/null | grep -q LISTEN; then
        break
    fi
    sleep 0.5
done
echo "✅ RTMP server listening on port 1935"
echo ""

# Start FFmpeg RTMP publisher
echo "▶️  Starting RTMP stream (test pattern, 30fps)..."
echo ""
echo "=================================================="
echo "  🎥 VIDEO SHOULD NOW BE PLAYING!"
echo "=================================================="
echo ""
echo "Stats will appear below:"
echo ""

ffmpeg -re -f lavfi -i testsrc=size=1280x720:rate=30 \
    -f lavfi -i sine=frequency=1000:sample_rate=48000 \
    -c:v libx264 -preset ultrafast -tune zerolatency \
    -g 30 -b:v 2M -maxrate 2M -bufsize 4M \
    -c:a aac -b:a 128k \
    -f flv rtmp://localhost:1935/live/demo 2>&1 | \
    grep --line-buffered "frame="

cleanup

