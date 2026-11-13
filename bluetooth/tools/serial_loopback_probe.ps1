<#
  serial_loopback_probe.ps1  -  check a USB-serial dev board (ESP32 etc.) for a TX->RX loopback

  Why: the first ESP32 board failed every upload with esptool's
  "Serial data stream stopped: Possible serial noise or corruption". esptool --trace
  showed every SYNC packet coming back byte-for-byte (an echo), and the echo stayed
  even with EN held down (ESP32 in reset), i.e. a board-level TX<->RX short on the
  USB-serial (CH340) side. Use this to check a new board before debugging anything else.

  What it does: opens the port with DTR/RTS de-asserted (no auto-reset), optionally
  listens first (e.g. while you press buttons), then sends an ASCII probe and an
  esptool SYNC frame and prints exactly what comes back. Writes nothing to flash.

  Usage (close Arduino IDE / Serial Monitor first so the port is free):
    # 1) Loopback test: hold the board's EN/RESET button down, then run:
    powershell -ExecutionPolicy Bypass -File serial_loopback_probe.ps1 -Port COM5
    #    Healthy board: "got back 0 bytes" for both probes.
    #    Echo present with EN held => board fault, not driver/IDE/cable.

    # 2) Bootloader check: run with -ListenSec 10, then hold BOOT, tap EN, release BOOT.
    powershell -ExecutionPolicy Bypass -File serial_loopback_probe.ps1 -Port COM5 -ListenSec 10
    #    Healthy ESP32: prints the ROM banner ("waiting for download") and
    #    "genuine ROM SYNC reply (c0 01 08) present: True".
#>
param(
  [Parameter(Mandatory = $true)][string]$Port,
  [int]$Baud = 115200,
  [int]$ListenSec = 0,        # >0: listen first (e.g. while you press buttons) and print what arrives
  [int]$SettleMs = 1500       # wait after open before probing
)
$ErrorActionPreference = 'Stop'

function Show-Hex([byte[]]$b) { if ($b.Length -eq 0) { '(none)' } else { ($b | ForEach-Object { $_.ToString('x2') }) -join ' ' } }
function Show-Text([byte[]]$b) { -join ($b | ForEach-Object { if ($_ -eq 10) { "`n" } elseif ($_ -eq 13) { '' } elseif ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { '.' } }) }
function Read-For($sp, [int]$ms) {
  $buf = New-Object System.Collections.Generic.List[byte]
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $ms) {
    $n = $sp.BytesToRead
    if ($n -gt 0) {
      $tmp = New-Object byte[] $n
      $r = $sp.Read($tmp, 0, $n)
      for ($i = 0; $i -lt $r; $i++) { $buf.Add($tmp[$i]) }
    } else { Start-Sleep -Milliseconds 5 }
  }
  return ,($buf.ToArray())
}
function Contains-Seq([byte[]]$hay, [byte[]]$needle) {
  if ($hay.Length -lt $needle.Length) { return $false }
  for ($i = 0; $i -le $hay.Length - $needle.Length; $i++) {
    $ok = $true
    for ($j = 0; $j -lt $needle.Length; $j++) { if ($hay[$i + $j] -ne $needle[$j]) { $ok = $false; break } }
    if ($ok) { return $true }
  }
  return $false
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.Handshake = [System.IO.Ports.Handshake]::None
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()
"[$(Get-Date -f HH:mm:ss.fff)] $Port open at $Baud, DTR/RTS de-asserted"

if ($ListenSec -gt 0) {
  "[$(Get-Date -f HH:mm:ss.fff)] Listening for $ListenSec s..."
  $rx = Read-For $sp ($ListenSec * 1000)
  "[$(Get-Date -f HH:mm:ss.fff)] Received $($rx.Length) bytes while listening. Text view:"
  '----------8<----------'
  Show-Text $rx
  '---------->8----------'
} else {
  Start-Sleep -Milliseconds $SettleMs
}

# Probe 1: plain ASCII
$sp.DiscardInBuffer()
$probe = [Text.Encoding]::ASCII.GetBytes('LOOPBACK-TEST-0123456789')
$sp.Write($probe, 0, $probe.Length)
$rx1 = Read-For $sp 600
"[$(Get-Date -f HH:mm:ss.fff)] PROBE 1 sent ASCII 'LOOPBACK-TEST-0123456789' ($($probe.Length) bytes)"
"  got back $($rx1.Length) bytes: $(Show-Hex $rx1)"
"  as text: '$(Show-Text $rx1)'"
"  => echo of probe present: $(Contains-Seq $rx1 $probe)"

# Probe 2: esptool SYNC frame (identical to esptool's 46-byte frame)
$sync = [byte[]](@(0xc0, 0x00, 0x08, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x07, 0x12, 0x20) + (@(0x55) * 32) + @(0xc0))
$sp.DiscardInBuffer()
$sp.Write($sync, 0, $sync.Length)
$rx2 = Read-For $sp 1000
"[$(Get-Date -f HH:mm:ss.fff)] PROBE 2 sent esptool SYNC frame ($($sync.Length) bytes)"
"  got back $($rx2.Length) bytes: $(Show-Hex $rx2)"
"  => exact echo of SYNC present: $(Contains-Seq $rx2 $sync)"
"  => genuine ROM SYNC reply (c0 01 08) present: $(Contains-Seq $rx2 ([byte[]](0xc0, 0x01, 0x08)))"

$sp.Close()
"[$(Get-Date -f HH:mm:ss.fff)] $Port closed"
