$ErrorActionPreference = "SilentlyContinue"

$HostName = if ($env:MOONLIGHT_DIAG_HOST) { $env:MOONLIGHT_DIAG_HOST } else { "video-editing-1.taild1a1df.ts.net" }
$Stamp = (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")
$OutDir = if ($env:MOONLIGHT_DIAG_OUT) { $env:MOONLIGHT_DIAG_OUT } else { "moonlight-diagnostics" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutFile = Join-Path $OutDir "moonlight-windows-$Stamp.txt"

$PreferenceNames = @(
    "width", "height", "fps", "bitrate", "videocfg", "videodec",
    "windowmode", "vsync", "framepacing", "mouseacceleration",
    "capturesyskeys", "hdr", "yuv444", "showperfoverlay", "reversescroll"
)
$PreferencePaths = @(
    "HKCU:\Software\Moonlight Game Streaming Project\Moonlight",
    "HKCU:\Software\moonlight-stream.com\Moonlight"
)

$Lines = [System.Collections.Generic.List[string]]::new()
$Lines.Add("Moonlight zoom diagnostics")
$Lines.Add("timestamp_utc=$Stamp")
$Lines.Add("platform=Windows")
$Lines.Add("client_hostname=$env:COMPUTERNAME")
$Lines.Add("os=$([System.Environment]::OSVersion.VersionString)")
$Lines.Add("architecture=$env:PROCESSOR_ARCHITECTURE")
$Lines.Add("")
$Lines.Add("[moonlight_preferences_allowlist]")

$FoundPreferences = $false
foreach ($Path in $PreferencePaths) {
    if (Test-Path $Path) {
        $FoundPreferences = $true
        $Values = Get-ItemProperty -Path $Path
        foreach ($Name in $PreferenceNames) {
            if ($null -ne $Values.$Name) {
                $Lines.Add("$Name=$($Values.$Name)")
            }
        }
        break
    }
}
if (-not $FoundPreferences) {
    $Lines.Add("Moonlight preference registry key not found")
}

$Lines.Add("")
$Lines.Add("[dpi_scaling]")
$Desktop = Get-ItemProperty "HKCU:\Control Panel\Desktop"
$Lines.Add("LogPixels=$($Desktop.LogPixels)")
$Lines.Add("Win8DpiScaling=$($Desktop.Win8DpiScaling)")
$Lines.Add("AppliedDPI=$($Desktop.AppliedDPI)")

$Lines.Add("")
$Lines.Add("[displays_and_gpus]")
Get-CimInstance Win32_VideoController | ForEach-Object {
    $Lines.Add("name=$($_.Name); current=$($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution); refresh=$($_.CurrentRefreshRate); driver=$($_.DriverVersion)")
}

$Lines.Add("")
$Lines.Add("[network_adapters]")
Get-NetAdapter | Where-Object Status -eq "Up" | ForEach-Object {
    $Lines.Add("name=$($_.Name); description=$($_.InterfaceDescription); link_speed=$($_.LinkSpeed)")
}

$Lines.Add("")
$Lines.Add("[tailscale_path_to_host]")
$Tailscale = Get-Command tailscale.exe
if ($Tailscale) {
    $Lines.AddRange([string[]](& $Tailscale.Source ping -c 5 $HostName 2>&1))
} else {
    $Lines.Add("tailscale CLI not found")
}

$Lines.Add("")
$Lines.Add("[moonlight_binary]")
$Candidates = @(
    "$env:ProgramFiles\Moonlight Game Streaming\Moonlight.exe",
    "$env:ProgramFiles(x86)\Moonlight Game Streaming\Moonlight.exe"
)
foreach ($Candidate in $Candidates) {
    if (Test-Path $Candidate) {
        $Version = (Get-Item $Candidate).VersionInfo.FileVersion
        $Lines.Add("path=$Candidate")
        $Lines.Add("version=$Version")
        break
    }
}

$Lines | Set-Content -Encoding UTF8 $OutFile
Write-Host "Wrote $OutFile"

