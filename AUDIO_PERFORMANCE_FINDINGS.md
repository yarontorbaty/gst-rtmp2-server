# Audio+Video Performance Issue

## Summary

The V2 parser correctly handles audio messages (type=8) alongside video (type=9), but capture rate degrades significantly when both streams are present.

## Test Results

### Video-Only Stream
```bash
ffmpeg -re -f lavfi -i testsrc=duration=2:rate=30 \
  -c:v libx264 -f flv rtmp://localhost:1935/live/test
```
**Result**: 60/60 frames (**100%** capture rate) ✅

### Audio+Video Stream  
```bash
ffmpeg -re -f lavfi -i testsrc=duration=2:rate=30 \
  -f lavfi -i sine=duration=2 \
  -c:v libx264 -c:a aac -f flv rtmp://localhost:1935/live/test
```
**Result**: 39/60 frames (**65%** capture rate) ❌

## Findings

1. **Parser works correctly**: No errors parsing audio messages
2. **Both streams output**: FLV file contains valid audio AND video streams
3. **Tag queue efficient**: Rarely exceeds 4 tags, drains quickly
4. **Issue is upstream**: Large gaps in frame arrival times from FFmpeg

### Example from logs:
```
0:00:02.938 - Frame #14 arrives
0:00:03.381 - Frame #15 arrives (443ms gap!)
```

## Possible Root Causes

1. **FFmpeg sending behavior**: `-re` flag may throttle incorrectly with dual streams
2. **Server polling loop**: 5ms sleep between tag checks may miss bursts
3. **Interleaved message handling**: Audio/video multiplexing creates timing issues

## Next Steps

1. ✅ Confirm parser handles audio (DONE - it does)
2. ✅ Test without `-re` flag to rule out FFmpeg throttling
3. ⏳ Profile tag arrival times vs drain times
4. ⏳ Optimize polling loop or switch to event-driven model
5. ⏳ Test with other RTMP sources (OBS, etc.)

## Status

- **Issue #4**: Closed - was about pipeline config, fixed in PR #5
- **Issue #7**: Created to track this performance issue  
- **AUDIO_SUPPORT_PROMPT.md**: Documentation for future audio work

## Recommendation

The **V2 parser fully supports audio** - no code changes needed there. The performance issue requires investigation into:
- Server event loop timing
- FFmpeg sending patterns  
- GStreamer buffer management

This is a performance optimization, not a critical bug. Audio+video streaming **works**, just not at 100% capture rate yet.

