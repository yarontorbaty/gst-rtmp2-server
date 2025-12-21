# RTMP Server - FULLY WORKING! 🎉

## Status: ✅ Audio + Video working correctly

The RTMP server now correctly:
1. ✅ Receives RTMP stream from FFmpeg (with `-re` for real-time)
2. ✅ Parses video (H.264) and audio (AAC) data
3. ✅ Handles interleaved audio/video chunk streams
4. ✅ Outputs valid FLV format with proper headers
5. ✅ 90 video frames + 131 audio frames captured correctly

## Working Test Commands

### Server:
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
GST_PLUGIN_PATH=./build gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=/tmp/out.flv
```

### FFmpeg (MUST use `-re` for real-time streaming):
```bash
ffmpeg -re -y -f lavfi -i testsrc=duration=3:rate=30 -f lavfi -i sine=duration=3:frequency=440 \
  -c:v libx264 -preset ultrafast -c:a aac -b:a 128k \
  -f flv rtmp://localhost:1935/live/test
```

### Verify:
```bash
ffprobe -v error -count_frames -show_entries stream=codec_type,codec_name,nb_read_frames -of csv=p=0 /tmp/out.flv
# Output: h264,video,90
#         aac,audio,131
```

## Key Fixes Made

1. **FLV File Header**: Added proper FLV signature with audio+video flags (0x05)
2. **FLV Tag Structure**: Preserved full RTMP message body including codec info byte
3. **Type 3 Chunk Handling**: Fixed chunk parser to handle Type 3 continuations for new messages that reuse previous header metadata
4. **srcpad**: Added always-present source pad for raw FLV output
5. **EOS Grace Period**: Wait 100ms before sending EOS to ensure all data is processed
6. **Race Condition Fix**: Fixed pending_tags access without holding lock

## Important Notes

- **MUST use `-re` flag** with FFmpeg to stream at real-time rate
- Without `-re`, FFmpeg blasts data too fast and the server can't keep up
- The `rtmp2serversrc` outputs raw FLV on its main `src` pad

## Build

```bash
./quick_rebuild.sh
```
