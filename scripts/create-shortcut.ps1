<#
.SYNOPSIS
Create a Midtown Madness 3 desktop shortcut for this checkout.

.DESCRIPTION
Run again after moving the game folder to update the shortcut's paths.
Use -Path to choose a different shortcut destination.
#>
param(
    [string]$Path = (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Midtown Madness 3.lnk')
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$launcher = Join-Path $PSScriptRoot 'play-desktop.ps1'
$icon = Join-Path $repo 'src\mm3.ico'
$executable = @('build\RelWithDebInfo\mm3_recomp.exe', 'build\Release\mm3_recomp.exe') |
    ForEach-Object { Join-Path $repo $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $executable) { throw 'No build found: run scripts\setup.ps1 first.' }
if (-not (Test-Path -LiteralPath (Join-Path $repo 'game_files\default.xbe') -PathType Leaf)) {
    throw 'game_files\ missing: run scripts\setup.ps1 -Iso <disc>.'
}
if (-not (Test-Path -LiteralPath $icon -PathType Leaf)) { throw "Game icon missing: $icon" }

$powerShell = Join-Path $repo 'tools\bin\powershell\pwsh.exe'
if (-not (Test-Path -LiteralPath $powerShell -PathType Leaf)) {
    $installedPowerShell = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if ($installedPowerShell) { $powerShell = $installedPowerShell.Source }
    else { $powerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe' }
}
$shortcutPath = [IO.Path]::GetFullPath($Path)
if ([IO.Path]::GetExtension($shortcutPath) -ine '.lnk') { throw 'Shortcut path must end in .lnk.' }
if (-not (Test-Path -LiteralPath (Split-Path $shortcutPath -Parent) -PathType Container)) {
    throw "Shortcut destination folder missing: $shortcutPath"
}
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $powerShell
$shortcut.Arguments = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}"' -f $launcher
$shortcut.WorkingDirectory = $repo
$shortcut.IconLocation = "$icon,0"
$shortcut.Description = 'Play Midtown Madness 3 with keyboard controls'
$shortcut.WindowStyle = 7
$shortcut.Save()
Write-Host "Created $shortcutPath"
