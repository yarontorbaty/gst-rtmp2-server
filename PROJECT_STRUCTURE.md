# Project Structure

This document describes the structure of the GStreamer RTMP2 Server Plugin project.

## Directory Layout

```
gst-rtmp2-server/
├── meson.build              # Main Meson build configuration
├── meson_options.txt         # Build options
├── README.md                 # Project documentation
├── CONTRIBUTING.md          # Contribution guidelines
├── LICENSE                   # LGPL license
├── .editorconfig            # Editor configuration
├── .gitignore               # Git ignore rules
├── gst/                     # Plugin source code
│   ├── gstrtmp2server.c     # Plugin registration
│   ├── gstrtmp2server.h     # Main plugin header
│   ├── gstrtmp2serversrc.c  # Main element implementation
│   ├── gstrtmp2serversrc.h  # Element header
│   ├── rtmp2handshake.c     # RTMP handshake implementation
│   ├── rtmp2handshake.h     # Handshake header
│   ├── rtmp2chunk.c         # RTMP chunk protocol parser
│   ├── rtmp2chunk.h         # Chunk parser header
│   ├── rtmp2flv.c           # FLV tag demuxer
│   ├── rtmp2flv.h           # FLV parser header
│   ├── rtmp2client.c        # Client connection handler
│   └── rtmp2client.h        # Client header
└── tests/                   # Test scripts
    ├── test-basic.sh        # Basic functionality test
    ├── test-multiple-clients.sh  # Multiple client test
    └── test-stream-key.sh   # Stream key validation test
```

## Component Overview

### Core Plugin Files

- **gstrtmp2server.c/h**: Plugin registration and initialization
- **gstrtmp2serversrc.c/h**: Main GStreamer element (`rtmp2serversrc`)

### RTMP Protocol Implementation

- **rtmp2handshake.c/h**: RTMP handshake (C0/C1/C2, S0/S1/S2)
- **rtmp2chunk.c/h**: RTMP chunk protocol parser
- **rtmp2flv.c/h**: FLV tag demuxing for video/audio streams
- **rtmp2client.c/h**: Client connection management and state handling

## Build Output

After building, the plugin library will be created as:
- `build/gst/libgstrtmp2server.so` (or `.dylib` on macOS)

The plugin will be installed to:
- `$PREFIX/lib/gstreamer-1.0/libgstrtmp2server.so`

## Dependencies

- GStreamer 1.20.0+
- GStreamer Base 1.20.0+
- GStreamer App 1.20.0+
- GStreamer Video 1.20.0+
- GStreamer Audio 1.20.0+
- GLib 2.56.0+
- GIO 2.56.0+

## Testing

Test scripts are located in the `tests/` directory. They can be run after building:

```bash
./tests/test-basic.sh
./tests/test-multiple-clients.sh
./tests/test-stream-key.sh
```

## Future Enhancements

Potential areas for expansion:

- Full AMF0/AMF3 command parsing
- Multiple simultaneous active streams
- Authentication mechanisms
- Additional codec support
- Performance optimizations
- Unit test suite
- Integration with GStreamer's test framework

