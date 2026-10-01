# Installs the Flowstate VST3 plug-ins from this folder into C:\Program Files\Common Files\VST3.
#   powershell -ExecutionPolicy Bypass -File .\install.ps1              (asks for admin)
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $argList = @('-NoExit', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    if ($Uninstall) { $argList += '-Uninstall' }
    Start-Process powershell -Verb RunAs -ArgumentList $argList
    exit
}

$dest = Join-Path $env:CommonProgramFiles 'VST3'
$bundles = @('Flowstate.vst3', 'Flowstate MIDI FX.vst3')

foreach ($b in $bundles) {
    $target = Join-Path $dest $b
    if (Test-Path $target) {
        try { Remove-Item -Recurse -Force $target }
        catch { throw "Couldn't replace $target. Close any DAW that has Flowstate loaded and try again." }
    }
}
if ($Uninstall) { Write-Host 'Flowstate is removed. Rescan plug-ins in your DAW.'; exit }

New-Item -ItemType Directory -Force $dest | Out-Null
foreach ($b in $bundles) {
    Copy-Item -Recurse (Join-Path $PSScriptRoot "VST3\$b") $dest
    Write-Host "  $(Join-Path $dest $b)"
}
$build = Get-Content (Join-Path $PSScriptRoot 'BUILD_ID') -ErrorAction SilentlyContinue
Write-Host "Installed Flowstate $build. Rescan plug-ins in your DAW."
