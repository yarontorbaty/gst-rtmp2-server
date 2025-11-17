#!/bin/bash
# Test script for rtmp2serversrc with GStreamer

set -e

PORT=1935
TEST_STREAM_KEY="test123"
OUTPUT_FILE="test_output.flv"

echo "=== GStreamer RTMP Server Test ==="
echo ""

# Set plugin path
export GST_PLUGIN_PATH="$(cd "$(dirname "$0")/.." && pwd)/build"
export GST_DEBUG=rtmp2serversrc:5

echo "Plugin path: $GST_PLUGIN_PATH"
echo ""

# Test 1: Basic server startup
echo "Test 1: Starting RTMP server..."
echo "Command: gst-launch-1.0 rtmp2serversrc port=$PORT ! fakesink"
echo ""

timeout 5 gst-launch-1.0 rtmp2serversrc port=$PORT ! fakesink 2>&1 &
SERVER_PID=$!
sleep 2

if ps -p $SERVER_PID > /dev/null; then
    echo "✓ Server started successfully (PID: $SERVER_PID)"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
else
    echo "✗ Server failed to start"
    exit 1
fi

echo ""

# Test 2: Server with file output
echo "Test 2: Server with file output..."
echo "Command: gst-launch-1.0 rtmp2serversrc port=$PORT ! filesink location=$OUTPUT_FILE"
echo "Starting server in background..."

gst-launch-1.0 rtmp2serversrc port=$PORT ! filesink location=$OUTPUT_FILE 2>&1 &
SERVER_PID=$!
sleep 2

if ps -p $SERVER_PID > /dev/null; then
    echo "✓ Server running (PID: $SERVER_PID)"
    echo "You can now push a stream to rtmp://localhost:$PORT/live/$TEST_STREAM_KEY"
    echo "Press Ctrl+C to stop..."
    wait $SERVER_PID 2>/dev/null || true
else
    echo "✗ Server failed to start"
    exit 1
fi

# Cleanup
rm -f "$OUTPUT_FILE"
echo ""
echo "=== Tests completed ==="

