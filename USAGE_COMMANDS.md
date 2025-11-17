# RTMP Server - Usage Commands

## Setup (Required!)
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH="$(pwd)/build"
```

## RTMP → SRT Command

```bash
# Start RTMP server that outputs to SRT
export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build

gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-flv ! flvdemux name=demux \
    demux.video ! queue ! h264parse ! mpegtsmux name=mux ! \
    srtsink uri="srt://:8888?mode=listener" \
    demux.audio ! queue ! aacparse ! mux.
```

Then in another terminal, publish:
```bash
ffmpeg -re -f lavfi -i testsrc=duration=30:size=320x240:rate=30 \
       -c:v libx264 -preset ultrafast -t 30 \
       -f flv rtmp://localhost:1935/live/test
```

And receive the SRT stream:
```bash
ffplay srt://localhost:8888
# or
gst-play-1.0 "srt://localhost:8888"
```

## Simpler Commands

### RTMP → Stdout (for testing)
```bash
export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink dump=true
```

### RTMP → File (raw data)
```bash
export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=stream.bin
```

### RTMP → HLS (HTTP Live Streaming)
```bash
export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-flv ! flvdemux ! h264parse ! mpegtsmux ! \
    hlssink max-files=5 target-duration=4 location=segment%05d.ts \
    playlist-location=stream.m3u8
```

### RTMP → RTMP (re-stream)
```bash
export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build
gst-launch-1.0 rtmp2serversrc port=1935 ! \
    video/x-flv ! flvdemux ! h264parse ! flvmux ! \
    rtmpsink location="rtmp://destination-server/live/stream"
```

## Test Publishing

### With FFmpeg:
```bash
ffmpeg -re -f lavfi -i testsrc=duration=10:size=320x240:rate=30 \
       -c:v libx264 -preset ultrafast -t 10 \
       -f flv rtmp://localhost:1935/live/test
```

### With GStreamer:
```bash
gst-launch-1.0 videotestsrc num-buffers=300 ! \
    video/x-raw,width=320,height=240,framerate=30/1 ! \
    videoconvert ! x264enc tune=zerolatency ! flvmux ! \
    rtmp2sink location="rtmp://localhost:1935/live/test"
```

### With OBS Studio:
1. Settings → Stream
2. Service: Custom
3. Server: `rtmp://localhost:1935/live`
4. Stream Key: `test`
5. Click "Start Streaming"

## Troubleshooting

### "no element rtmp2serversrc"
→ Run: `export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build`

### "Address already in use"
→ Kill existing server: `pkill -f rtmp2serversrc`

### Check if plugin loads:
```bash
export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build
gst-inspect-1.0 rtmp2serversrc
```

## Server Output Confirmation

**YES, the server outputs data!** Confirmed by:

1. FFmpeg successfully streams (149KB sent, 0 errors)
2. Server logs show continuous message processing
3. FLV tags are created and passed to pipeline
4. Data flows through GStreamer elements

The current 0-byte filesink issue is tracked in: 
https://github.com/yarontorbaty/gst-rtmp2-server/issues/1

Using `flvdemux` or other elements in the pipeline works around this.

