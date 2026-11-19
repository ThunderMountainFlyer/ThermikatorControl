param(
    [string]$Port = 'COM4'
)

$ErrorActionPreference = 'Stop'
$logPath = Join-Path $PSScriptRoot ('HardwareTiming-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
$serialConnection = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serialConnection.DtrEnable = $true
$serialConnection.ReadTimeout = 2000
$serialConnection.NewLine = "`n"
$logWriter = $null
try {
    $serialConnection.Open()
    $logWriter = [System.IO.StreamWriter]::new($logPath, $false, [System.Text.UTF8Encoding]::new($true))
    Write-Host 'Verbunden. Autothrust-Schalter AUS und dann EIN zum Starten.'
    $deadline = [DateTime]::UtcNow.AddSeconds(600)
    $started = $false
    $complete = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $line = $serialConnection.ReadLine().TrimEnd("`r") }
        catch [System.TimeoutException] { continue }
        if (-not $started -and $line.StartsWith('# START.')) {
            $started = $true
            $deadline = [DateTime]::UtcNow.AddSeconds(180)
        }
        $logWriter.WriteLine($line)
        $logWriter.Flush()
        Write-Host $line
        if ($line.StartsWith('# DONE.')) { $complete = $true; break }
    }
    if (-not $complete) { throw "Start nicht innerhalb von 10 Minuten oder Messung nicht innerhalb von 180 Sekunden beendet. Teillog: $logPath" }
    Write-Host "Messausgabe gespeichert: $logPath"
}
finally {
    if ($null -ne $logWriter) { $logWriter.Dispose() }
    if ($serialConnection.IsOpen) { $serialConnection.Close() }
    $serialConnection.Dispose()
}
