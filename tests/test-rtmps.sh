#!/bin/bash
# Test RTMPS (TLS/SSL) with self-signed certificate

set -e

PORT=1935
STREAM_KEY="securetest"
CERT_FILE="test_cert.pem"
KEY_FILE="test_key.pem"
OUTPUT_FILE="rtmps_received.flv"

export GST_PLUGIN_PATH="$(cd "$(dirname "$0")/.." && pwd)/build"
export GST_DEBUG=rtmp2serversrc:4

echo "=== Testing RTMPS (TLS/SSL) ==="
echo ""

# Generate self-signed certificate if it doesn't exist
if [ ! -f "$CERT_FILE" ] || [ ! -f "$KEY_FILE" ]; then
    echo "Generating self-signed certificate for testing..."
    openssl req -x509 -newkey rsa:2048 -keyout "$KEY_FILE" -out "$CERT_FILE" \
        -days 365 -nodes -subj "/CN=localhost" 2>/dev/null
    echo "✓ Certificate generated"
    echo ""
fi

echo "Starting RTMPS server..."
echo "Command: gst-launch-1.0 rtmp2serversrc port=$PORT tls=true certificate=$CERT_FILE private-key=$KEY_FILE ! filesink location=$OUTPUT_FILE"
echo ""

gst-launch-1.0 rtmp2serversrc port=$PORT tls=true \
    certificate="$CERT_FILE" private-key="$KEY_FILE" \
    ! filesink location=$OUTPUT_FILE 2>&1 &
SERVER_PID=$!

sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo "✗ Server failed to start"
    exit 1
fi

echo "✓ RTMPS server running (PID: $SERVER_PID)"
echo ""
echo "You can now push a stream to rtmps://localhost:$PORT/live/$STREAM_KEY"
echo "Note: FFmpeg may require --tls-verify=false for self-signed certificates"
echo ""
echo "Example FFmpeg command:"
echo "  ffmpeg -i input.mp4 -c copy -f flv rtmps://localhost:$PORT/live/$STREAM_KEY"
echo ""
echo "Press Ctrl+C to stop the server..."

# Wait for user interrupt or timeout
trap "kill $SERVER_PID 2>/dev/null; exit" INT TERM
wait $SERVER_PID 2>/dev/null || true

# Cleanup
rm -f "$OUTPUT_FILE"
echo ""
echo "=== Test completed ==="

