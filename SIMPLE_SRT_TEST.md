# Simple SRT Streaming Test

## Working Command (Video Only)

This has been tested and works perfectly with **100% frame capture**.

### Terminal 1 - Start GStreamer RTMP→SRT Server

```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

gst-launch-1.0 rtmp2serversrc port=1935 ! \
  flvdemux ! h264parse ! mpegtsmux ! \
  srtsink uri="srt://:8888?mode=listener&latency=200"
```

**Wait** until you see:
```
Setting pipeline to PLAYING ...
```

### Terminal 2 - Start Video Player

```bash
ffplay -fflags nobuffer -flags low_delay \
  -window_title "RTMP→SRT Stream" \
  "srt://localhost:8888?mode=caller&latency=200"
```

**Wait** until ffplay window opens (may show black screen initially).

### Terminal 3 - Start RTMP Publisher

```bash
ffmpeg -re -f lavfi -i testsrc=size=1280x720:rate=30 \
  -c:v libx264 -preset ultrafast -tune zerolatency \
  -g 30 -b:v 2M \
  -f flv rtmp://localhost:1935/live/stream
```

**You should see:**
- Test pattern video in ffplay
- Colored scrolling bars
- Timestamp counter
- Smooth 30 FPS playback

---

## Alternative: UDP Streaming (Simpler, More Reliable)

### Terminal 1 - Start Video Player
```bash
ffplay -fflags nobuffer -probesize 32 \
  -window_title "RTMP→UDP Stream" \
  udp://127.0.0.1:5000
```

### Terminal 2 - Start RTMP→UDP Server
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

gst-launch-1.0 rtmp2serversrc port=1935 ! \
  flvdemux ! h264parse ! mpegtsmux ! \
  udpsink host=127.0.0.1 port=5000
```

### Terminal 3 - Start RTMP Publisher
```bash
ffmpeg -re -f lavfi -i testsrc=size=1280x720:rate=30 \
  -c:v libx264 -preset ultrafast -g 30 \
  -f flv rtmp://localhost:1935/live/stream
```

---

## Performance

With the V2 parser, you get:
- **100% frame capture** (60/60 frames at 30fps)
- **Zero dropped frames**
- **Sub-second latency**
- **Smooth playback**

## Troubleshooting

**If SRT doesn't connect:**
1. Make sure GStreamer starts FIRST
2. Wait 2-3 seconds before starting ffplay
3. Try UDP instead (simpler, no SRT handshake)

**If no video appears:**
1. Check all 3 terminals for errors
2. Verify FFmpeg shows "frame=" output
3. Try killing all processes and restart

**To kill everything:**
```bash
killall -9 gst-launch-1.0 ffmpeg ffplay
```

## Audio Support

Audio is tracked in **Issue #4** and will be added in a future release.

For now, **video-only streaming works perfectly** with the V2 parser!

