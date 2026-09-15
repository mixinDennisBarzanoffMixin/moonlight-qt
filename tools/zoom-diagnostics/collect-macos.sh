#!/bin/sh
set -eu

host_name="${MOONLIGHT_DIAG_HOST:-video-editing-1.taild1a1df.ts.net}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${MOONLIGHT_DIAG_OUT:-moonlight-diagnostics}"
out_file="$out_dir/moonlight-macos-$stamp.txt"
mkdir -p "$out_dir"

pref_domain="com.moonlight-stream.Moonlight"
pref_keys="width height fps bitrate videocfg videodec windowmode vsync framepacing mouseacceleration capturesyskeys hdr yuv444 showperfoverlay reversescroll"

{
  echo "Moonlight zoom diagnostics"
  echo "timestamp_utc=$stamp"
  echo "platform=macOS"
  printf "os_version="
  sw_vers -productVersion
  printf "os_build="
  sw_vers -buildVersion
  printf "architecture="
  uname -m
  printf "client_hostname="
  scutil --get ComputerName 2>/dev/null || hostname

  echo
  echo "[moonlight_preferences_allowlist]"
  for key in $pref_keys; do
    value="$(defaults read "$pref_domain" "$key" 2>/dev/null || true)"
    [ -n "$value" ] && printf '%s=%s\n' "$key" "$value"
  done

  echo
  echo "[displays]"
  system_profiler SPDisplaysDataType 2>/dev/null | sed -n '/Displays:/,$p'

  echo
  echo "[network_interfaces]"
  networksetup -listallhardwareports 2>/dev/null || true

  echo
  echo "[tailscale_path_to_host]"
  if command -v tailscale >/dev/null 2>&1; then
    tailscale ping -c 5 "$host_name" 2>&1 || true
  else
    echo "tailscale CLI not found"
  fi

  echo
  echo "[recent_moonlight_log_extract]"
  log show --last 15m --style compact \
    --predicate 'process == "Moonlight"' 2>/dev/null \
    | grep -Ei 'resolution|display|window|scale|dpi|mouse|touch|decoder|codec|bitrate|fps|direct|relay' \
    | tail -200 || true
} >"$out_file"

printf 'Wrote %s\n' "$out_file"

