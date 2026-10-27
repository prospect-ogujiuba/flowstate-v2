# Installs the Flowstate VST3 plug-ins from this folder into C:\Program Files\Common Files\VST3, or the
# folder given with -Dest (e.g. your own plug-in folder).
#   powershell -ExecutionPolicy Bypass -File .\install.ps1              (asks for admin)
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 -Dest 'C:\Program Files\Common Files\VSTs'
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall   (with the same -Dest)
# The hosted agent service: a service.json in this folder is installed for you, or pass it yourself:
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 -Service https://flowstate.example.com -Token fst_...
# Testers double-click install.cmd, which runs this.
param([switch]$Uninstall, [string]$Dest, [string]$Service, [string]$Token, [switch]$Elevated)
$ErrorActionPreference = 'Stop'

# Where the plug-in looks for the agent service and the tester token (docs/bridge-spec.md). Written before
# asking for admin, so it lands in this user's profile, and the token never reaches the elevated window.
$settings = Join-Path $env:APPDATA 'Flowstate\service.json'
if (-not $Elevated) {
    if ($Uninstall) {
        if (Test-Path $settings) { Remove-Item -Force $settings; Write-Host "  removed $settings" }
    } elseif ($Service -or $Token) {
        if ($Service -notmatch '^https://[A-Za-z0-9.-]+(:[0-9]+)?/?$') { throw '-Service must be an https URL' }
        if ($Token -notmatch '^fst_[A-Za-z0-9_-]+$') { throw '-Token must be the fst_... token you were sent' }
        New-Item -ItemType Directory -Force (Split-Path $settings) | Out-Null
        @{ url = $Service.TrimEnd('/'); token = $Token } | ConvertTo-Json -Compress | Set-Content -Encoding ascii $settings
        Write-Host "  $settings (agent service)"
    } elseif (Test-Path (Join-Path $PSScriptRoot 'service.json')) {
        New-Item -ItemType Directory -Force (Split-Path $settings) | Out-Null
        Copy-Item -Force (Join-Path $PSScriptRoot 'service.json') $settings
        Write-Host "  $settings (agent service)"
    }
}

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Elevated')
    if ($Uninstall) { $argList += '-Uninstall' }
    if ($Dest) { $argList += @('-Dest', "`"$Dest`"") }
    Start-Process powershell -Verb RunAs -ArgumentList $argList
    exit
}

# The elevated window closes when the script ends, so keep it open to show what happened.
trap {
    Write-Host "Error: $_" -ForegroundColor Red
    if ($Elevated) { Read-Host 'Flowstate was not installed. Press Enter to close' | Out-Null }
    exit 1
}
function Done([string]$message) {
    Write-Host $message
    if ($Elevated) { Read-Host 'Press Enter to close' | Out-Null }
    exit
}

$standard = Join-Path $env:CommonProgramFiles 'VST3'
$dest = if ($Dest) { $Dest } else { $standard }
$bundles = @('Flowstate.vst3', 'Flowstate MIDI FX.vst3')

# Clear the destination, and the standard folder too: an older copy left there would show up twice in a
# DAW that scans both.
foreach ($b in $bundles) {
    foreach ($dir in @($dest, $standard) | Select-Object -Unique) {
        $target = Join-Path $dir $b
        if (Test-Path $target) {
            try { Remove-Item -Recurse -Force $target }
            catch { throw "Couldn't replace $target. Close any DAW that has Flowstate loaded and try again." }
            Write-Host "  removed $target"
        }
    }
}
if ($Uninstall) { Done 'Flowstate is removed. Rescan plug-ins in your DAW.' }

New-Item -ItemType Directory -Force $dest | Out-Null
foreach ($b in $bundles) {
    Copy-Item -Recurse (Join-Path $PSScriptRoot "VST3\$b") $dest
    # Files from a downloaded zip carry Windows' download mark; some hosts refuse to load them with it.
    Get-ChildItem -Recurse -File (Join-Path $dest $b) | Unblock-File
    Write-Host "  $(Join-Path $dest $b)"
}
$build = Get-Content (Join-Path $PSScriptRoot 'BUILD_ID') -ErrorAction SilentlyContinue
Done "Installed Flowstate $build. Rescan plug-ins in your DAW."
