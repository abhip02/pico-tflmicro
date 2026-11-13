<#
  watch_usb.ps1  -  show whether Windows detects a board when you plug it in

  Read-only. Snapshots all Plug-and-Play devices, then prints every device that
  appears or disappears during the window, and the COM ports at the end.

  Usage:
    powershell -ExecutionPolicy Bypass -File watch_usb.ps1 -Seconds 60
    # then plug the board in (and, for the nRF52840, double-tap reset to enter the bootloader)

  Reading the result:
    - New "USB Serial Device (COMx)" / CH340 / CP210x line  -> board detected, use that COM port
    - "Unknown USB Device (Device Descriptor Request Failed)" -> bad cable/port/board USB
    - Nothing at all while the board's LED is on            -> almost always a charge-only cable
#>
param([int]$Seconds = 60)
function Snap { $h = @{}; Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | ForEach-Object { $h[$_.InstanceId] = $_ }; $h }
$before = Snap
"[$(Get-Date -f HH:mm:ss)] Watching for $Seconds s ($($before.Count) devices present)..."
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
  Start-Sleep -Milliseconds 700
  $now = Snap
  foreach ($k in $now.Keys)    { if (-not $before.ContainsKey($k)) { $d = $now[$k];    "[$(Get-Date -f HH:mm:ss)] + ADDED   [$($d.Status)] $($d.Class) | $($d.FriendlyName) | $k" } }
  foreach ($k in $before.Keys) { if (-not $now.ContainsKey($k))    { $d = $before[$k]; "[$(Get-Date -f HH:mm:ss)] - REMOVED $($d.Class) | $($d.FriendlyName) | $k" } }
  $before = $now
}
"[$(Get-Date -f HH:mm:ss)] Done. COM ports now:"
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } | ForEach-Object { "  $($_.Name)  [$($_.PNPDeviceID)]" }
