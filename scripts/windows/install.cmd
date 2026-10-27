@echo off
rem Double-click to install Flowstate (asks for admin). Arguments go to install.ps1, e.g.
rem   install.cmd -Uninstall
rem   install.cmd -Service https://flowstate.example.com -Token fst_...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
if errorlevel 1 (
  echo.
  echo Flowstate wasn't installed. Send this window's text to whoever sent you Flowstate.
  pause
  exit /b 1
)
