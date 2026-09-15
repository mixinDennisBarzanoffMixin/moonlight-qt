# Moonlight zoom and pointer diagnostics

This collector creates a small, shareable report for comparing a Moonlight
client on macOS with one on Windows. It is intended for cases where the remote
desktop looks unexpectedly small, zoom gestures behave differently, or pointer
coordinates do not line up with the streamed image.

The scripts only collect an allowlist of Moonlight preferences. They do **not**
export pairing certificates, private keys, passwords, or the complete Moonlight
settings store.

## Reproduce consistently

1. Set Moonlight to 1920x1080, 60 FPS, 20 Mbps, HEVC, and hardware decoding.
2. Enable remote-desktop optimized mouse mode and the performance overlay.
3. Connect to the `Desktop` application on `video-editing-1`.
4. Maximize Moonlight without changing its settings.
5. Take one screenshot before using zoom or scrolling.
6. Scroll vertically, pinch once (trackpads only), and click each screen corner.
7. Take a second screenshot and disconnect.
8. Run the collector immediately after disconnecting.

On macOS:

```sh
./tools/zoom-diagnostics/collect-macos.sh
```

On Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\zoom-diagnostics\collect-windows.ps1
```

Each command writes a timestamped report under `moonlight-diagnostics/`. Attach
that report and the two screenshots to the same issue. Before sharing a report,
review it for machine names or network addresses you consider private.

## What the report distinguishes

- Client display resolution and operating-system DPI scaling
- Moonlight stream resolution versus local window/display dimensions
- Fullscreen, borderless, V-Sync, frame pacing, codec, and absolute-mouse mode
- Client network link speed and whether Tailscale reaches the host directly
- Recent Moonlight log lines related to rendering, scaling, and input

If both clients reproduce the issue while their local DPI differs, inspect the
host display mode and Sunshine resolution negotiation next. If only one client
reproduces it, compare that client's DPI, window mode, and absolute-mouse mode.

