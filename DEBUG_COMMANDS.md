# Debug Commands

## Enable GStreamer Debug Output

```bash
# Set debug level (0=none, 1=error, 2=warning, 3=fixme, 4=info, 5=debug, 6=log)
export GST_DEBUG=3  # Shows errors and warnings

# Or for specific components:
export GST_DEBUG=rtmp2serversrc:5,rtmp2client:5,flvdemux:5

# Or maximum verbosity:
export GST_DEBUG=6  # Very verbose!
```

## Debug RTMP→SRT Pipeline

```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH="$(pwd)/build"

# Kill anything on the ports
lsof -ti:1935 | xargs kill -9 2>/dev/null
lsof -ti:8888 | xargs kill -9 2>/dev/null
sleep 2

# Start with debug output
export GST_DEBUG=rtmp2serversrc:4,rtmp2client:4,flvdemux:4,srtsink:4

gst-launch-1.0 -v rtmp2serversrc port=1935 ! \
    video/x-flv ! flvdemux name=demux \
    demux.video ! queue ! h264parse ! mpegtsmux name=mux ! \
    srtsink uri="srt://:8888?mode=listener" \
    demux.audio ! queue ! aacparse ! mux.
```

## Why FFmpeg Stops After a Few Seconds

FFmpeg is stopping because your test has a **duration limit**:
```bash
testsrc=duration=10  # ← Stops after 10 seconds
-t 10                # ← Also limits to 10 seconds
```

This is NORMAL and EXPECTED! FFmpeg finishes, sends "UnPublishing", and exits cleanly.

## For Continuous Streaming

Remove the duration limits:
```bash
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=30 \
       -c:v libx264 -preset ultrafast \
       -f flv rtmp://localhost:1935/live/test
# Runs until you press Ctrl+C
```

## Check if SRT is Working

```bash
# In terminal 1: Start RTMP→SRT server
export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink dump=true

# In terminal 2: Publish RTMP
ffmpeg -re -f lavfi -i testsrc -c:v libx264 -f flv rtmp://localhost:1935/live/test

# You should see:
# - "Client started publishing" in server output
# - Continuous "Processing X RTMP messages" 
# - FFmpeg showing frames encoding
```

## Simple Working Test

```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH="$(pwd)/build"

# Kill existing
lsof -ti:1935 | xargs kill -9 2>/dev/null; sleep 2

# Start server
gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink &
sleep 3

# Test publish
ffmpeg -re -f lavfi -i testsrc=duration=5:size=160x120:rate=10 \
       -c:v libx264 -t 5 -f flv rtmp://localhost:1935/live/test

# Should see: "frame= 50" then "Exiting with exit code 0"
```

## Troubleshooting

### "Address already in use"
```bash
# Find and kill process
lsof -ti:1935 | xargs kill -9
# or
pkill -9 -f rtmp2serversrc
```

### "no element rtmp2serversrc"  
```bash
export GST_PLUGIN_PATH=/Users/yarontorbaty/gst-rtmp2-server/build
```

### Check what's on port 1935
```bash
lsof -i:1935
```

### View full server logs
```bash
GST_DEBUG=4 gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink 2>&1 | tee server.log
```

