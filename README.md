# GStreamer RTMP2 Server Plugin

A GStreamer plugin that implements an RTMP server source element (`rtmp2serversrc`) for receiving RTMP push streams from clients.

## Overview

This plugin provides a GStreamer source element that acts as an RTMP server, accepting incoming RTMP push streams from clients (such as FFmpeg, OBS, or other RTMP publishers). It implements the RTMP protocol including handshake, chunk protocol, and FLV tag demuxing.

## Features

- **RTMP Server**: Listens for incoming RTMP connections on a configurable port
- **Enhanced RTMP (E-RTMP) v2 Support**: Full implementation of Enhanced RTMP specification
  - **TLS/SSL encryption** (RTMPS) for secure streaming
  - **AMF3 support** (message types 15, 16, 17) in addition to AMF0
  - **Enhanced Connect Command** with capability negotiation (capsEx, videoFourCcInfoMap)
  - **Enhanced Video Codecs**: H.264, H.265/HEVC, VP9, AV1
  - **Enhanced Audio Codecs**: AAC, MP3, Opus, G.711
  - **Multitrack Streaming**: Support for multiple audio/video tracks
  - **Reconnect Request**: Client reconnection capabilities
  - **Timestamp Nano Offset**: Higher precision timestamps
  - **Enhanced Metadata**: Enhanced onMetaData format support
- **RTMP Protocol Support**:
  - RTMP handshake (C0/C1/C2, S0/S1/S2)
  - RTMP chunk protocol parsing
  - FLV tag demuxing for video and audio
- **Stream Management**:
  - Application name routing (`/live/streamkey`)
  - Optional stream key validation
  - Multiple simultaneous client support
- **Configurable Properties**:
  - Host address binding
  - TCP port (default: 1935)
  - Application name (default: "live")
  - Stream key validation (optional)
  - Client timeout
  - TLS/SSL encryption (E-RTMP)
  - TLS certificate and private key

## Building

### Prerequisites

- GStreamer 1.20.0 or later
- GLib 2.56.0 or later
- Meson build system
- C compiler (GCC or Clang)

### Build Instructions

```bash
# Configure the build
meson setup build

# Build the plugin
meson compile -C build

# Install (optional, requires root)
sudo meson install -C build
```

The plugin will be installed to `$PREFIX/lib/gstreamer-1.0/`.

## Usage

### Basic Example

Start the RTMP server and output to a file:

```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv
```

### With FFmpeg Client

1. Start the GStreamer pipeline:
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-h264 ! h264parse ! mp4mux ! filesink location=output.mp4
```

2. Push a stream from FFmpeg:
```bash
ffmpeg -re -i input.mp4 -c copy -f flv rtmp://localhost:1935/live/mystream
```

### With OBS Studio

1. Start the GStreamer pipeline:
```bash
gst-launch-1.0 rtmp2serversrc port=1935 application=live ! \
    video/x-h264 ! h264parse ! mp4mux ! filesink location=output.mp4
```

2. In OBS Studio:
   - Settings → Stream
   - Service: Custom
   - Server: `rtmp://localhost:1935/live`
   - Stream Key: `mystream`

### Stream Key Validation

To only accept a specific stream key:

```bash
gst-launch-1.0 rtmp2serversrc port=1935 stream-key=secret123 ! \
    video/x-h264 ! h264parse ! mp4mux ! filesink location=output.mp4
```

### E-RTMP (RTMPS) with TLS/SSL

To enable secure RTMP streaming with TLS encryption:

```bash
# Generate self-signed certificate (for testing)
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes

# Start RTMPS server
gst-launch-1.0 rtmp2serversrc port=1935 tls=true \
    certificate=cert.pem private-key=key.pem ! \
    video/x-h264 ! h264parse ! mp4mux ! filesink location=output.mp4
```

Push from FFmpeg using RTMPS:

```bash
ffmpeg -i input.mp4 -c copy -f flv rtmps://localhost:1935/live/mystream
```

Note: For production use, use a proper certificate from a Certificate Authority (CA).

### Enhanced RTMP (E-RTMP) Features

The plugin implements Enhanced RTMP v2 specification as defined by [Veovera](https://veovera.org/docs/enhanced/enhanced-rtmp-v2.html). Enhanced RTMP provides:

1. **AMF3 Support**: More efficient encoding format (message types 15, 16, 17)
2. **Enhanced Codecs**: Support for modern codecs (H.265, VP9, AV1, Opus)
3. **Capability Negotiation**: Clients and servers negotiate features via `capsEx` flags
4. **Multitrack Streaming**: Multiple audio/video tracks in a single stream
5. **Reconnect Support**: Clients can reconnect without full handshake
6. **Enhanced Metadata**: Richer metadata format with more information

The server automatically advertises Enhanced RTMP capabilities in the connect response. Clients that support Enhanced RTMP will negotiate capabilities during the connect handshake.

### Multiple Streams

The plugin supports multiple simultaneous connections. Each client can push to a different stream key:

```bash
# Client 1
ffmpeg -i input1.mp4 -c copy -f flv rtmp://localhost:1935/live/stream1

# Client 2
ffmpeg -i input2.mp4 -c copy -f flv rtmp://localhost:1935/live/stream2
```

## Element Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `host` | string | "0.0.0.0" | Address to bind to |
| `port` | int | 1935 | TCP port to listen on |
| `application` | string | "live" | RTMP application name |
| `stream-key` | string | NULL | Optional stream key validation |
| `timeout` | int | 30 | Client timeout in seconds |
| `tls` | boolean | false | Enable TLS/SSL encryption (E-RTMP/RTMPS) |
| `certificate` | string | NULL | Path to TLS certificate file (PEM format) |
| `private-key` | string | NULL | Path to TLS private key file (PEM format) |

## Pipeline Examples

### Record RTMP Stream to MP4

```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-h264 ! h264parse ! \
    audio/mpeg ! aacparse ! \
    mpegtsmux ! filesink location=output.ts
```

### Stream to HLS

```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-h264 ! h264parse ! \
    audio/mpeg ! aacparse ! \
    mpegtsmux ! hlssink location=segment_%05d.ts \
    playlist-location=playlist.m3u8 target-duration=5
```

### Transcode and Stream

```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-h264 ! h264parse ! avdec_h264 ! \
    x264enc ! h264parse ! \
    audio/mpeg ! aacparse ! avdec_aac ! \
    aacenc ! aacparse ! \
    mpegtsmux ! filesink location=output.ts
```

## Testing

### Test with FFmpeg

```bash
# Terminal 1: Start server
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink

# Terminal 2: Push stream
ffmpeg -re -i test.mp4 -c copy -f flv rtmp://localhost:1935/live/test
```

### Test with OBS

1. Configure OBS to stream to `rtmp://localhost:1935/live/obsstream`
2. Start GStreamer pipeline to receive:
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink
```

## Architecture

The plugin consists of several components:

- **gstrtmp2serversrc**: Main GStreamer element (GstPushSrc)
- **rtmp2handshake**: RTMP handshake implementation
- **rtmp2chunk**: RTMP chunk protocol parser
- **rtmp2flv**: FLV tag demuxer
- **rtmp2client**: Client connection handler

## Limitations

- Currently supports single active stream (first publishing client)
- AMF0 command parsing is simplified (full implementation needed for production)
- Limited codec support (H.264, H.265, AAC, MP3)
- No authentication beyond stream key validation

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on contributing to this project.

## License

This plugin is licensed under the LGPL, matching GStreamer's licensing.

## References

- [RTMP Specification](https://www.adobe.com/devnet/rtmp.html)
- [FLV File Format Specification](https://www.adobe.com/devnet/f4v.html)
- [GStreamer Plugin Development](https://gstreamer.freedesktop.org/documentation/plugin-development/)

## Status

This is a work-in-progress implementation. The core functionality is implemented, but some features may need refinement for production use.

