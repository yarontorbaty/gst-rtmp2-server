# Contributing to GStreamer RTMP2 Server Plugin

Thank you for your interest in contributing to the GStreamer RTMP2 Server Plugin!

## Getting Started

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Test your changes thoroughly
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

## Code Style

This project follows GStreamer coding conventions:

- Use 2 spaces for indentation (not tabs)
- Maximum line length: 80 characters (soft limit, 100 hard limit)
- Use GLib types (`gint`, `guint`, `gchar*`, etc.)
- Follow GObject naming conventions
- Add proper error handling and logging

### Example

```c
static gboolean
my_function (GstElement * element, const gchar * name, GError ** error)
{
  if (!name || !name[0]) {
    g_set_error (error, GST_CORE_ERROR, GST_CORE_ERROR_FAILED,
        "Invalid name");
    return FALSE;
  }

  GST_DEBUG_OBJECT (element, "Processing name: %s", name);
  return TRUE;
}
```

## Testing

Before submitting a pull request:

1. Build the plugin successfully
2. Test with common RTMP clients (FFmpeg, OBS)
3. Test with multiple simultaneous connections
4. Verify error handling works correctly
5. Check for memory leaks using Valgrind

### Running Tests

```bash
# Build with debug symbols
meson setup build -Dbuildtype=debug

# Run with Valgrind
valgrind --leak-check=full gst-launch-1.0 rtmp2serversrc port=1935 ! fakesink
```

## Commit Messages

Follow the GStreamer commit message format:

```
element: Short description

Longer description explaining what and why, not how.

Fixes #123
```

- First line: element name and short description (max 72 chars)
- Blank line
- Detailed explanation (wrap at 72 chars)
- Reference issues/PRs if applicable

## Pull Request Process

1. Update documentation if needed
2. Add tests if applicable
3. Ensure all tests pass
4. Update CHANGELOG if significant changes
5. Request review from maintainers

## Areas for Contribution

### High Priority

- Full AMF0 command parsing (connect, publish, play)
- Multiple simultaneous stream support
- Authentication mechanisms
- Additional codec support (VP8, VP9, Opus)
- Better error recovery

### Medium Priority

- Performance optimizations
- Unit tests
- Integration tests
- Documentation improvements
- Example pipelines

### Low Priority

- Code cleanup
- Refactoring
- Additional logging
- Debugging tools

## Questions?

Feel free to open an issue for questions or discussions about contributions.

## License

By contributing, you agree that your contributions will be licensed under the LGPL, matching the project license.

