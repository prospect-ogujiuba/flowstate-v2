# Opens the component gallery (P1-8, check 12 in docs/testing-plugin.md) instead of the normal UI.
#   powershell -ExecutionPolicy Bypass -File .\gallery.ps1                        (the Standalone app)
#   powershell -ExecutionPolicy Bypass -File .\gallery.ps1 -Daw "C:\...\daw.exe"  (a DAW; quit it first)
param([string]$Daw)
$ErrorActionPreference = 'Stop'

# The plugin reads this at startup, so the DAW has to be started from here to see it.
$env:FLOWSTATE_UI_PAGE = 'gallery.html'
if ($Daw) {
    if (-not (Test-Path $Daw)) { throw "No such program: $Daw" }
    Start-Process $Daw
} else {
    Start-Process (Join-Path $PSScriptRoot 'Standalone\Flowstate.exe')
}
