# GStreamer RTMP2 Server Source Plugin

A GStreamer plugin that provides `rtmp2serversrc` - an RTMP server source element that receives incoming RTMP streams from encoders like FFmpeg and OBS Studio.

## Status

This plugin is being contributed to GStreamer upstream. See the merge request:
https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10428

## Features

- Listens for incoming RTMP connections on a configurable port
- Supports H.264, H.265/HEVC video codecs
- Supports AAC audio codec
- Enhanced RTMP (E-RTMP) support for modern codecs
- Outputs raw FLV data via the `src` pad
- `loop` property for persistent server mode (keeps listening after client disconnects)

## Usage

### Basic Example - Save to File
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv
```

### Stream to SRT
```bash
gst-launch-1.0 rtmp2serversrc port=1935 ! flvdemux name=demux \
  demux.video ! queue ! h264parse config-interval=-1 ! mpegtsmux name=mux ! \
  srtsink uri="srt://:9000" wait-for-connection=false \
  demux.audio ! queue ! aacparse ! mux.
```

### Persistent Server Mode
```bash
gst-launch-1.0 rtmp2serversrc port=1935 loop=true ! filesink location=output.flv
```

### Send from FFmpeg
```bash
ffmpeg -re -i input.mp4 -c:v libx264 -c:a aac -f flv rtmp://localhost:1935/live/stream
```

## Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| host | string | "0.0.0.0" | Host address to bind to |
| port | uint | 1935 | Port to listen on |
| application | string | NULL | Expected application name (optional) |
| stream-key | string | NULL | Expected stream key (optional) |
| timeout | uint | 30 | Client timeout in seconds |
| loop | boolean | false | Keep listening after client disconnects |

## Building

This code is designed to be built as part of GStreamer's `gst-plugins-bad` module.

For the standalone version that works outside the GStreamer tree, see the `standalone-plugin` branch.

## License

LGPL-2.1-or-later

## Author

Yaron Torbaty <yarontorbaty@gmail.com>

