# FLV Serialization Issue - Wrong Codec Byte

## Problem
FFmpeg reports "Video codec (0)" when reading the FLV, meaning the codec byte is `0x00` instead of `0x17` (H.264 keyframe).

## Root Cause Investigation Needed

The serialization code reconstructs the codec byte as:
```c
guint8 frame_type = tag->video_keyframe ? 1 : 2;  
guint8 codec_id = tag->video_codec & 0x0F;
*ptr++ = (frame_type << 4) | codec_id;
```

But this appears to produce `0x00` or `0x20` instead of `0x17`.

### Hypothesis 1: `tag->video_codec` is not populated
The FLV parser in `rtmp2flv.c` extracts the codec from the incoming RTMP data, but maybe it's not being set correctly.

### Hypothesis 2: RTMP messages don't include codec info
Maybe the RTMP chunk parser isn't passing codec metadata through to the FLV tag creator.

## Quick Fix: Copy Original Data

Instead of reconstructing, we should **preserve the original RTMP message payload** which already has the codec byte.

The issue is that `rtmp2flv.c` **strips** the codec byte when parsing, storing only the payload in `tag->data`. We need to either:

1. **Store the full data including codec byte** in `tag->data` (change rtmp2flv.c)
2. **Store codec byte separately** as `tag->codec_byte` (add field to Rtmp2FlvTag)
3. **Don't use FLV at all** - emit raw H.264/AAC on separate pads (original SRT_LIVE_STREAMING_TASK.md approach)

## Recommended Path Forward

**Option 3** is cleanest. The FLV intermediate layer adds complexity with no benefit for live streaming. Instead:

- Have `rtmp2serversrc` emit **video/x-h264** and **audio/mpeg** directly on sometimes pads
- Parse RTMP message types 8/9 and extract codec data
- Push buffers with proper caps
- Pipeline becomes: `rtmp2serversrc.video_0 ! h264parse ! ...`

This matches how other live sources (rtspsrc, udpsrc) work.

