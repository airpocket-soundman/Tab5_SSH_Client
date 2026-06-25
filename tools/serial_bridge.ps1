param(
    [string]$PortName = "COM4",
    [int]$BaudRate = 115200,
    [string]$LogPath = ".pio\serial-monitor.log",
    [string]$CommandPath = ".pio\serial-commands.txt"
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
$logFull = Join-Path $root $LogPath
$cmdFull = Join-Path $root $CommandPath
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $logFull) | Out-Null
"--- serial bridge start $(Get-Date -Format o) port=$PortName baud=$BaudRate ---" | Add-Content -Path $logFull -Encoding UTF8

while ($true) {
    $port = $null
    try {
        $port = New-Object System.IO.Ports.SerialPort $PortName,$BaudRate,'None',8,'One'
        $port.ReadTimeout = 100
        $port.WriteTimeout = 500
        $port.NewLine = "`r`n"
        $port.DtrEnable = $true
        $port.RtsEnable = $false
        $port.Open()
        "--- opened $(Get-Date -Format o) ---" | Add-Content -Path $logFull -Encoding UTF8

        while ($port.IsOpen) {
            try {
                $text = $port.ReadExisting()
                if ($text.Length -gt 0) {
                    $text | Add-Content -Path $logFull -Encoding UTF8 -NoNewline
                }
            } catch {
                "--- read error $(Get-Date -Format o): $($_.Exception.Message) ---" | Add-Content -Path $logFull -Encoding UTF8
                break
            }

            if (Test-Path $cmdFull) {
                try {
                    $commands = Get-Content -Path $cmdFull -Encoding UTF8
                    Clear-Content -Path $cmdFull
                    foreach ($line in $commands) {
                        if ($line.Trim().Length -gt 0) {
                            "--- tx $(Get-Date -Format o): $line ---" | Add-Content -Path $logFull -Encoding UTF8
                            $port.Write("$line`r`n")
                        }
                    }
                } catch {
                    "--- tx error $(Get-Date -Format o): $($_.Exception.Message) ---" | Add-Content -Path $logFull -Encoding UTF8
                }
            }
            Start-Sleep -Milliseconds 50
        }
    } catch {
        "--- open error $(Get-Date -Format o): $($_.Exception.Message) ---" | Add-Content -Path $logFull -Encoding UTF8
    } finally {
        if ($port -ne $null) {
            try { $port.Close() } catch {}
            try { $port.Dispose() } catch {}
        }
    }
    Start-Sleep -Seconds 1
}
