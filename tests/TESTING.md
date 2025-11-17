# Testing Guide for GStreamer RTMP2 Server Plugin

This guide explains how to test the `rtmp2serversrc` plugin with various clients and scenarios.

## Prerequisites

1. Build the plugin (see main README.md)
2. Set the plugin path:
   ```bash
   export GST_PLUGIN_PATH="$(pwd)/build"
   ```

## Basic Testing

### 1. Start the Server

```bash
# Basic server that accepts connections
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink

# Server that saves to file
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv

# Server with custom application
gst-launch-1.0 rtmp2serversrc port=1935 application=live ! filesink location=output.flv
```

### 2. Test with FFmpeg

In one terminal, start the server:
```bash
export GST_PLUGIN_PATH="$(pwd)/build"
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=received.flv
```

In another terminal, push a stream:
```bash
# Push from a file
ffmpeg -re -i input.mp4 -c copy -f flv rtmp://localhost:1935/live/mystream

# Push test pattern
ffmpeg -f lavfi -i testsrc=duration=10:size=640x480:rate=30 \
       -f lavfi -i sine=frequency=1000:duration=10 \
       -c:v libx264 -preset ultrafast -tune zerolatency \
       -c:a aac -f flv rtmp://localhost:1935/live/teststream
```

### 3. Test with OBS Studio

1. Start the GStreamer server:
   ```bash
   gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=obs_output.flv
   ```

2. In OBS Studio:
   - Settings → Stream
   - Service: Custom
   - Server: `rtmp://localhost:1935/live`
   - Stream Key: `obsstream`
   - Click "Start Streaming"

### 4. Test Enhanced RTMP Features

The server automatically advertises Enhanced RTMP capabilities. To verify:

```bash
# Start server with debug output
export GST_DEBUG=rtmp2serversrc:5
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink
```

Look for Enhanced RTMP capability negotiation in the logs when a client connects.

### 5. Test RTMPS (TLS/SSL)

First, generate a self-signed certificate:
```bash
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
    -days 365 -nodes -subj "/CN=localhost"
```

Start RTMPS server:
```bash
gst-launch-1.0 rtmp2serversrc port=1935 tls=true \
    certificate=cert.pem private-key=key.pem \
    ! filesink location=secure_output.flv
```

Push with FFmpeg (may need to disable TLS verification for self-signed certs):
```bash
ffmpeg -i input.mp4 -c copy -f flv \
    rtmps://localhost:1935/live/securestream
```

### 6. Test Stream Key Validation

```bash
# Server with stream key
gst-launch-1.0 rtmp2serversrc port=1935 stream-key=secret123 \
    ! filesink location=validated.flv

# Push with matching key (in stream path)
ffmpeg -i input.mp4 -c copy -f flv \
    rtmp://localhost:1935/live/secret123
```

### 7. Test Multiple Concurrent Streams

Start server:
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink
```

Push multiple streams:
```bash
# Terminal 1
ffmpeg -i input1.mp4 -c copy -f flv rtmp://localhost:1935/live/stream1

# Terminal 2
ffmpeg -i input2.mp4 -c copy -f flv rtmp://localhost:1935/live/stream2
```

### 8. Test with GStreamer Pipeline

Receive and process the stream:
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-h264 ! h264parse ! \
    avdec_h264 ! autovideosink
```

Or save to MP4:
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-h264 ! h264parse ! \
    audio/mpeg ! aacparse ! \
    mpegtsmux ! filesink location=output.ts
```

## Debugging

### Enable Debug Output

```bash
export GST_DEBUG=rtmp2serversrc:5
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink
```

Debug levels:
- `1` = ERROR
- `2` = WARNING
- `3` = FIXME
- `4` = INFO
- `5` = DEBUG
- `6` = LOG
- `7` = TRACE

### Check Plugin Registration

```bash
export GST_PLUGIN_PATH="$(pwd)/build"
gst-inspect-1.0 rtmp2serversrc
```

### Verify Properties

```bash
gst-inspect-1.0 rtmp2serversrc | grep -A 3 "Property"
```

## Automated Test Scripts

Run the provided test scripts:

```bash
# Basic functionality test
./tests/test-gstreamer.sh

# Test with FFmpeg client
./tests/test-with-ffmpeg.sh

# Test with GStreamer client
./tests/test-with-gstreamer-client.sh

# Test RTMPS
./tests/test-rtmps.sh
```

## Common Issues

### Plugin Not Found

Make sure `GST_PLUGIN_PATH` is set:
```bash
export GST_PLUGIN_PATH="$(pwd)/build"
```

### Port Already in Use

Change the port:
```bash
gst-launch-1.0 rtmp2serversrc port=1936 ! fakesink
```

### Connection Refused

- Check firewall settings
- Verify server is running
- Check port number matches

### No Data Received

- Verify client is pushing to correct URL format: `rtmp://host:port/application/streamkey`
- Check application name matches (default is "live")
- Verify stream key if validation is enabled

## Performance Testing

Test with high bitrate streams:
```bash
# Server
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink

# High bitrate client
ffmpeg -re -i input.mp4 -c:v libx264 -b:v 5M -c:a aac -b:a 192k \
    -f flv rtmp://localhost:1935/live/highbitrate
```

## Integration with Other GStreamer Elements

Example pipelines:

### Transcode and Stream
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-h264 ! h264parse ! avdec_h264 ! \
    x264enc ! h264parse ! \
    audio/mpeg ! aacparse ! avdec_aac ! \
    aacenc ! aacparse ! \
    mpegtsmux ! filesink location=transcoded.ts
```

### Stream to HLS
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-h264 ! h264parse ! \
    audio/mpeg ! aacparse ! \
    mpegtsmux ! hlssink location=segment_%05d.ts \
    playlist-location=playlist.m3u8 target-duration=5
```

### Record and Monitor
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! tee name=t ! \
    queue ! filesink location=record.flv \
    t. ! queue ! fakesink
```

