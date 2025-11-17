# Async Reading Implementation - Findings

## Branch: optimize-frame-capture
## Issue: #2

## Summary

Implemented GSource-based asynchronous reading but discovered a fundamental GMainContext integration issue.

## What We Built

### 1. Async Reading Infrastructure ✅
- `rtmp2_client_read_cb()`: Callback triggered by GSource when socket has data
- `rtmp2_client_start_reading()`: Sets up G_IO_IN monitoring on client socket
- Thread-safe with mutex protection
- Activated after handshake completion

### 2. Test Results

**Baseline (buffer optimizations only):**
- 12/60 frames captured (20%)
- This is better than original ~5% but still inadequate

**With Async Reading:**
- Async callback fires ONCE after handshake
- Processes connect command successfully
- Then NEVER fires again until FFmpeg times out
- Result: 0 useful frames

## Root Cause Analysis

### The Problem

The async callback is correctly set up and fires once, proving the infrastructure works. However:

1. **Async callback fires**: Thread `0x600000884480` processes connect at 3.198 seconds
2. **Callback returns**: After processing connect and hitting WOULD_BLOCK
3. **Event loop not pumped**: Nothing calls `g_main_context_iteration()` 
4. **No more callbacks**: GSource never triggers again despite FFmpeg sending data
5. **FFmpeg timeout**: At 16.005 seconds, FFmpeg closes connection
6. **Final callback**: Async callback fires once more, sees EOF

### Why Event Loop Isn't Pumping

The retry loop in `create()`:
```c
while (retry_count < max_retries) {
    g_mutex_lock (&src->clients_lock);
    // ... check for tags ...
    g_mutex_unlock (&src->clients_lock);
    
    g_main_context_iteration(src->context, FALSE);  // ← Only here
    g_usleep(1000);
    
    g_mutex_lock (&src->clients_lock);
    // Process handshakes...
}
```

**Problem**: The async callback needs:
1. Event loop to call `g_main_context_iteration()`
2. Mutex to be unlocked so it can acquire it
3. This to happen FREQUENTLY (every few ms)

But the retry loop:
- Calls iteration once per cycle
- With 1-10ms sleep between cycles  
- FFmpeg sends releaseStream/FCPublish/createStream in quick succession
- By the time next iteration happens, FFmpeg has already timed out

## The Fundamental Issue

**GStreamer's GstPushSrc is pull-based despite the name.**

Even though the element extends `GstPushSrc`, the `create()` function is still called **on-demand** by GStreamer's data flow. Between `create()` calls:
- The retry loop is blocked waiting for data
- Event loop isn't pumping
- Async callbacks can't fire
- Data accumulates and is lost

### Proof

Looking at the thread IDs:
- `0x60000089db00`: Main create() thread - processes handshake
- `0x600000884480`: GSource callback thread - fires ONCE then starves

The callback thread is a GLib worker thread waiting for `g_main_context_iteration()` to be called so it can process events.

## Possible Solutions

### Option 1: Dedicated Event Loop Thread (RECOMMENDED)
Create a separate thread that does nothing but pump the event loop:

```c
static gpointer
event_loop_thread (gpointer user_data) {
    GstRtmp2ServerSrc *src = user_data;
    while (src->running) {
        g_main_context_iteration(src->context, TRUE);
    }
    return NULL;
}
```

**Pros**: Clean separation, async callbacks will fire reliably
**Cons**: Need to manage thread lifecycle

### Option 2: Use Default Main Context
Attach GSources to `g_main_context_default()` which GStreamer already pumps:

```c
g_source_attach (client->read_source, g_main_context_default());
```

**Pros**: No custom threading needed
**Cons**: May conflict with GStreamer's event handling

### Option 3: Abandon Async, Use Direct Threading
Create a read thread per client instead of using GSource:

```c
GThread *read_thread = g_thread_new("rtmp-reader", 
                                     rtmp2_client_read_thread, client);
```

**Pros**: Simple, proven pattern
**Cons**: More threads, manual queue management

### Option 4: Make Element Truly Push-Mode
Override more GstPushSrc methods to run event loop independently:

```c
static gboolean
gst_rtmp2_server_src_start (GstBaseSrc * bsrc) {
    // Start event loop in separate thread
    src->event_thread = g_thread_new("event-loop", ...);
}
```

**Pros**: Architecturally correct
**Cons**: Major refactoring

## Recommendation

**Option 1 (Dedicated Event Loop Thread)** is the cleanest solution:
- Minimal code changes (~20 lines)
- Leverages existing GSource infrastructure
- Clear separation of concerns
- Proven pattern used by other GStreamer elements

## Current Code Status

All async infrastructure is in place and working:
- ✅ GSource creation
- ✅ Callback implementation  
- ✅ Thread safety (mutex)
- ✅ Handshake triggering
- ⚠️ Event loop pumping (needs dedicated thread)

## Files Modified

- `gst/rtmp2client.c`: Async callback and setup
- `gst/rtmp2client.h`: Function declarations
- `gst/gstrtmp2serversrc.c`: Integration and handshake processing

## Next Step

Implement Option 1: Add event loop thread in `gst_rtmp2_server_src_start()` that continuously calls `g_main_context_iteration()`. This will make async callbacks fire reliably and should achieve >90% frame capture.

## Test Evidence

```
Handshake complete at: 3.198 seconds
Async callback fires:   3.198 seconds (processes connect)
Async callback again:   16.005 seconds (13 second gap - FFmpeg timeout)
Commands processed:     connect only (missing releaseStream, FCPublish, createStream)
```

The 13-second gap proves the async callback isn't being triggered despite FFmpeg sending data.

