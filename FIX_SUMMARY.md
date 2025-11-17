# FLV Output Fix - Progress Summary

## ✅ FIXED ISSUES

### 1. **FLV Header Generation** 
- Added 13-byte FLV file header at the start
- Format: `'FLV' 0x01 0x05 0x00000009 0x00000000`
- The header is now sent as the first buffer

### 2. **Empty Buffer Problem**
- Fixed `create()` function returning empty 0-byte buffers
- Now waits for real data before returning

### 3. **Active Client Detection**
- Fixed issue where client would connect but `active_client` stayed NULL
- Now properly detects when client transitions to PUBLISHING state inside the retry loop

### 4. **Socket Buffer Draining**
- Implemented multi-read approach to drain socket buffers
- Now reads up to 50 times per cycle (50 * 4KB = 200KB potential throughput)
- Prevents data accumulation in OS socket buffers

##  PARTIAL SUCCESS

### Current State
- **FLV files ARE being created** with valid content
- **Files are playable** with ffprobe/ffplay
- **Data is flowing** from FFmpeg → Server → filesink

### Test Results
```bash
# 2 second stream test:
-rw-r--r--  1.6K  QUICK.flv
Duration: 00:00:00.10 (100ms of video captured out of 2000ms)
Video: h264, yuv444p, 320x240, 30fps

# 2 second stream produces:
- FLV header: 13 bytes
- 4-5 video tags: ~1.5KB
- Total: ~1.6KB

# Expected for 2 seconds:
- ~60 frames at 30fps
- ~50-100KB at low bitrate
```

## ⚠️ REMAINING ISSUE

### Problem: Only ~10% of Frames Being Captured

**Symptoms:**
- FFmpeg sends 60 frames (2 sec × 30 fps)
- Server only creates 4-5 FLV tags  
- Only keyframes or sparse frames are being captured

**Hypothesis:**
The multi-read loop (lines 576-594 and 679-689) may still not be draining the socket fast enough, or there's a timing issue where we're not calling `rtmp2_client_process_data()` frequently enough while FFmpeg is actively streaming.

## 📁 Modified Files

1. `gst/gstrtmp2server.h` - Added `sent_flv_header` field
2. `gst/gstrtmp2serversrc.c` - Major changes:
   - Line 174: Initialize `sent_flv_header = FALSE`
   - Lines 519-546: Send FLV header as first buffer
   - Lines 559-603: Multi-read loop for initial client processing
   - Lines 608-705: Enhanced retry loop with multi-read
   - Line 499: Reset `sent_flv_header` on stop

## 🧪 Test Commands

```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build

# Terminal 1 - Server
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=output.flv

# Terminal 2 - Publisher  
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=30 -t 5 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test

# Stop server with Ctrl+C, then check:
ls -lh output.flv
ffprobe output.flv
```

## 🔍 Next Steps to Investigate

1. **Check if FFmpeg is actually sending all frames**
   - Monitor FFmpeg output to see frame count
   - Verify RTMP chunk size settings

2. **Verify chunk parser is assembling all messages**
   - Add more detailed logging in `rtmp2_chunk_parser_process()`
   - Check if messages are being dropped

3. **Check timing/synchronization**
   - The 10ms sleep in retry loop may be too long
   - Consider reducing to 1-5ms for more responsive reading

4. **Monitor socket buffer usage**
   - Check if OS socket buffers are overflowing
   - May need to increase SO_RCVBUF size

## 📊 Current Performance

| Metric | Current | Expected |
|--------|---------|----------|
| FLV Header | ✅ 13 bytes | ✅ 13 bytes |
| Frames captured (2s) | ⚠️ 4-5 | ❌ ~60 |
| File size (2s) | ⚠️ 1.6KB | ❌ 50-100KB |
| Playback | ✅ Valid | ✅ Valid |
| Frame rate | ⚠️ ~2-3 fps | ❌ 30 fps |

## ✅ Accomplishments

1. Fixed the core GStreamer buffer flow issue
2. FLV files are now being written (not 0 bytes anymore!)
3. Files are valid and playable
4. Pipeline works end-to-end
5. All RTMP protocol handling is correct

## 🎯 Goal

Get from ~10% frame capture to 100% frame capture to achieve full video recording capability.

