# MediaMTX/gortmplib Analysis - The Path to 100%

## Key Findings from Source Code

I analyzed MediaMTX ([bluenviron/mediamtx](https://github.com/bluenviron/mediamtx)) and their RTMP library gortmplib. Here's how they achieve reliable RTMP chunk handling:

### The Pattern (from gortmplib/pkg/rawmessage/reader.go)

```go
// Reader has a bufio.Reader
type Reader struct {
    br *bufio.Reader  // ← KEY: Buffers TCP reads internally
    chunkStreams map[byte]*readerChunkStream
}

// Read() loops until complete message
func (r *Reader) Read() (*Message, error) {
    for {  // ← INFINITE LOOP - keeps trying!
        msg, err := rc.readMessage(typ)
        if err != nil {
            if errors.Is(err, errMoreChunksNeeded) {
                continue  // ← Loop again, read next chunk
            }
            return nil, err
        }
        return msg, err  // Only return when complete
    }
}
```

### Critical Elements

1. **bufio.Reader** - Buffers TCP packet fragmentation
2. **Synchronous blocking reads** - Can wait for data
3. **Dedicated goroutine per connection** - Blocking is OK
4. **Internal loop** - Keeps reading until message complete
5. **errMoreChunksNeeded** - Internal signal to continue looping

### MediaMTX Connection Pattern (internal/servers/rtmp/conn.go)

```go
func (c *conn) runInner() error {
    readerErr := make(chan error)
    go func() {  // ← Dedicated goroutine
        readerErr <- c.runReader()
    }()
    // ... manages goroutine lifecycle
}

func (c *conn) runReader() error {
    conn := &gortmplib.ServerConn{RW: c.nconn}
    conn.Initialize()
    conn.Accept()  // Synchronous - blocks until complete
    // ... continues reading
}
```

## Why Our Approach Didn't Work

### What We Tried

1. **Async GSource callbacks** - Event-driven, non-blocking
2. **Event loop thread** - Pumps events
3. **Timeout polling** - Retries every 5-50ms
4. **GBufferedInputStream** - Tried to buffer TCP

### The Fundamental Incompatibility

**gortmplib**: 
- Synchronous blocking reads
- `bufio.Reader` waits for complete data
- Dedicated thread per connection
- Can loop indefinitely until message complete

**Our async approach**:
- Non-blocking reads (MUST return quickly)
- GSource callbacks (can't block event loop)
- Shared event thread for all clients  
- Returns errMoreChunksNeeded → waits for next callback

**The problem**: When chunks arrive fragmented:
1. Async callback reads partial chunk
2. WOULD_BLOCK (no more data yet)
3. Returns, waits for next trigger
4. By then, partial chunk is stale or new data arrived
5. Chunks accumulate in parser, never complete

## The Solution: Dedicated Read Thread Per Client

Following gortmplib's pattern exactly:

```c
// Per-client read thread (like Go goroutine)
static gpointer
client_read_thread_func(gpointer user_data) {
    Rtmp2Client *client = user_data;
    
    // Create buffered input (can block - we're in dedicated thread)
    GInputStream *buffered = g_buffered_input_stream_new(client->input_stream);
    g_buffered_input_stream_set_buffer_size(G_BUFFERED_INPUT_STREAM(buffered), 65536);
    
    while (client->thread_running) {
        // Synchronous read - blocks until data available
        gssize bytes = g_input_stream_read(buffered, buffer, sizeof(buffer), NULL, &error);
        
        // Process immediately - no WOULD_BLOCK issues
        rtmp2_chunk_parser_process(&client->chunk_parser, buffer, bytes, ...);
        
        // If incomplete, loop continues and reads more
        // bufio.Reader handles TCP fragmentation automatically
    }
}
```

### Why This Works

1. **Blocking OK** - Dedicated thread can wait
2. **bufio.Reader equivalent** - GBufferedInputStream works with blocking reads
3. **Continuous loop** - Like gortmplib's `for` loop
4. **No async complexity** - Simple synchronous flow
5. **TCP fragmentation handled** - Buffer layer reassembles packets

## Implementation Plan

### Phase 1: Basic Thread
1. Add `GThread *read_thread` to Rtmp2Client
2. Create thread after handshake completes
3. Thread runs synchronous buffered reads in loop
4. Queues complete messages in pending_tags

### Phase 2: Remove Async Complexity
1. Remove GSource callbacks
2. Remove event loop gymnastics  
3. Remove timeout polling
4. Keep simple: threads + mutexes

### Phase 3: Optimize
1. Thread pool for multiple clients
2. Better buffer sizing
3. Flow control

## Estimated Work

**Time**: 2-3 hours  
**Complexity**: Medium (threading is simpler than async)  
**Confidence**: High (proven pattern from MediaMTX)

## Alternative: Super-Aggressive Buffer Optimization

If threading is too much work, we could:
1. Go back to baseline (commit def72dc)
2. Make it MUCH more aggressive:
   - Read 1000+ times per cycle (vs 100)
   - Sleep 100μs (vs 10ms)
   - Even larger buffers (1MB SO_RCVBUF)
3. Might get to 50-70% without threading complexity

## Recommendation

**For 100% solution**: Implement dedicated read thread per client (2-3 hours)  
**For faster deployment**: Super-aggressive buffer opts (30 minutes, ~50-70% capture)

The MediaMTX source code clearly shows: **Proper RTMP requires synchronous buffered reads in dedicated threads**. There's no way around it with async I/O.

