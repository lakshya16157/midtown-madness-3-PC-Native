<#
.SYNOPSIS
Launch MM3 from its desktop shortcut with keyboard controls.
#>
$ErrorActionPreference = 'Stop'
try {
    $global:LASTEXITCODE = 0
    & (Join-Path $PSScriptRoot 'play.ps1') -Keyboard
    if ($LASTEXITCODE -ne 0) {
        $logs = Join-Path (Split-Path $PSScriptRoot -Parent) 'logs'
        throw "The game exited with code $LASTEXITCODE. Launch details are saved in $logs."
    }
} catch {
    # A hidden launcher still needs to explain missing files or a second click
    # while the game is running. Keep the existing play script's launch guard.
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show(
        $_.Exception.Message, 'Midtown Madness 3',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    exit 1
}
