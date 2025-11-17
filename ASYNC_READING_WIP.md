# Async Reading Implementation - Work in Progress

## Goal
Implement continuous asynchronous reading from client sockets to eliminate frame drops caused by pull-mode gaps.

## Implementation Approach

### Architecture Change
Instead of only reading when GStreamer calls `create()`, add `GSource` monitoring to each client socket that continuously reads data as it arrives.

### Components Added

1. **`rtmp2_client_read_cb()` (rtmp2client.c:380-422)**
   - Callback triggered when data is available on client socket
   - Monitors `G_IO_IN | G_IO_ERR | G_IO_HUP` conditions
   - Drains up to 100 chunks per callback invocation
   - Automatically handles errors and disconnections

2. **`rtmp2_client_start_reading()` (rtmp2client.c:424-454)**
   - Sets up the `GSource` for a client socket
   - Attaches it to the GMainContext for event-driven reading
   - Called after handshake completes

3. **Handshake-triggered Async Start (rtmp2client.c:663-675)**
   - When C2 (final handshake step) completes
   - Automatically starts async reading
   - Ensures handshake is fully complete before continuous reading begins

### Flow

```
Client Connects
    ↓
Manual Handshake Processing (in create())
    ↓
Handshake Completes → Start Async Reading
    ↓
GSource Callback Continuously Reads Data
    ↓
FLV Tags Queued in pending_tags
    ↓
create() Returns Queued Tags
```

## Current Status

### ✅ Completed
- Created async read callback infrastructure
- Integrated GSource creation and attachment
- Added handshake completion trigger
- Simplified create() to just return queued data
- Buffer optimizations (16KB, 256KB SO_RCVBUF)

### ⚠️ Issues Encountered
1. **Handshake Timing**: Need to balance manual vs async reading during handshake
2. **Create() Frequency**: GStreamer doesn't call create() often enough to drive handshake completion
3. **Event Processing**: Need to ensure g_main_context_iteration() runs frequently

### 🔧 Current Challenge
The handshake phase requires synchronous processing before async reading can start. The retry loop in `create()` needs to actively process handshake steps, but:
- First `create()` call returns FLV header immediately
- Second `create()` call needs to complete handshake
- If handshake takes time, FFmpeg may timeout

## Files Modified

- `gst/rtmp2client.h`: Added `rtmp2_client_start_reading()` declaration
- `gst/rtmp2client.c`: Implemented async reading callback and setup
- `gst/gstrtmp2serversrc.c`: Integrated async reading into create() flow

## Testing Status

### What Works
- Server starts and accepts connections
- GSource infrastructure is properly set up
- Async callback is registered

### What Doesn't Work Yet
- Handshake completion timing
- FFmpeg reports "Cannot read RTMP handshake response"
- Connection fails before streaming starts

## Next Steps

### Option 1: Fix Handshake Timing
- Increase handshake processing frequency in create()
- Add dedicated handshake event source
- Better event loop integration

### Option 2: Hybrid Approach
- Keep async reading for post-handshake
- Use dedicated handshake thread
- Simpler state management

### Option 3: Full Thread-Based
- Create dedicated read thread per client
- Use mutexes for pending_tags queue
- More traditional server architecture

## Recommendation

The async GSource approach is architecturally clean and leverages GLib's event loop. The handshake timing issue is solvable by:

1. Processinghandshake in the retry loop more aggressively
2. Ensuring g_main_context_iteration() runs frequently enough
3. Adding timeout handling for handshake phase

With these adjustments, the async reading should work perfectly and solve the frame loss issue.

## Related Files

- See `OPTIMIZATION_SUMMARY.md` for buffer optimization details
- See issue #2 for original problem description

