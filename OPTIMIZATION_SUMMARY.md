# Frame Capture Optimization Summary

## Branch: optimize-frame-capture
## Issue: #2 - https://github.com/yarontorbaty/gst-rtmp2-server/issues/2

## Changes Made

### 1. Increased Socket Read Buffer (rtmp2client.c)
- Changed from 4KB to 16KB buffer
- Allows reading more data per iteration
- **Lines 463**: `guint8 buffer[16384]`

### 2. Increased Socket Receive Buffer (gstrtmp2serversrc.c)
- Added SO_RCVBUF socket option to 256KB
- Prevents kernel-level packet drops
- **Lines 334-343**: Socket buffer configuration

### 3. Optimized Read Loop
- Increased max_reads from 50 to 100 iterations
- Doubled the burst capacity from 200KB to 1.6MB
- **Lines 576, 696**: max_reads = 100

### 4. Adaptive Sleep Timing
- Reduced sleep to 100μs (0.1ms) during active streaming
- Was 10ms, now 100× faster polling
- **Lines 677-686**: Conditional sleep based on publishing state

### 5. Added sys/socket.h Include
- Required for SO_RCVBUF constant
- **Line 30**: `#include <sys/socket.h>`

## Test Results

### Before Optimizations
- 4-5 frames captured out of ~60 expected (~8% capture rate)
- File size: 1.6 KB
- Duration: 0.1 seconds

### After Optimizations
- 3 frames captured out of ~60 expected (~5% capture rate)
- File size: 0 bytes (not flushed due to test interruption)
- Duration: N/A

## Root Cause Analysis

The optimizations improved buffer capacity and read frequency, but the core architectural issue remains:

**The problem is NOT buffer size or read speed - it's the GStreamer pull-model architecture.**

### How It Works
1. GStreamer calls `create()` to request a buffer
2. Server reads from socket and looks for FLV tags  
3. When a tag is found, `create()` returns it to GStreamer
4. **GStreamer processes the buffer** (caps negotiation, etc.)
5. GStreamer calls `create()` again
6. GOTO step 2

### The Issue
During step 4 (GStreamer processing), the server is NOT reading from the socket. FFmpeg continues sending frames at 30fps (one every 33ms), but:
- The server only reads when `create()` is called
- Between calls, there can be 100ms+ delays
- During delays, FFmpeg's frames fill the socket buffer
- Once full, new frames are dropped

### Evidence
- Timestamps show 1.9 second gap between "publish" and first frame
- Only 3-4 frames received despite FFmpeg sending 60
- FFmpeg completes successfully (sent all data)
- Server logs show sparse tag reception

## Potential Solutions (Not Implemented)

### Solution 1: Separate Read Thread
Create a dedicated thread that continuously reads from socket and queues FLV tags, independent of GStreamer's `create()` calls.

**Pros**: Eliminates read gaps, maximizes capture rate
**Cons**: Major architectural change, thread synchronization complexity

### Solution 2: GStreamer Push Mode
Convert element to push-mode instead of pull-mode, where the element proactively pushes buffers downstream.

**Pros**: Natural fit for live streaming
**Cons**: Complete redesign of element

### Solution 3: Larger Buffers + Burst Reading
Increase socket buffers to several MB and read in massive bursts.

**Pros**: Simple, might help with short streams
**Cons**: Won't scale to longer streams or higher bitrates

## Recommendation

The current optimizations are valuable (larger buffers, faster polling) but won't solve the fundamental issue. To achieve >90% frame capture, the element needs architectural changes:

1. **Short-term**: Implement a read thread that continuously drains the socket
2. **Long-term**: Redesign as a push-mode element

## Files Modified

- `gst/gstrtmp2serversrc.c` - Main server source element
- `gst/rtmp2client.c` - Client socket handling

## Testing

Run test with:
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
export GST_PLUGIN_PATH=$(pwd)/build
./test_final.sh
```

Expected for 2-second 30fps stream:
- **Target**: ~60 frames, 50-100KB file
- **Current**: ~3 frames, minimal data

