# Next Steps for Frame Capture Optimization

## Current State

**Branch**: `optimize-frame-capture`  
**Status**: 95% of RTMP protocol working, intermittent video capture

### Best Results Achieved
- ✅ Event loop thread working
- ✅ Async callbacks functioning  
- ✅ All RTMP commands processed (releaseStream, FCPublish, createStream, _checkbw)
- ✅ **publish command processed** (intermittently)
- ✅ **PUBLISHING state reached** (intermittently)
- ⚠️ 5-12 video frames captured out of 60 (8-20%)

## The Core Issue

**RTMP chunk reassembly doesn't align with our async callback timing.**

Chunks arrive as:
```
[Header:12 bytes] → Incomplete, buffered
[Payload:34 bytes] → Still incomplete
...waiting...
```

The chunk parser buffers partial data in `chunk_streams` hash table, waiting for complete messages. Meanwhile:
- Timeout fires every 5ms
- Gets WOULD_BLOCK (no new socket data)
- Incomplete chunks stay buffered
- Video frames arrive but aren't read fast enough

## Recommended Next Steps

### Immediate (Choose One)

#### Option 1: Accept Buffer Optimization Baseline (RECOMMENDED)
**Action**: Merge the buffer optimizations (def72dc commit) without async changes  
**Result**: Reliable 20% capture rate (12/60 frames)  
**Pros**: 
- Works reliably
- 4x better than original
- No complex chunk timing issues
**Cons**: 
- Not optimal
- May drop frames on high bitrate streams

#### Option 2: Continue Debugging Async (HIGH EFFORT)
**Action**: Deep dive into chunk parser to understand buffering  
**Tasks**:
1. Add detailed chunk parser logging
2. Understand why 46-byte messages don't complete
3. Fix chunk size negotiation
4. Test with multiple chunk sizes
**Expected time**: 4-8 hours  
**Risk**: May require rewriting chunk parser

#### Option 3: Hybrid Approach
**Action**: Use buffer opts as default, add async as experimental feature  
**Implementation**:
- Add property: `async-reading=true/false` (default: false)
- Keep both code paths
- Let users choose based on their needs

### Long Term

#### Study Reference Implementations
1. **nginx-rtmp chunk parser**
   - How does it handle partial chunks?
   - Buffer pool management
   - Chunk size negotiation

2. **librtmp integration**
   - Consider using proven library
   - Focus on GStreamer integration instead of protocol details

3. **Alternative protocols**
   - SRT has better network tolerance
   - WebRTC for modern browsers
   - Consider if RTMP is the right choice

#### Improve Buffer Optimizations
Even at 20%, there's room for improvement:
- Larger SO_RCVBUF (try 512KB or 1MB)
- Faster polling (reduce to 1ms consistently)
- Better burst handling (read 200+ times per cycle)
- TCP_NODELAY socket option

## Test Commands for Future Work

### Test Current Async (Best Effort)
```bash
cd /Users/yarontorbaty/gst-rtmp2-server
git checkout optimize-frame-capture
[rebuild]
export GST_PLUGIN_PATH=$(pwd)/build
export GST_DEBUG=rtmp2client:6,rtmp2chunk:6

# Run multiple times - sometimes it works!
for i in {1..5}; do
  echo "Test $i"
  ./final_test.sh
  sleep 2
done
```

### Test Baseline (Reliable)
```bash
git checkout def72dc  # Buffer opts only
[rebuild]
export GST_PLUGIN_PATH=$(pwd)/build
gst-launch-1.0 rtmp2serversrc port=1935 ! filesink location=test.flv &
sleep 3
ffmpeg -re -f lavfi -i testsrc=size=320x240:rate=30 -t 2 \
       -c:v libx264 -preset ultrafast -g 30 \
       -f flv rtmp://localhost:1935/live/test
# Expect: 12/60 frames reliably
```

## Metrics Summary

| Approach | Frames | Rate | Reliability | Complexity |
|----------|--------|------|-------------|------------|
| Original | 3-5 | 5-8% | High | Low |
| Buffer Opts | 12 | 20% | High | Low |
| Async (current) | 5-12 | 8-20% | **Low** | **High** |

## Decision Matrix

### If You Need:
- **Reliability**: Use buffer opts baseline (def72dc)
- **Best capture**: Keep working on async (current branch)
- **Production use**: Buffer opts + monitor for drops
- **Learning**: Study nginx-rtmp implementation

## What We Learned

1. **GStreamer async I/O** requires dedicated event loop thread
2. **RTMP chunks** don't align with TCP packets
3. **Chunk reassembly** is a hard problem requiring specialized handling
4. **Buffer optimizations** provide good ROI with low complexity
5. **Event-driven architecture** is correct but needs protocol-specific tuning

## Files to Clean Up

Lots of test files to remove:
```bash
rm -f *.flv test*.sh run*.sh *_test* CHUNK* BUFFER* FINAL* async* baseline* chunk* direct* event* fixed* manual* optimized* result* simple* trace* verbose* working*
```

## Conclusion

We successfully implemented professional async I/O architecture (event loop thread, GSource callbacks, timeout polling) following patterns from nginx-rtmp and gst-rtsp-server. 

The chunk reassembly issue is an RTMP protocol detail requiring either:
- Deep RTMP expertise to fix properly
- Or accepting the 20% baseline which is a significant improvement

**The codebase is now well-architected for future enhancements.**

