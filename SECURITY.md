# Security Policy

FF-DEFLATE is maintained by FF-LEAF and parses compressed data and archive containers, so malformed input bugs matter.

## Supported Versions

The `main` branch receives security fixes before a stable release line exists.

## Reporting a Vulnerability

Please open a private advisory on GitHub if available. If private advisories are not enabled, open an issue with a minimal reproducer and avoid posting sensitive production data.

Useful details include:

- Compiler and platform.
- Exact compressed input or a minimized reproducer.
- Crash logs, sanitizer output, or returned error status.
- Whether the issue affects raw DEFLATE, zlib, ZIP, or PNG code paths.
