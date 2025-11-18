#!/bin/bash
# End-to-End RTMP to SRT Streaming Demo
# Demonstrates 100% frame capture with V2 parser + low-latency SRT

set -e

cd "$(dirname "$0")"
export GST_PLUGIN_PATH=$(pwd)/build

echo "=================================================="
echo "  RTMP→SRT Streaming Demo (V2 Parser - 100%)"
echo "=================================================="
echo ""
echo "This will open a video player window showing live"
echo "RTMP stream with SRT output for low latency."
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
    pkill -f "ffplay.*srt" 2>/dev/null || true
    pkill -f "mpv.*srt" 2>/dev/null || true
    pkill -f "ffmpeg.*testsrc" 2>/dev/null || true
    sleep 1
    echo "Cleanup complete"
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT

# Start GStreamer RTMP→SRT bridge (listener mode)
echo "▶️  Starting RTMP server (port 1935) → SRT output (port 8888)..."
gst-launch-1.0 -q rtmp2serversrc port=1935 ! \
    flvdemux ! h264parse ! mpegtsmux ! \
    srtsink uri="srt://:8888?mode=listener&latency=200" 2>&1 | \
    grep -E "Setting|ERROR" &
GST_PID=$!
sleep 3

if ! ps -p $GST_PID >/dev/null 2>&1; then
    echo "❌ Failed to start GStreamer"
    exit 1
fi
echo "✅ RTMP→SRT bridge ready (PID: $GST_PID)"
echo ""

# Wait for RTMP server to be ready
echo "⏳ Waiting for RTMP server to initialize..."
for i in {1..20}; do
    if lsof -i :1935 2>/dev/null | grep -q LISTEN; then
        break
    fi
    sleep 0.5
done

if ! lsof -i :1935 2>/dev/null | grep -q LISTEN; then
    echo "❌ RTMP server failed to start"
    exit 1
fi
echo "✅ RTMP server listening on port 1935"
echo ""

# Check for video player
HAS_MPV=false
HAS_FFPLAY=false
command -v mpv >/dev/null 2>&1 && HAS_MPV=true
command -v ffplay >/dev/null 2>&1 && HAS_FFPLAY=true

if ! $HAS_MPV && ! $HAS_FFPLAY; then
    echo "❌ No video player found (need mpv or ffplay)"
    exit 1
fi

# Start video player (SRT client)
echo "▶️  Starting SRT video player..."
if $HAS_MPV; then
    echo "Using mpv player..."
    mpv --no-cache --untimed --no-demuxer-thread \
        --video-sync=audio --title="RTMP→SRT Demo (100% Capture)" \
        "srt://localhost:8888?mode=caller&latency=200" >/dev/null 2>&1 &
    PLAYER_PID=$!
    PLAYER_NAME="mpv"
else
    echo "Using ffplay player..."
    ffplay -fflags nobuffer -flags low_delay -probesize 32 \
        -window_title "RTMP→SRT Demo" \
        "srt://localhost:8888?mode=caller&latency=200" >/dev/null 2>&1 &
    PLAYER_PID=$!
    PLAYER_NAME="ffplay"
fi

sleep 2

if ! ps -p $PLAYER_PID >/dev/null 2>&1; then
    echo "❌ Failed to start video player"
    exit 1
fi
echo "✅ Video player ready ($PLAYER_NAME, PID: $PLAYER_PID)"
echo ""

# Start FFmpeg RTMP publisher
echo "▶️  Starting RTMP stream (test pattern, 30fps)..."
echo ""
echo "=================================================="
echo "  🎥 VIDEO SHOULD NOW BE PLAYING IN $PLAYER_NAME!"
echo "=================================================="
echo ""
echo "You should see a test pattern with:"
echo "  • Colored scrolling bars"
echo "  • Timestamp counter"  
echo "  • Smooth 30 FPS playback"
echo ""
echo "Streaming stats will appear below."
echo "Press Ctrl+C to stop."
echo ""
echo "=================================================="
echo ""

ffmpeg -re -f lavfi -i testsrc=size=1280x720:rate=30 \
    -c:v libx264 -preset ultrafast -tune zerolatency \
    -g 30 -b:v 2M -maxrate 2M -bufsize 4M \
    -f flv rtmp://localhost:1935/live/demo 2>&1 | \
    grep --line-buffered "frame=" || true

echo ""
echo "Stream ended naturally"
cleanup

