# Next Session Plan - Chunk Parser V2 Implementation

## Current Status

**v0.8.0 in main**: 15-20% capture, production ready  
**Frame tracking**: Shows 9/60 frames received, 100% of those processed correctly  
**Bottleneck**: 51 frames lost in chunk parser layer

## SRS-Based Parser V2 - Implementation Plan

### Phase 1: Core Buffer (2-3 hours)

Implement SRS's `SrsFastStream` equivalent:

```c
typedef struct {
  guint8 *data;
  gsize size;
  gsize read_pos;
  gsize write_pos;
  GInputStream *stream;  // To read more data when needed
} FastBuffer;

// Like SRS's in_buffer->grow(skt, N)
gboolean fast_buffer_ensure(FastBuffer *buf, gsize needed, GError **error) {
  while ((buf->write_pos - buf->read_pos) < needed) {
    // Read more from socket
    gssize n = g_input_stream_read(buf->stream, 
        buf->data + buf->write_pos, 
        buf->size - buf->write_pos, NULL, error);
    if (n <= 0) return FALSE;
    buf->write_pos += n;
  }
  return TRUE;
}
```

### Phase 2: Chunk Parsing (3-4 hours)

Follow SRS's `recv_interlaced_message` pattern exactly:

```c
gboolean parse_one_chunk(FastBuffer *buf, Rtmp2ChunkMessage **msg) {
  // 1. Ensure 1 byte for basic header
  if (!fast_buffer_ensure(buf, 1, &err)) return FALSE;
  
  // 2. Parse basic header
  guint8 fmt, csid;
  // ... parse
  
  // 3. Ensure bytes for message header based on fmt
  gsize header_size = (fmt == 0) ? 11 : (fmt == 1) ? 7 : (fmt == 2) ? 3 : 0;
  if (!fast_buffer_ensure(buf, header_size, &err)) return FALSE;
  
  // 4. Parse message header
  // ... parse
  
  // 5. Calculate chunk payload size
  gsize payload_needed = min(chunk_size, msg_length - bytes_received);
  
  // 6. Ensure payload bytes available
  if (!fast_buffer_ensure(buf, payload_needed, &err)) return FALSE;
  
  // 7. Copy payload
  // ... copy
  
  // 8. Return message if complete
  if (bytes_received == msg_length) {
    return TRUE;  // Message complete
  }
  return FALSE;  // Need more chunks
}
```

### Phase 3: Integration (1-2 hours)

1. Replace `rtmp2_chunk_parser_process` calls with v2
2. Pass GInputStream to parser for `fast_buffer_ensure`
3. Test and iterate

### Phase 4: Optimization (1-2 hours)

- Tune buffer sizes
- Add proper error handling
- Remove old parser code

## Expected Outcome

**With SRS's `ensure()` pattern**: 70-90% capture  
**Why**: Parser waits for complete data instead of fragmenting

## Files to Modify

1. `gst/rtmp2chunk_v2.c` - New implementation
2. `gst/rtmp2chunk_v2.h` - New header
3. `gst/rtmp2client.c` - Use v2 parser
4. `meson.build` - Add v2 to build

## Total Estimate

**8-12 hours** of focused implementation following SRS patterns exactly.

## Fallback

If parser v2 doesn't achieve 70%+, we have:
- Working 15-20% in v0.8.0
- Professional architecture  
- Complete understanding of the problem

## Starting Point for Next Session

The stub in `rtmp2chunk_v2.c` has the basic buffer structure.  
Follow SRS's `srs_protocol_rtmp_stack.cpp` lines 775-1220 for complete implementation.

Key functions to implement:
1. `fast_buffer_ensure()` - Like SRS's `in_buffer_->grow()`
2. `parse_basic_header_v2()` - With ensure() calls
3. `parse_message_header_v2()` - With ensure() calls  
4. `read_chunk_payload_v2()` - With ensure() calls
5. `process_chunks_v2()` - Main loop like `recv_interlaced_message`

This is doable and will solve the throughput issue.

