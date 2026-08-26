@echo off
setlocal

set "JLINK=C:\Progra~1\SEGGER\JLink_V970\JLink.exe"
set "SCRIPT=%~dp0k380-prekernel2-sequence.jlink"
set "LOGDIR=%~dp0logs"
set "LOG=%LOGDIR%\k380-prekernel2-sequence.log"

if not exist "%JLINK%" (
  echo JLink.exe not found: %JLINK%
  exit /b 1
)

if not exist "%LOGDIR%" mkdir "%LOGDIR%"

echo Running K380 PRE_KERNEL_2 sequence trace...
echo Close Ozone, RTT Viewer, and J-Link GDB Server before running this.
"%JLINK%" -CommanderScript "%SCRIPT%" > "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"

type "%LOG%"
echo.
echo Log saved to: %LOG%
exit /b %RC%
