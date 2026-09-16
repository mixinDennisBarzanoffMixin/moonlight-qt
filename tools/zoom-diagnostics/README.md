# Moonlight zoom and pointer diagnostics

This collector creates a small, shareable report for comparing a Moonlight
client on macOS with one on Windows. It is intended for cases where the remote
desktop looks unexpectedly small, zoom gestures behave differently, or pointer
coordinates do not line up with the streamed image.

The scripts only collect an allowlist of Moonlight preferences. They do **not**
export pairing certificates, private keys, passwords, or the complete Moonlight
settings store.

## Reproduce consistently

1. Set Moonlight to 1920x1080, 60 FPS, 10 Mbps, HEVC, and hardware decoding.
2. Enable remote-desktop optimized mouse mode and the performance overlay.
3. Connect to the `Desktop` application on `video-editing-1`.
4. Maximize Moonlight without changing its settings.
5. Take one screenshot before using zoom or scrolling.
6. In KiCad, scroll slowly for five seconds, scroll quickly for five seconds,
   pinch once (trackpads only), and click each screen corner.
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

To verify what the Windows input stack actually receives, run the wheel-only
probe in the logged-in desktop session, reproduce in KiCad, and wait for it to
exit:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\zoom-diagnostics\windows-wheel-probe.ps1 -DurationSeconds 120
```

It writes only wheel direction/delta, timing, injection flags, pointer position,
and the foreground process name to
`%LOCALAPPDATA%\MoonlightZoomDebug\windows-wheel.log`. It does not record keys,
buttons, window titles, clipboard contents, or credentials.

For the opt-in KiCad comparison build, launch Moonlight with
`MOONLIGHT_CAD_SCROLL_FILTER=1`. On macOS this suppresses horizontal trackpad
drift and coalesces vertical wheel input. The default interval is 40 ms and can
be changed with `MOONLIGHT_CAD_SCROLL_INTERVAL_MS`; normal Moonlight behavior is
unchanged when the filter is disabled.

Each command writes a timestamped report under `moonlight-diagnostics/`. Attach
that report and the two screenshots to the same issue. Before sharing a report,
review it for machine names or network addresses you consider private.

## What the report distinguishes

- Client display resolution and operating-system DPI scaling
- Moonlight stream resolution versus local window/display dimensions
- Fullscreen, borderless, V-Sync, frame pacing, codec, and absolute-mouse mode
- Client network link speed and whether Tailscale reaches the host directly
- Recent Moonlight log lines related to rendering, scaling, and input
- Each wheel event's timing and raw high-resolution delta (`wheel-raw`)
- The exact Windows wheel delta sent, including client clamping or rounding to
  zero (`wheel-send`)
- Scroll events discarded before transmission and the reason (`wheel-drop`)
- Wheel deltas received by Windows, including whether Sunshine injected them
  and which process was in the foreground (`WheelProbe`)
- CAD-filter decisions, including coalesced vertical input and suppressed
  horizontal drift (`wheel-filter`)

If both clients reproduce the issue while their local DPI differs, inspect the
host display mode and Sunshine resolution negotiation next. If only one client
reproduces it, compare that client's DPI, window mode, and absolute-mouse mode.
