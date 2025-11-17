# Status Update - Frame Capture Optimization

## Branch: optimize-frame-capture  
## Issue: #2

## Current Implementation

### ✅ Completed
1. **Buffer Optimizations**
   - 16KB read buffer (was 4KB)
   - 256KB SO_RCVBUF socket option
   - Result: 12/60 frames (20%) vs original 5%

2. **Async GSource Infrastructure**
   - `rtmp2_client_read_cb()`: Async callback function
   - `rtmp2_client_start_reading()`: GSource setup
   - Thread-safe with mutex protection

3. **Event Loop Thread** (NEW!)
   - Dedicated thread running `g_main_context_iteration()` continuously
   - Similar to nginx-rtmp and gst-rtsp-server architecture
   - Thread lifecycle properly managed (start/stop/join)

## Current Status: Event Loop Thread Working But GSource Not Retriggering

### Evidence from Latest Test

**Thread Analysis:**
- `0x6000003b1560`: Main create() thread
- `0x6000003ae340`: **Event loop thread** (async callbacks)

**Timeline:**
- **0.347s**: Event loop thread starts ✅
- **4.136s**: Client connects
- **4.179-4.234s**: Handshake (C0, C1, S0/S1/S2, C2) ✅
- **4.234s**: Async callback fires, processes:
  - connect command ✅
  - _checkbw command ✅
  - Hits WOULD_BLOCK, returns G_SOURCE_CONTINUE
- **4.235s - 47.904s**: **43-SECOND GAP** ❌
- **47.904s**: Async callback fires again, sees EOF (FFmpeg timeout)

### The Problem

The async callback fires ONCE after handshake, processes all available data (connect + _checkbw), returns `G_SOURCE_CONTINUE`, then **never fires again** for 43 seconds until FFmpeg gives up.

**Expected behavior**: GSource should fire again when FFmpeg sends releaseStream, FCPublish, createStream commands.

**Actual behavior**: GSource doesn't fire until connection closes.

### Possible Root Causes

1. **FFmpeg Not Sending More Data**
   - Maybe waiting for a response we didn't send correctly
   - Maybe expecting a specific message format
   - Ping/pong keep-alive not working

2. **GSource Edge-Triggered vs Level-Triggered**
   - GSource might be edge-triggered (fires once per data arrival)
   - If all data arrives in one burst, only fires once
   - Need to verify socket monitoring mode

3. **Mutex Deadlock**
   - Async callback holds mutex while reading
   - Event thread can't process new events while mutex held?
   - But we return from callback, so mutex should be released

4. **G_IO_IN Condition Not Re-arming**
   - After reading until WOULD_BLOCK, socket might not signal again
   - Need to verify GSource stays active

## Test Commands to Diagnose

```bash
# Check what messages we're processing
grep "type=" event_thread.log | head -20

# Check for control messages (ping/pong)
grep "type=0\|type=1\|type=2" event_thread.log

# See all async thread activity
grep "0x6000003ae340" event_thread.log | tail -30
```

## Next Steps

### Option A: Verify FFmpeg Behavior
Use Wireshark/tcpdump to see if FFmpeg actually sends releaseStream/FCPublish/createStream or if it's waiting.

### Option B: Force GSource Retriggering
After processing data, explicitly re-arm the GSource or use a different monitoring strategy.

### Option C: Continuous Polling in Async Callback
Instead of returning after WOULD_BLOCK, keep the callback running and poll periodically.

### Option D: Check RTMP Protocol Sequence
Verify we're sending responses in the exact format FFmpeg expects for each command.

## Files Modified

- `gst/gstrtmp2server.h`: Added event_thread and running fields
- `gst/gstrtmp2serversrc.c`: Implemented event loop thread
- `gst/rtmp2client.c`: Async reading callbacks
- `gst/rtmp2client.h`: Function declarations

## Comparison

| Approach | Frames | Rate | Notes |
|----------|--------|------|-------|
| Original | 3-5 | 5% | 4KB buffer, 10ms sleep |
| Buffer opts | 12 | 20% | 16KB buffer, faster polling |
| Async + Event Thread | 0 | 0% | GSource fires once then stops |

The async infrastructure is correct but something prevents the GSource from retriggering when new data arrives.

